#include <arpa/inet.h>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/socket.h>
#include <unistd.h>

#include "../utils/string.hpp"
#include "../utils/vector.hpp"

struct GlobalResult {
    string url;
    string title;
    string snippet;
    double score;

    bool operator>(const GlobalResult &other) const { return score > other.score; }
    bool operator<(const GlobalResult &other) const { return score < other.score; }
};

class GlobalTopKHeap {
  private:
    ::vector<GlobalResult> heap_;
    size_t k_;

    void heapifyUp(int index) {
        while (index > 0) {
            int parent = (index - 1) / 2;
            if (heap_[index].score < heap_[parent].score) {
                GlobalResult temp = heap_[index];
                heap_[index] = heap_[parent];
                heap_[parent] = temp;

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
                GlobalResult temp = heap_[index];
                heap_[index] = heap_[smallest];
                heap_[smallest] = temp;

                index = smallest;
            } else {
                break;
            }
        }
    }

  public:
    GlobalTopKHeap(size_t k) : k_(k) { heap_.reserve(k + 1); }

    void push(const GlobalResult &item) {
        if (heap_.size() < k_) {
            heap_.pushBack(item);
            heapifyUp(heap_.size() - 1);
        } else if (item.score > heap_[0].score) {
            heap_[0] = item;
            heapifyDown(0);
        }
    }

    ::vector<GlobalResult> extractSorted() {
        ::vector<GlobalResult> sorted;
        while (!heap_.empty()) {
            sorted.pushBack(heap_[0]);
            heap_[0] = heap_.back();
            heap_.popBack();
            if (!heap_.empty())
                heapifyDown(0);
        }

        size_t n = sorted.size();
        for (size_t i = 0; i < n / 2; ++i) {
            GlobalResult temp = sorted[i];
            sorted[i] = sorted[n - 1 - i];
            sorted[n - 1 - i] = temp;
        }
        return sorted;
    }
};

struct WorkerArgs {
    string ip;
    int port;
    string query;
    size_t k;
    ::vector<GlobalResult> local_results;
};

string to_string(double score) {
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "%.4f", score);
    return string(buffer);
}

string to_string(size_t n) {
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "%zu", n);
    return string(buffer);
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
    // std::cout << "[MASTER NODE] Broker HTTP Server listening on port 8080...\n";

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) >= 0) {
        string msg = to_string(wa->k) + "\t" + wa->query + "\n";
        send(sock, msg.c_str(), msg.size(), 0);

        char buffer[4096];
        string raw_response = "";

        while (true) {
            memset(buffer, 0, sizeof(buffer));
            ssize_t bytes_read = read(sock, buffer, sizeof(buffer) - 1);
            if (bytes_read <= 0)
                break;

            raw_response += buffer;
            if (raw_response.find("END_OF_RESULTS") != string::npos)
                break;
        }

        size_t pos = 0;
        while ((pos = raw_response.find('\n')) != string::npos) {
            string line = raw_response.substr(0, pos);
            raw_response.erase(0, pos + 1);

            if (line == "END_OF_RESULTS")
                break;

            size_t t1 = line.find('\t');
            size_t t2 = line.find('\t', t1 + 1);
            size_t t3 = line.find('\t', t2 + 1);

            if (t1 != string::npos && t2 != string::npos && t3 != string::npos) {
                string url = line.substr(0, t1);
                string title = line.substr(t1 + 1, t2 - t1 - 1);
                string snippet = line.substr(t2 + 1, t3 - t2 - 1);
                double score = atof(line.substr(t3 + 1).c_str());

                wa->local_results.pushBack({url, title, snippet, score});
            }
        }
    }
    close(sock);
    return nullptr;
}

struct ClientArgs {
    int socket;
};

