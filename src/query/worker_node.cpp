#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/socket.h>
#include <unistd.h>

#include "../index/src/lib/disk_chunk_reader.h"
#include "../index/src/lib/query_engine.h"

// TODO: Change to self-designed string and vector
#include <string>
#include <vector>
using String = std::string;
template <typename T> using Vector = std::vector<T>;

struct ThreadArgs {
    int client_socket;
    QueryEngine *engine;
};

// TODO: Implement the scoring function based on the document and query
double calculate_score(const DocumentRecord &doc, const String &query) { return 1.0; }

void *handle_master_connection(void *args) {
    ThreadArgs *t_args = (ThreadArgs *)args;
    int sock = t_args->client_socket;
    QueryEngine *engine = t_args->engine;

    char buffer[1024];
    memset(buffer, 0, sizeof(buffer));
    ssize_t bytes_read = read(sock, buffer, sizeof(buffer) - 1);

    if (bytes_read > 0) {
        String query(buffer);

        while (!query.empty() && (query.back() == '\n' || query.back() == '\r')) {
            query.pop_back();
        }

        Vector<DocumentRecord> matches = engine->search(query);

        // Format the response: "url score\n"
        String response = "";

        // TODO: implement Top-K selection algorithm to get the top 500 results
        size_t limit = (matches.size() < 500) ? matches.size() : 500;

        for (size_t i = 0; i < limit; ++i) {
            double score = calculate_score(matches[i], query);
            // TODO: implement score to string conversion if needed
            response += matches[i].url + " " + std::to_string(score) + "\n";
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
        std::cerr << "Usage: ./worker_node <port> <index_file.idx>\n";
        return 1;
    }

    int port = std::stoi(argv[1]);
    String index_file = argv[2];

    DiskChunkReader reader;
    if (!reader.open(index_file)) {
        std::cerr << "Fatal Error: Could not open index file: " << index_file << "\n";
        return 1;
    }
    QueryEngine engine(reader);

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

    std::cout << "[WORKER NODE] Listening on port " << port << " for index: " << index_file << "\n";

    while (true) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);

        int client_sock = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);

        if (client_sock >= 0) {
            ThreadArgs *args = new ThreadArgs{client_sock, &engine};

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