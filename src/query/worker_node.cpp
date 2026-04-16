#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <iostream>
#include <limits>
#include <netinet/in.h>
#include <pthread.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

#include "../index/src/lib/disk_chunk_reader.h"
#include "../index/src/lib/query_engine.h"
#include "./index/src/lib/Common.h"

#include "../utils/hash/HashTable.h"
#include "../utils/string.hpp"
#include "../utils/threads/lock_guard.hpp"
#include "../utils/threads/mutex.hpp"
#include "../utils/vector.hpp"

struct MetaRecord {
    string url;
    string title;
    string snippet;
};

// bind previous chunk related thread args into a struct
struct ChunkDescriptor {
    string base_name;
    DiskChunkReader *body_reader = nullptr;
    DiskChunkReader *anchor_reader = nullptr;
    QueryEngine *engine = nullptr;
    vector<MetaRecord> meta;
    double max_static_score = 0.0;
};

struct GlobalMatch {
    size_t chunk_id = 0;
    uint32_t doc_id = 0;
    double score = 0.0;
};

// Query wrapper 
struct QueryTask {
    size_t chunk_id = 0;
};

struct ThreadArgs {
    int client_socket;
    vector<ChunkDescriptor> *chunks;
};

namespace {
struct Location {
    size_t chunk_id;
    size_t doc_id;
};

string to_string(double score) {
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "%.4f", score);
    return string(buffer);
}

string derive_anchor_path(const string &anchor_dir, const string &base_name) {
    if (anchor_dir.empty()) {
        return "";
    }

    string candidate;

    if (base_name.find("chunk_") == 0 && base_name.length() > 6) {
        string suffix = base_name.substr(6);
        candidate = anchor_dir + "/anchor_" + suffix + ".idx";
    }

    if (access(candidate.c_str(), F_OK) == 0) {
        return candidate;
    }

    return "";
}
  
static bool anchorKeyEqual(string a, string b) { return a == b; }

static uint64_t anchorKeyHash(string key) { return hashString(key.cstr()); }

class TopKHeap {
  public:
    explicit TopKHeap(size_t k) : k_(k) { heap_.reserve(k + 1); }

    void push(const GlobalMatch &item) {
        if (k_ == 0) {
            return;
        }

        if (heap_.size() < k_) {
            heap_.pushBack(item);
            heapifyUp(static_cast<int>(heap_.size() - 1));
        } else if (item.score > heap_[0].score) {
            heap_[0] = item;
            heapifyDown(0);
        }
    }

    double minScore() const {
        if (heap_.size() < k_ || heap_.empty()) {
            return -std::numeric_limits<double>::infinity();
        }
        return heap_[0].score;
    }

    vector<GlobalMatch> extractSorted() {
        vector<GlobalMatch> sorted_results;
        while (!heap_.empty()) {
            sorted_results.pushBack(heap_[0]);
            heap_[0] = heap_.back();
            heap_.popBack();
            if (!heap_.empty()) {
                heapifyDown(0);
            }
        }
        for (size_t i = 0; i < sorted_results.size() / 2; ++i) {
            std::swap(sorted_results[i], sorted_results[sorted_results.size() - 1 - i]);
        }
        return sorted_results;
    }

  private:
    vector<GlobalMatch> heap_;
    size_t k_;

    void heapifyUp(int index) {
        while (index > 0) {
            int parent = (index - 1) / 2;
            if (heap_[index].score < heap_[parent].score) {
                std::swap(heap_[index], heap_[parent]);
                index = parent;
            } else {
                break;
            }
        }
    }

    void heapifyDown(int index) {
        int size = static_cast<int>(heap_.size());
        while (true) {
            int left = 2 * index + 1;
            int right = 2 * index + 2;
            int smallest = index;

            if (left < size && heap_[left].score < heap_[smallest].score) {
                smallest = left;
            }
            if (right < size && heap_[right].score < heap_[smallest].score) {
                smallest = right;
            }

            if (smallest != index) {
                std::swap(heap_[index], heap_[smallest]);
                index = smallest;
            } else {
                break;
            }
        }
    }
};

