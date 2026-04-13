// Minimal TCP stream test using the same host:port and socket patterns as
// main_distributed.cpp (sendBatchToPeer / openListeningSocket / recv loop).

#include <arpa/inet.h>
#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <netdb.h>
#include <string>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

std::atomic<bool> g_stop{false};

void onSignal(int) { g_stop = true; }

bool splitHostPort(const std::string &peer, std::string &host, std::string &port, std::string &err) {
    size_t colon = peer.find(':');
    if (colon == std::string::npos || colon == 0 || colon + 1 >= peer.size()) {
        err = "expected host:port (e.g. 127.0.0.1:8081 or 0.0.0.0:8081)";
        return false;
    }
    host = peer.substr(0, colon);
    port = peer.substr(colon + 1);
    if (port.empty()) {
        err = "empty port";
        return false;
    }
    return true;
}

int connectToPeer(const std::string &peer) {
    std::string host, port, err;
    if (!splitHostPort(peer, host, port, err)) {
        std::cerr << "Invalid peer address: " << peer << " — " << err << '\n';
        return -1;
    }

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo *result = nullptr;
    if (getaddrinfo(host.c_str(), port.c_str(), &hints, &result) != 0) {
        std::cerr << "Failed to resolve peer " << peer << '\n';
        return -1;
    }

    int socketFd = -1;
    for (addrinfo *rp = result; rp != nullptr; rp = rp->ai_next) {
        socketFd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (socketFd < 0) {
            continue;
        }
        if (connect(socketFd, rp->ai_addr, rp->ai_addrlen) == 0) {
            break;
        }
        close(socketFd);
        socketFd = -1;
    }
    freeaddrinfo(result);

    if (socketFd < 0) {
        std::cerr << "Failed to connect to peer " << peer << '\n';
    }
    return socketFd;
}

int openListeningSocket(const std::string &selfPeer) {
    std::string host, port, err;
    if (!splitHostPort(selfPeer, host, port, err)) {
        std::cerr << "Invalid listen address: " << selfPeer << " — " << err << '\n';
        return -1;
    }

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    addrinfo *result = nullptr;
    if (getaddrinfo(host.empty() ? nullptr : host.c_str(), port.c_str(), &hints, &result) != 0) {
        std::cerr << "Failed to resolve listen address " << selfPeer << '\n';
        return -1;
    }

    int listenFd = -1;
    for (addrinfo *rp = result; rp != nullptr; rp = rp->ai_next) {
        listenFd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (listenFd < 0) {
            continue;
        }
        int opt = 1;
        setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        if (bind(listenFd, rp->ai_addr, rp->ai_addrlen) == 0 && listen(listenFd, 32) == 0) {
            break;
        }
        close(listenFd);
        listenFd = -1;
    }
    freeaddrinfo(result);

    if (listenFd >= 0) {
        std::cerr << "Listening on " << selfPeer << '\n';
    }
    return listenFd;
}

bool sendAll(int fd, const char *data, size_t len, const std::string &peer) {
    while (len > 0) {
        ssize_t sent = send(fd, data, len, 0);
        if (sent < 0) {
            std::cerr << "send failed to " << peer << ": " << std::strerror(errno) << '\n';
            return false;
        }
        data += static_cast<size_t>(sent);
        len -= static_cast<size_t>(sent);
    }
    return true;
}

void printUsage(const char *argv0) {
    std::cerr
        << "Usage:\n"
        << "  " << argv0 << " listen <host:port>\n"
        << "      Bind and accept connections (same as peer_address for this machine).\n"
        << "      Reads each connection until the peer closes (like ReceiveFromMachineThread).\n"
        << "      Repeat until SIGINT (Ctrl+C).\n\n"
        << "  " << argv0 << " send <host:port> [lines]\n"
        << "      Connect and stream `lines` synthetic batch lines (default 500).\n"
        << "      Each line looks like: url<TAB>anchor1<TAB>anchor2<NEWLINE>\n"
        << "      (same framing as sendBatchToPeer in main_distributed.cpp).\n\n"
        << "Examples (two terminals):\n"
        << "  " << argv0 << " listen 0.0.0.0:9000\n"
        << "  " << argv0 << " send 127.0.0.1:9000 1000\n"
        << "Remote:\n"
        << "  " << argv0 << " listen 0.0.0.0:8081\n"
        << "  " << argv0 << " send 203.0.113.10:8081 200\n";
}

