#include <cstring>
#include <dirent.h>
#include <iostream>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/socket.h>
#include <unistd.h>

#include "../index/src/lib/disk_chunk_reader.h"
#include "../index/src/lib/query_engine.h"

#include "../utils/string.hpp"
#include "../utils/vector.hpp"

struct ThreadArgs {
    int client_socket;
    ::vector<QueryEngine *> *engines;
};

::string to_string(double score) {
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "%.4f", score);
    return ::string(buffer);
}

class TopKHeap {
  private:
    ::vector<ScoredDocument> heap_;
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

    void push(const ScoredDocument &item) {
        if (heap_.size() < k_) {
            heap_.push_back(item);
            heapifyUp(heap_.size() - 1);
        } else if (item.score > heap_[0].score) {
            heap_[0] = item;
            heapifyDown(0);
        }
    }

    ::vector<ScoredDocument> extractSorted() {
        ::vector<ScoredDocument> sorted_results;
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

void *handle_master_connection(void *args) {
    ThreadArgs *t_args = (ThreadArgs *)args;
    int sock = t_args->client_socket;
    ::vector<QueryEngine *> *engines = t_args->engines;

    char buffer[1024];
    memset(buffer, 0, sizeof(buffer));
    ssize_t bytes_read = read(sock, buffer, sizeof(buffer) - 1);

    if (bytes_read > 0) {
        ::string query(buffer);

        while (!query.empty() && (query.back() == '\n' || query.back() == '\r')) {
            query.pop_back();
        }

        // TODO: Default value for K, can be modified to read from query if needed
        size_t K = 10;
        TopKHeap top_k_heap(K);
        for (QueryEngine *engine : *engines) {
            ::vector<ScoredDocument> chunk_matches = engine->search(query, K);
            for (const auto &match : chunk_matches) {
                top_k_heap.push(match); // Keep only the best K across ALL files
            }
        }
        ::vector<ScoredDocument> matches = top_k_heap.extractSorted();

        // Format the response: "url score\n"
        ::string response = "";
        for (const auto &match : matches) {
            response += match.doc.url + " " + to_string(match.score) + "\n";
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
    ::string dir_path = argv[2];

    // Ensure directory path doesn't end with a slash
    if (!dir_path.empty() && dir_path.back() == '/')
        dir_path.pop_back();

    ::vector<DiskChunkReader *> readers;
    ::vector<QueryEngine *> engines;

    DIR *dir;
    struct dirent *ent;
    if ((dir = opendir(dir_path.c_str())) != nullptr) {
        while ((ent = readdir(dir)) != nullptr) {
            ::string file_name = ent->d_name;

            // If the file ends with ".idx"
            if (file_name.length() >= 4 && file_name.substr(file_name.length() - 4) == ".idx") {
                ::string full_path = dir_path + "/" + file_name;

                DiskChunkReader *reader = new DiskChunkReader();
                if (reader->open(full_path)) {
                    QueryEngine *engine = new QueryEngine(*reader);
                    readers.push_back(reader);
                    engines.push_back(engine);
                    std::cout << "Loaded chunk: " << file_name << "\n";
                } else {
                    delete reader;
                }
            }
        }
        closedir(dir);
    } else {
        std::cerr << "Fatal Error: Could not open directory " << dir_path << "\n";
        return 1;
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

    std::cout << "[WORKER NODE] Listening on port " << port << " for index directory: " << dir_path
              << "\n";

    while (true) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);

        int client_sock = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);

        if (client_sock >= 0) {
            ThreadArgs *args = new ThreadArgs{client_sock, &engines};

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