struct QueryWorkerArgs {
    const vector<QueryTask> *tasks = nullptr;
    vector<ChunkDescriptor> *chunks = nullptr;
    const string *query = nullptr;
    size_t K = 0;
    TopKHeap *top_k_heap = nullptr;
    mutex *heap_mutex = nullptr;
    std::atomic<double> *shared_threshold = nullptr;
    std::atomic<size_t> *next_task = nullptr;
};

void *QueryWorkerThread(void *arg) {
    QueryWorkerArgs *worker_args = static_cast<QueryWorkerArgs *>(arg);

    while (true) {
        size_t task_index = worker_args->next_task->fetch_add(1);
        if (task_index >= worker_args->tasks->size()) {
            break;
        }

        const QueryTask &task = (*worker_args->tasks)[task_index];
        ChunkDescriptor &chunk = (*worker_args->chunks)[task.chunk_id];
        vector<ScoredDocument> chunk_matches =
            chunk.engine->search(*worker_args->query, worker_args->K, worker_args->shared_threshold);
        if (chunk_matches.empty()) {
            continue;
        }

        lock_guard<mutex> guard(*worker_args->heap_mutex);
        for (const ScoredDocument &match : chunk_matches) {
            worker_args->top_k_heap->push({task.chunk_id, match.doc_id, match.score});
        }
        worker_args->shared_threshold->store(worker_args->top_k_heap->minScore());
    }

    return nullptr;
}

vector<MetaRecord> load_meta_records(const string &meta_path) {
    vector<MetaRecord> chunk_meta;
    FILE *fp = fopen(meta_path.c_str(), "r");
    if (!fp) {
        return chunk_meta;
    }

    char line[4096];
    while (fgets(line, sizeof(line), fp)) {
        string s(line);
        if (!s.empty() && s.back() == '\n') {
            s.pop_back();
        }

        size_t tab1 = s.find('\t');
        size_t tab2 = s.find('\t', tab1 + 1);

        if (tab1 != string::npos && tab2 != string::npos) {
            string url = s.substr(0, tab1);
            string title = s.substr(tab1 + 1, tab2 - tab1 - 1);
            string snippet = s.substr(tab2 + 1);
            chunk_meta.pushBack({url, title, snippet});
        }
    }

    fclose(fp);
    return chunk_meta;
}

void *handle_master_connection(void *args) {
    ThreadArgs *t_args = static_cast<ThreadArgs *>(args);
    int sock = t_args->client_socket;

    char buffer[1024];
    memset(buffer, 0, sizeof(buffer));
    ssize_t bytes_read = read(sock, buffer, sizeof(buffer) - 1);

    if (bytes_read > 0) {
        string query(buffer);
        while (!query.empty() && (query.back() == '\n' || query.back() == '\r')) {
            query.pop_back();
        }

        size_t K = 10;
        size_t sep = query.find('\t');
        if (sep != string::npos) {
            string k_prefix = query.substr(0, sep);
            char *endptr = nullptr;
            long parsed = std::strtol(k_prefix.c_str(), &endptr, 10);
            if (endptr != k_prefix.c_str() && *endptr == '\0' && parsed > 0) {
                K = static_cast<size_t>(parsed);
                query = query.substr(sep + 1);
            }
        }

        vector<QueryTask> tasks;
        tasks.reserve(t_args->chunks->size());
        for (size_t i = 0; i < t_args->chunks->size(); ++i) {
            QueryTask task;
            task.chunk_id = i;
            tasks.pushBack(task);
        }

        TopKHeap top_k_heap(K);
        mutex heap_mutex;
        std::atomic<double> shared_threshold(-std::numeric_limits<double>::infinity());
        std::atomic<size_t> next_task(0);

        size_t thread_count = tasks.size();
        vector<pthread_t> workers;
        vector<QueryWorkerArgs> worker_args;
        workers.reserve(thread_count);
        worker_args.reserve(thread_count);

        for (size_t worker_id = 0; worker_id < thread_count; ++worker_id) {
            QueryWorkerArgs args;
            args.tasks = &tasks;
            args.chunks = t_args->chunks;
            args.query = &query;
            args.K = K;
            args.top_k_heap = &top_k_heap;
            args.heap_mutex = &heap_mutex;
            args.shared_threshold = &shared_threshold;
            args.next_task = &next_task;
            worker_args.pushBack(args);

            pthread_t worker;
            if (pthread_create(&worker, nullptr, QueryWorkerThread,
                               &worker_args[worker_args.size() - 1]) == 0) {
                workers.pushBack(worker);
            }
        }

        for (pthread_t &worker : workers) {
            pthread_join(worker, nullptr);
        }

        vector<GlobalMatch> matches = top_k_heap.extractSorted();
        string response;
        for (const GlobalMatch &match : matches) {
            if (match.doc_id >= (*t_args->chunks)[match.chunk_id].meta.size()) {
                continue;
            }

            MetaRecord &meta = (*t_args->chunks)[match.chunk_id].meta[match.doc_id];
            response += meta.url + "\t" + meta.title + "\t" + meta.snippet + "\t" +
                        to_string(match.score) + "\n";
        }

        response += "END_OF_RESULTS\n";
        send(sock, response.c_str(), response.size(), 0);
    }

    close(sock);
    delete t_args;
    return nullptr;
}

} // namespace

