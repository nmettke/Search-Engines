#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <iostream>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/socket.h>
#include <unistd.h>

#include "../index/src/lib/disk_chunk_reader.h"
#include "../index/src/lib/query_engine.h"
#include "./index/src/lib/Common.h"

#include "../utils/hash/HashTable.h"
#include "../utils/string.hpp"
#include "../utils/vector.hpp"

struct MetaRecord {
    string url;
    string title;
    string snippet;
};

struct GlobalMatch {
    size_t chunk_id;
    uint32_t doc_id;
    double score;
};

struct ThreadArgs {
    int client_socket;
    vector<QueryEngine *> *engines;
    vector<vector<MetaRecord>> *all_meta;
};

struct ChunkSearchArgs {
    QueryEngine *engine;
    string query;
    size_t K;
    size_t chunk_id;

    vector<ScoredDocument> local_matches; // TODO
};

struct Location {
    size_t chunk_id;
    size_t doc_id;
};

string to_string(double score) {
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "%.4f", score);
    return string(buffer);
}

static bool anchorKeyEqual(string a, string b) { return a == b; }

static uint64_t anchorKeyHash(string key) { return hashString(key.cstr()); }

class TopKHeap {
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
        int size = heap_.size();
        while (true) {
            int left = 2 * index + 1;
            int right = 2 * index + 2;
            int smallest = index;

            if (left < size && heap_[left].score < heap_[smallest].score)
                smallest = left;
            if (right < size && heap_[right].score < heap_[smallest].score)
                smallest = right;

            if (smallest != index) {
                std::swap(heap_[index], heap_[smallest]);
                index = smallest;
            } else {
                break;
            }
        }
    }

  public:
    TopKHeap(size_t k) : k_(k) { heap_.reserve(k + 1); }

    void push(const GlobalMatch &item) {
        if (heap_.size() < k_) {
            heap_.push_back(item);
            heapifyUp(heap_.size() - 1);
        } else if (item.score > heap_[0].score) {
            heap_[0] = item;
            heapifyDown(0);
        }
    }

    vector<GlobalMatch> extractSorted() {
        vector<GlobalMatch> sorted_results;
        while (!heap_.empty()) {
            sorted_results.push_back(heap_[0]);
            heap_[0] = heap_.back();
            heap_.popBack();
            heapifyDown(0);
        }
        for (size_t i = 0; i < sorted_results.size() / 2; ++i) {
            std::swap(sorted_results[i], sorted_results[sorted_results.size() - 1 - i]);
        }
        return sorted_results;
    }
};

void *search_chunk_thread(void *arg) {
    ChunkSearchArgs *args = (ChunkSearchArgs *)arg;

    args->local_matches = args->engine->search(args->query, args->K);

    pthread_exit(nullptr);
    return nullptr;
}