string url_decode(const string &src) {
    string ret;
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

bool parse_positive_k(const string &raw_k, size_t &k_out) {
    if (raw_k.empty())
        return false;

    char *endptr = nullptr;
    long parsed = std::strtol(raw_k.c_str(), &endptr, 10);
    if (endptr == raw_k.c_str() || *endptr != '\0' || parsed <= 0)
        return false;

    k_out = static_cast<size_t>(parsed);
    return true;
}

void *handle_frontend(void *args) {
    ClientArgs *ca = (ClientArgs *)args;
    int client_sock = ca->socket;

    char buffer[4096];
    memset(buffer, 0, sizeof(buffer));
    read(client_sock, buffer, sizeof(buffer) - 1);
    string http_request(buffer);

    // Parse HTTP GET /search?q=cat&k=20 HTTP/1.1
    string query = "";
    size_t k = 10;
    size_t get_pos = http_request.find("GET ");
    if (get_pos != string::npos) {
        size_t target_start = get_pos + 4;
        size_t target_end = http_request.find(' ', target_start);
        if (target_end != string::npos) {
            string target = http_request.substr(target_start, target_end - target_start);
            size_t qmark = target.find('?');
            string path = target.substr(0, qmark);

            if (path == "/search" && qmark != string::npos) {
                string query_string = target.substr(qmark + 1);
                string raw_query;
                string raw_k;

                size_t pos = 0;
                while (pos <= query_string.size()) {
                    size_t amp = query_string.find('&', pos);
                    if (amp == string::npos)
                        amp = query_string.size();

                    string param = query_string.substr(pos, amp - pos);
                    size_t eq = param.find('=');
                    if (eq != string::npos) {
                        string key = param.substr(0, eq);
                        string value = param.substr(eq + 1);

                        if (key == "q")
                            raw_query = value;
                        else if (key == "k")
                            raw_k = value;
                    }

                    if (amp == query_string.size())
                        break;
                    pos = amp + 1;
                }

                query = url_decode(raw_query);
                if (!raw_k.empty()) {
                    size_t parsed_k = 0;
                    if (!parse_positive_k(url_decode(raw_k), parsed_k)) {
                        string response = "HTTP/1.1 400 Bad Request\r\n\r\nInvalid k";
                        send(client_sock, response.c_str(), response.size(), 0);
                        close(client_sock);
                        delete ca;
                        return nullptr;
                    }
                    k = parsed_k;
                }
            }
        }
    }

    if (query.empty()) {
        string response = "HTTP/1.1 400 Bad Request\r\n\r\nMissing query";
        send(client_sock, response.c_str(), response.size(), 0);
        close(client_sock);
        delete ca;
        return nullptr;
    }

    // Launch concurrent pthreads to query the Workers
    // TODO: load these from a config file
    ::vector<WorkerArgs> workers = {{"localhost", 8081, query, k, {}},
                                    {"localhost", 8082, query, k, {}}};

    ::vector<pthread_t> threads(workers.size());
    for (size_t i = 0; i < workers.size(); ++i) {
        pthread_create(&threads[i], nullptr, fetch_from_worker, &workers[i]);
    }

    for (size_t i = 0; i < threads.size(); ++i) {
        pthread_join(threads[i], nullptr);
    }

    // Merge results and get global top k
    GlobalTopKHeap top_k(k);
    for (const auto &w : workers) {
        for (const auto &res : w.local_results) {
            top_k.push(res);
        }
    }

    ::vector<GlobalResult> final_results = top_k.extractSorted();

    // Manually format the JSON response
    string json = "{\n  \"query\": \"" + query + "\",\n  \"results\": [\n";
    for (size_t i = 0; i < final_results.size(); ++i) {
        json += "    {\n";
        json += "      \"url\": \"" + final_results[i].url + "\",\n";
        json += "      \"title\": \"" + final_results[i].title + "\",\n";
        json += "      \"snippet\": \"" + final_results[i].snippet + "\",\n";
        json += "      \"score\": " + to_string(final_results[i].score) + "\n";
        json += "    }";
        if (i < final_results.size() - 1)
            json += ",";
        json += "\n";
    }
    json += "  ]\n}";

    // Manually format the HTTP Response Header with CORS allowed
    string http_response = "HTTP/1.1 200 OK\r\n"
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
