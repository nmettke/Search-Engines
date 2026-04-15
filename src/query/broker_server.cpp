#include <arpa/inet.h>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/stat.h>

#include "../utils/vector.hpp"
#include "Plugin.h"
#include "SearchCommon.h"

PluginObject *Plugin = nullptr;

char *RootDirectory;

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

    // plugin stuff
    size_t sp1 = http_request.find(' ');
    size_t sp2 = http_request.find(' ', sp1 + 1);
    if (sp1 != string::npos && sp2 != string::npos) {
        string raw_path = http_request.substr(sp1 + 1, sp2 - sp1 - 1);
        string path_only = raw_path;
        size_t qmark = path_only.find('?');
        if (qmark != string::npos)
            path_only = path_only.substr(0, qmark);

        if (Plugin && Plugin->MagicPath(path_only)) {
            string response = Plugin->ProcessRequest(string(buffer));
            send(client_sock, response.c_str(), response.size(), 0);
            close(client_sock);
            delete ca;
            return nullptr;
        }

        // serves files statically
        string method = http_request.substr(0, sp1);
        if (method == "GET" && RootDirectory) {
            string fullpath = string(RootDirectory) + "/" + path_only;
            int f = open(fullpath.c_str(), O_RDONLY);
            if (f >= 0) {
                struct stat info;
                fstat(f, &info);
                if ((info.st_mode & S_IFMT) != S_IFDIR) {

                    const char *content_type = "application/octet-stream";
                    size_t dot = string::npos;
                    for (size_t i = path_only.size(); i > 0; --i)
                        if (path_only[i - 1] == '.') {
                            dot = i - 1;
                            break;
                        }
                    if (dot != string::npos) {
                        string ext = path_only.substr(dot);
                        if (ext == ".html" || ext == ".htm")
                            content_type = "text/html";
                        else if (ext == ".css")
                            content_type = "text/css";
                        else if (ext == ".js")
                            content_type = "application/javascript";
                        else if (ext == ".json")
                            content_type = "application/json";
                        else if (ext == ".png")
                            content_type = "image/png";
                        else if (ext == ".jpg" || ext == ".jpeg")
                            content_type = "image/jpeg";
                        else if (ext == ".gif")
                            content_type = "image/gif";
                        else if (ext == ".svg")
                            content_type = "image/svg+xml";
                        else if (ext == ".ico")
                            content_type = "image/x-icon";
                    }

                    char header[1024];
                    int hlen = snprintf(header, sizeof(header),
                                        "HTTP/1.1 200 OK\r\n"
                                        "Content-Length: %lld\r\n"
                                        "Connection: close\r\n"
                                        "Content-Type: %s\r\n\r\n",
                                        (long long)info.st_size, content_type);
                    send(client_sock, header, hlen, 0);

                    char filebuf[10240];
                    ssize_t r;
                    while ((r = read(f, filebuf, sizeof(filebuf))) > 0)
                        send(client_sock, filebuf, r, 0);

                    close(f);
                    close(client_sock);
                    delete ca;
                    return nullptr;
                }
                close(f);
            }
        }
    }

    // Parse HTTP GET /search?q=cat HTTP/1.1
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

    auto query_start = std::chrono::steady_clock::now();

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

    auto query_end = std::chrono::steady_clock::now();
    double elapsed_ms = std::chrono::duration<double, std::milli>(query_end - query_start).count();

    // Manually format the JSON response
    string json = "{\n  \"query\": \"" + query + "\",\n  \"elapsed_ms\": " + to_string(elapsed_ms) +
                  ",\n  \"results\": [\n";
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

int main(int argc, char **argv) {
    if (argc != 3) {
        std::cerr << "Usage:  " << argv[0] << " port rootdirectory" << std::endl;
        return 1;
    }

    int port = atoi(argv[1]);
    RootDirectory = argv[2];

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    ::bind(server_fd, (struct sockaddr *)&address, sizeof(address));
    listen(server_fd, 100);

    std::cout << "[MASTER NODE] Broker HTTP Server listening on port " << port << "...\n";
    std::cout << "Serving files from " << RootDirectory << "/\n";
    if (Plugin)
        std::cout << "Search plugin loaded.\n";

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
