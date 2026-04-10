#include <algorithm> // TODO: Remove when custom sorting is implemented
#include <arpa/inet.h>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/socket.h>
#include <unistd.h>

#include "../utils/string.hpp"
#include "../utils/vector.hpp"

struct GlobalResult {
    ::string url;
    double score;

    bool operator>(const GlobalResult &other) const { return score > other.score; }
    bool operator<(const GlobalResult &other) const { return score < other.score; }
};

struct WorkerArgs {
    ::string ip;
    int port;
    ::string query;
    ::vector<GlobalResult> local_results;
};

::string to_string(double score) {
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "%.4f", score);
    return ::string(buffer);
}

::string to_string(size_t n) {
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "%zu", n);
    return ::string(buffer);
}

void *fetch_from_worker(void *args) {
    WorkerArgs *wa = (WorkerArgs *)args;

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(wa->port);
    inet_pton(AF_INET, wa->ip.c_str(), &serv_addr.sin_addr);

    // Set a 500ms timeout
    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 500000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) >= 0) {
        ::string msg = wa->query + "\n";
        send(sock, msg.c_str(), msg.size(), 0);

        char buffer[4096];
        ::string raw_response = "";

        while (true) {
            memset(buffer, 0, sizeof(buffer));
            ssize_t bytes_read = read(sock, buffer, sizeof(buffer) - 1);
            if (bytes_read <= 0)
                break;

            raw_response += buffer;
            if (raw_response.find("END_OF_RESULTS") != ::string::npos)
                break;
        }

        size_t pos = 0;
        while ((pos = raw_response.find('\n')) != ::string::npos) {
            ::string line = raw_response.substr(0, pos);
            raw_response.erase(0, pos + 1);

            if (line == "END_OF_RESULTS")
                break;

            size_t space_pos = line.find(' ');
            if (space_pos != ::string::npos) {
                ::string url = line.substr(0, space_pos);
                double score = atoi(line.substr(space_pos + 1).c_str());
                wa->local_results.push_back({url, score});
            }
        }
    }
    close(sock);
    return nullptr;
}

struct ClientArgs {
    int socket;
};

::string url_decode(const ::string &src) {
    ::string ret;
    char ch;
    for (size_t i = 0; i < src.length(); i++) {
        if (src[i] == '%') {
            int ii;
            sscanf(src.substr(i + 1, 2).data(), "%x", &ii);
            ch = static_cast<char>(ii);
            ret += ch;
            i += 2;
        } else if (src[i] == '+') {
            ret += ' ';
        } else {
            ret += src[i];
        }
    }
    return ret;
}

void *handle_frontend(void *args) {
    ClientArgs *ca = (ClientArgs *)args;
    int client_sock = ca->socket;

    char buffer[4096];
    memset(buffer, 0, sizeof(buffer));
    read(client_sock, buffer, sizeof(buffer) - 1);
    ::string http_request(buffer);

    // Parse HTTP GET /search?q=cat HTTP/1.1
    ::string query = "";
    size_t q_pos = http_request.find("GET /search?q=");
    if (q_pos != ::string::npos) {
        size_t start = q_pos + 14;
        size_t end = http_request.substr(start).find(' ');
        if (end != ::string::npos)
            end += start;
        ::string raw_query = http_request.substr(start, end - start);
        query = url_decode(raw_query);
    }

    if (query.empty()) {
        ::string response = "HTTP/1.1 400 Bad Request\r\n\r\nMissing query";
        send(client_sock, response.c_str(), response.size(), 0);
        close(client_sock);
        delete ca;
        return nullptr;
    }

    // Launch concurrent pthreads to query the Workers
    // TODO: load these from a config file
    ::vector<WorkerArgs> workers = {{"localhost", 8081, query, {}}, {"localhost", 8082, query, {}}};

    ::vector<pthread_t> threads(workers.size());
    for (size_t i = 0; i < workers.size(); ++i) {
        pthread_create(&threads[i], nullptr, fetch_from_worker, &workers[i]);
    }

    for (size_t i = 0; i < threads.size(); ++i) {
        pthread_join(threads[i], nullptr);
    }

    // merge results from all workers into a single list
    ::vector<GlobalResult> all_results;
    for (const auto &w : workers) {
        for (const auto &res : w.local_results) {
            all_results.push_back(res);
        }
    }

    // TODO: Implement a more efficient Top-K selection algorithm instead of sorting the entire list
    // TODO: Implement a custom sorting function if needed instead of std::sort
    // std::sort(all_results.begin(), all_results.end(),
    //           [](const GlobalResult &a, const GlobalResult &b) { return a.score > b.score; });

    // TODO: Change the hardcoded 10
    if (all_results.size() > 10) {
        all_results.resize(10);
    }

    // Manually format the JSON response
    ::string json = "{\n  \"query\": \"" + query + "\",\n  \"results\": [\n";
    for (size_t i = 0; i < all_results.size(); ++i) {
        json += "    {\"url\": \"" + all_results[i].url +
                "\", \"score\": " + to_string(all_results[i].score) + "}";
        if (i < all_results.size() - 1)
            json += ",\n";
        else
            json += "\n";
    }
    json += "  ]\n}";

    // Manually format the HTTP Response Header with CORS allowed
    ::string http_response = "HTTP/1.1 200 OK\r\n"
                             "Content-Type: application/json\r\n"
                             "Access-Control-Allow-Origin: *\r\n"
                             "Content-Length: " +
                             to_string(json.size()) +
                             "\r\n"
                             "\r\n" +
                             json;

    send(client_sock, http_response.c_str(), http_response.size(), 0);

    close(client_sock);
    delete ca;
    return nullptr;
}

int main() {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(8080); // Broker always listens on 8080

    bind(server_fd, (struct sockaddr *)&address, sizeof(address));
    listen(server_fd, 100);

    std::cout << "[MASTER NODE] Broker HTTP Server listening on port 8080...\n";

    while (true) {
        int client_sock = accept(server_fd, nullptr, nullptr);
        if (client_sock >= 0) {
            ClientArgs *args = new ClientArgs{client_sock};
            pthread_t thread_id;
            pthread_create(&thread_id, nullptr, handle_frontend, args);
            pthread_detach(thread_id);
        }
    }
    return 0;
}