void *handle_master_connection(void *args) {
    ThreadArgs *t_args = (ThreadArgs *)args;
    int sock = t_args->client_socket;
    vector<QueryEngine *> *engines = t_args->engines;

    char buffer[1024];
    memset(buffer, 0, sizeof(buffer));
    ssize_t bytes_read = read(sock, buffer, sizeof(buffer) - 1);

    if (bytes_read > 0) {
        string query(buffer);

        while (!query.empty() && (query.back() == '\n' || query.back() == '\r')) {
            query.pop_back();
        }

        // Message format is either "query" (legacy) or "k\tquery".
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

        size_t num_engines = engines->size();
        pthread_t *threads = new pthread_t[num_engines];
        ChunkSearchArgs *thread_args = new ChunkSearchArgs[num_engines];

        for (size_t i = 0; i < num_engines; ++i) {
            thread_args[i].engine = (*engines)[i];
            thread_args[i].query = query;
            thread_args[i].K = K;
            thread_args[i].chunk_id = i;

            pthread_create(&threads[i], nullptr, search_chunk_thread, &thread_args[i]);
        }

        for (size_t i = 0; i < num_engines; ++i) {
            pthread_join(threads[i], nullptr);
        }

        TopKHeap top_k_heap(K);
        for (size_t i = 0; i < num_engines; ++i) {
            for (const auto &match : thread_args[i].local_matches) {
                top_k_heap.push({thread_args[i].chunk_id, match.doc_id, match.score});
            }
        }

        vector<GlobalMatch> matches = top_k_heap.extractSorted();

        delete[] threads;
        delete[] thread_args;

        // Format the response: "url score\n"
        string response = "";
        for (const auto &match : matches) {
            MetaRecord &meta = (*t_args->all_meta)[match.chunk_id][match.doc_id];

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

int main(int argc, char **argv) {
    if (argc < 3) {
        std::cerr << "Usage: ./worker_node <port> <directory_path>\n";
        return 1;
    }

    int port = std::stoi(argv[1]);
    string dir_path = argv[2];

    // Ensure directory path doesn't end with a slash
    if (!dir_path.empty() && dir_path.back() == '/')
        dir_path.pop_back();

    vector<DiskChunkReader *> readers;
    vector<QueryEngine *> engines;
    vector<vector<MetaRecord>> all_meta;

    DIR *dir;
    struct dirent *ent;
    string index_dir = dir_path + "/parsed_anchor_index";
    string meta_dir = dir_path + "/meta";

    {
        HashTable<string, Location> index_lookup(anchorKeyEqual, anchorKeyHash);

        if ((dir = opendir(index_dir.c_str())) != nullptr) {
            while ((ent = readdir(dir)) != nullptr) {
                string file_name = ent->d_name;

                if (file_name.length() >= 4 && file_name.substr(file_name.length() - 4) == ".idx") {
                    string idx_path = index_dir + "/" + file_name;

                    DiskChunkReader *reader = new DiskChunkReader();
                    if (reader->open(idx_path)) {
                        QueryEngine *engine = new QueryEngine(*reader);
                        readers.pushBack(reader);
                        engines.pushBack(engine);

                        auto header = reader->header();
                        std::cout << "Loaded chunk: " << idx_path << " with "
                                  << header.num_documents << " documents\n";

                        for (size_t doc_id = 0; doc_id < header.num_documents; ++doc_id) {
                            auto doc_info = reader->getDocument(doc_id);
                            index_lookup.Find(doc_info->url, {readers.size() - 1, doc_id});
                        }
                    }
                }
            }
            closedir(dir);
        } else {
            std::cerr << "Fatal Error: Could not open directory " << dir_path << "\n";
            return 1;
        }

        // Initialize all_meta with empty records for each document in each chunk
        for (size_t i = 0; i < readers.size(); i++) {
            auto header = readers[i]->header();
            vector<MetaRecord> chunk_meta(header.num_documents);
            all_meta.pushBack(chunk_meta);
        }

        // load all meta data and put into all_meta vector
        if ((dir = opendir(meta_dir.c_str())) != nullptr) {
            while ((ent = readdir(dir)) != nullptr) {
                string file_name = ent->d_name;

                if (file_name.length() >= 5 &&
                    file_name.substr(file_name.length() - 5) == ".meta") {
                    string meta_path = meta_dir + "/" + file_name;

                    FILE *fp = fopen(meta_path.c_str(), "r");
                    if (fp) {
                        char line[4096];
                        while (fgets(line, sizeof(line), fp)) {
                            string s(line);
                            if (!s.empty() && s.back() == '\n')
                                s.pop_back();

                            size_t tab1 = s.find('\t');
                            size_t tab2 = s.find('\t', tab1 + 1);

                            if (tab1 != string::npos && tab2 != string::npos) {
                                string url = s.substr(0, tab1);
                                string title = s.substr(tab1 + 1, tab2 - tab1 - 1);
                                string snippet = s.substr(tab2 + 1);

                                Tuple<string, Location> *loc_tuple = index_lookup.Find(url);
                                if (loc_tuple != nullptr) {
                                    Location loc = loc_tuple->value;
                                    all_meta[loc.chunk_id][loc.doc_id] = {url, title, snippet};
                                }
                            }
                        }
                        fclose(fp);
                    }
                }
            }
            closedir(dir);
        } else {
            std::cerr << "Fatal Error: Could not open directory " << meta_dir << "\n";
            return 1;
        }
    }

    if (engines.empty()) {
        std::cerr << "Fatal Error: No .idx files found in " << dir_path << "\n";
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

    if (listen(server_fd, 100) < 0) { // Allow a backlog of 100 connections
        std::cerr << "Fatal Error: Listen failed.\n";
        return 1;
    }

    // std::cout << "[WORKER NODE] Listening on port " << port << " for index directory: "
    //           << dir_path << "\n";

    while (true) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);

        int client_sock = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);

        if (client_sock >= 0) {
            ThreadArgs *args = new ThreadArgs{client_sock, &engines, &all_meta};

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