int main(int argc, char **argv) {
    if (argc < 3) {
        std::cerr << "Usage: ./worker_node <port> <directory_path>\n";
        return 1;
    }

    int port = std::stoi(argv[1]);
    string dir_path = argv[2];
    if (!dir_path.empty() && dir_path.back() == '/') {
        dir_path.pop_back();
    }

    vector<ChunkDescriptor> chunks;
    DIR *dir;
    struct dirent *ent;

    string body_index_dir = dir_path + "/body_index";
    string anchor_index_dir = dir_path + "/anchor_index";
    string meta_dir = dir_path + "/meta";

    if ((dir = opendir(body_index_dir.c_str())) != nullptr) {
        while ((ent = readdir(dir)) != nullptr) {
            string file_name = ent->d_name;
            if (file_name.length() < 4 || file_name.substr(file_name.length() - 4) != ".idx") {
                continue;
            }

            if (file_name.find("anchor_") == 0) {
                continue;
            }

            string base_name = file_name.substr(0, file_name.length() - 4);
            string body_path = body_index_dir + "/" + file_name;
            string anchor_path = derive_anchor_path(anchor_index_dir, base_name);
            string meta_path = meta_dir + "/" + base_name + ".meta";

            DiskChunkReader *body_reader = new DiskChunkReader();
            if (!body_reader->open(body_path)) {
                delete body_reader;
                continue;
            }

            DiskChunkReader *anchor_reader = nullptr;
            if (!anchor_path.empty()) {
                anchor_reader = new DiskChunkReader();
                if (!anchor_reader->open(anchor_path)) {
                    delete anchor_reader;
                    anchor_reader = nullptr;
                }
            }

            QueryEngine *engine = anchor_reader != nullptr
                                      ? new QueryEngine(*body_reader, *anchor_reader)
                                      : new QueryEngine(*body_reader);

            ChunkDescriptor chunk;
            chunk.base_name = base_name;
            chunk.body_reader = body_reader;
            chunk.anchor_reader = anchor_reader;
            chunk.engine = engine;
            chunk.meta = load_meta_records(meta_path);
            chunk.max_static_score = engine->maxStaticScore();
            chunks.push_back(std::move(chunk));

            std::cout << "Loaded chunk: " << base_name << " (" << chunks.back().meta.size()
                      << " docs";
            if (anchor_reader != nullptr) {
                std::cout << ", anchor enabled";
            }
            std::cout << ")\n";
        }
    }

    if (chunks.empty()) {
        std::cerr << "Fatal Error: No body chunks found in " << dir_path << "\n";
        return 1;
    }

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        std::cerr << "Fatal Error: Could not create socket.\n";
        return 1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        std::cerr << "Fatal Error: Bind failed.\n";
        return 1;
    }

    if (listen(server_fd, 100) < 0) {
        std::cerr << "Fatal Error: Listen failed.\n";
        return 1;
    }

    while (true) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int client_sock = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);

        if (client_sock >= 0) {
            ThreadArgs *args = new ThreadArgs{client_sock, &chunks};

            pthread_t thread_id;
            if (pthread_create(&thread_id, nullptr, handle_master_connection, args) == 0) {
                pthread_detach(thread_id);
            } else {
                std::cerr << "Error: Failed to create thread.\n";
                close(client_sock);
                delete args;
            }
        }
    }

    return 0;
}