int runListen(const std::string &addr) {
    int listenFd = openListeningSocket(addr);
    if (listenFd < 0) {
        return 1;
    }

    signal(SIGINT, onSignal);
    signal(SIGTERM, onSignal);

    int connCount = 0;
    while (!g_stop) {
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(listenFd, &readSet);
        timeval timeout{};
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        int ready = select(listenFd + 1, &readSet, nullptr, nullptr, &timeout);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            std::cerr << "select: " << std::strerror(errno) << '\n';
            break;
        }
        if (ready == 0) {
            continue;
        }

        int clientFd = accept(listenFd, nullptr, nullptr);
        if (clientFd < 0) {
            if (errno == EINTR) {
                continue;
            }
            std::cerr << "accept: " << std::strerror(errno) << '\n';
            continue;
        }

        connCount++;
        std::string payload;
        char buf[4096];
        while (true) {
            ssize_t n = recv(clientFd, buf, sizeof(buf), 0);
            if (n == 0) {
                break;
            }
            if (n < 0) {
                if (errno == EINTR) {
                    continue;
                }
                std::cerr << "recv: " << std::strerror(errno) << '\n';
                break;
            }
            payload.append(buf, static_cast<size_t>(n));
        }
        close(clientFd);

        size_t lines = 0;
        for (char c : payload) {
            if (c == '\n') {
                lines++;
            }
        }
        std::cerr << "[connection " << connCount << "] received " << payload.size() << " bytes, "
                  << lines << " newline-terminated lines\n";
        if (!payload.empty() && payload.size() <= 240) {
            std::cerr << "payload:\n" << payload;
        } else if (!payload.empty()) {
            std::cerr << "first 120 bytes:\n"
                      << std::string(payload.data(), std::min(payload.size(), size_t{120})) << "...\n";
        }
    }

    close(listenFd);
    std::cerr << "Listener shut down.\n";
    return 0;
}

int runSend(const std::string &peer, size_t lineCount) {
    int fd = connectToPeer(peer);
    if (fd < 0) {
        return 1;
    }

    std::string payload;
    payload.reserve(lineCount * 64);
    for (size_t i = 0; i < lineCount; ++i) {
        payload += "https://example.com/stream-test/";
        char num[32];
        std::snprintf(num, sizeof(num), "%zu", i);
        payload += num;
        payload += '\t';
        payload += "anchor_word_";
        payload += num;
        payload += '\t';
        payload += "second_anchor_";
        payload += num;
        payload += '\n';
    }

    if (!sendAll(fd, payload.data(), payload.size(), peer)) {
        close(fd);
        return 1;
    }
    close(fd);
    std::cerr << "Sent " << lineCount << " lines (" << payload.size() << " bytes) to " << peer
              << '\n';
    return 0;
}

} // namespace

int main(int argc, char **argv) {
    if (argc < 3) {
        printUsage(argv[0]);
        return 1;
    }

    std::string mode = argv[1];
    std::string addr = argv[2];

    if (mode == "listen") {
        return runListen(addr);
    }
    if (mode == "send") {
        size_t lines = 500;
        if (argc >= 4) {
            char *end = nullptr;
            unsigned long v = std::strtoul(argv[3], &end, 10);
            if (end == argv[3] || *end != '\0' || v == 0) {
                std::cerr << "Invalid line count: " << argv[3] << '\n';
                return 1;
            }
            lines = static_cast<size_t>(v);
        }
        return runSend(addr, lines);
    }

    printUsage(argv[0]);
    return 1;
}
