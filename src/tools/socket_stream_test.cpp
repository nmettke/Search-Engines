// Minimal TCP stream test using the same host:port and socket patterns as
// main_distributed.cpp (sendBatchToPeer / openListeningSocket / recv loop).
//
// --- GCP defaults (cache-me-if-you-can) — edit if IPs change -----------------
//   crawler-1 (sender)   external IP: 34.58.160.109
//   crawler-3 (listener) external IP: 34.135.152.6
//
// Default listen bind is 0.0.0.0:8081 on crawler-3. Do NOT bind(34.135.152.6):
// the public IP is not on the VM NIC; use 0.0.0.0 and connect from crawler-1
// to 34.135.152.6:8081.
//
// If connect() hangs or listen never accepts, check in order:
//   1) VPC firewall: ingress TCP 8081 to crawler-3, source 34.58.160.109/32
//      (or the subnet used by crawler-1). "Allow internal" alone does not add
//      arbitrary ports.
//   2) Same-region / routing: both VMs must reach each other on that path.
//   3) OS firewall on crawler-3: ufw status; iptables -L; allow 8081/tcp if
//      a host firewall is enabled.
//   4) Ephemeral external IPs change if the instance is recreated — update
//      the constants below and firewall rules.
// ----------------------------------------------------------------------------

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

// File scope so main() and the anonymous-namespace helpers share the same defaults.
constexpr const char kCrawler1ExternalIp[] = "34.58.160.109";
constexpr const char kCrawler3ExternalIp[] = "34.135.152.6";
constexpr const char kDefaultListenBind[] = "0.0.0.0:8081";
constexpr const char kDefaultSendPeer[] = "34.135.152.6:8081";

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
        std::cerr << "[send] invalid peer address: " << peer << " — " << err << '\n';
        return -1;
    }
    std::cerr << "[send] parsed peer: host=\"" << host << "\" port=\"" << port << "\"\n";

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    std::cerr << "[send] resolving " << host << ":" << port << " (getaddrinfo)...\n" << std::flush;
    addrinfo *result = nullptr;
    int gai = getaddrinfo(host.c_str(), port.c_str(), &hints, &result);
    if (gai != 0) {
        std::cerr << "[send] getaddrinfo failed for " << peer << ": " << gai_strerror(gai) << '\n';
        return -1;
    }
    std::cerr << "[send] resolve ok\n";

    int socketFd = -1;
    int attempt = 0;
    for (addrinfo *rp = result; rp != nullptr; rp = rp->ai_next) {
        attempt++;
        const char *fam = (rp->ai_family == AF_INET)     ? "IPv4"
                          : (rp->ai_family == AF_INET6) ? "IPv6"
                                                        : "other";
        std::cerr << "[send] trying endpoint #" << attempt << " (" << fam << "): socket()...\n"
                  << std::flush;
        socketFd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (socketFd < 0) {
            std::cerr << "[send] socket() failed: " << std::strerror(errno) << '\n';
            continue;
        }
        std::cerr << "[send] connect() to " << peer << " (blocks until success/refused/timeout)...\n"
                  << std::flush;
        if (connect(socketFd, rp->ai_addr, rp->ai_addrlen) == 0) {
            std::cerr << "[send] TCP connected, fd=" << socketFd << '\n';
            break;
        }
        std::cerr << "[send] connect() failed on this endpoint: " << std::strerror(errno) << '\n';
        close(socketFd);
        socketFd = -1;
    }
    freeaddrinfo(result);

    if (socketFd < 0) {
        std::cerr << "[send] giving up: no working route to peer " << peer << '\n';
    }
    return socketFd;
}

int openListeningSocket(const std::string &selfPeer) {
    std::string host, port, err;
    if (!splitHostPort(selfPeer, host, port, err)) {
        std::cerr << "[listen] invalid bind address: " << selfPeer << " — " << err << '\n';
        return -1;
    }
    std::cerr << "[listen] bind address: host=\"" << (host.empty() ? "(any)" : host) << "\" port=\""
              << port << "\"\n";

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    std::cerr << "[listen] resolving bind target (getaddrinfo, AI_PASSIVE)...\n" << std::flush;
    addrinfo *result = nullptr;
    int gai = getaddrinfo(host.empty() ? nullptr : host.c_str(), port.c_str(), &hints, &result);
    if (gai != 0) {
        std::cerr << "[listen] getaddrinfo failed for " << selfPeer << ": " << gai_strerror(gai)
                  << '\n';
        return -1;
    }
    std::cerr << "[listen] resolve ok, trying bind()+listen()...\n" << std::flush;

    int listenFd = -1;
    int attempt = 0;
    for (addrinfo *rp = result; rp != nullptr; rp = rp->ai_next) {
        attempt++;
        const char *fam = (rp->ai_family == AF_INET)     ? "IPv4"
                          : (rp->ai_family == AF_INET6) ? "IPv6"
                                                        : "other";
        std::cerr << "[listen] endpoint #" << attempt << " (" << fam << "): socket()...\n";
        listenFd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (listenFd < 0) {
            std::cerr << "[listen] socket() failed: " << std::strerror(errno) << '\n';
            continue;
        }
        int opt = 1;
        setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        if (bind(listenFd, rp->ai_addr, rp->ai_addrlen) != 0) {
            std::cerr << "[listen] bind() failed: " << std::strerror(errno) << '\n';
            close(listenFd);
            listenFd = -1;
            continue;
        }
        if (listen(listenFd, 32) != 0) {
            std::cerr << "[listen] listen() failed: " << std::strerror(errno) << '\n';
            close(listenFd);
            listenFd = -1;
            continue;
        }
        std::cerr << "[listen] bound and listening on " << selfPeer << ", listen fd=" << listenFd
                  << '\n';
        break;
    }
    freeaddrinfo(result);

    if (listenFd < 0) {
        std::cerr << "[listen] failed: could not bind any address for " << selfPeer << '\n';
    }
    return listenFd;
}

bool sendAll(int fd, const char *data, size_t len, const std::string &peer) {
    const size_t total = len;
    size_t sentSoFar = 0;
    size_t nextProgressLog = 0;
    const size_t kLogEveryBytes = 1024 * 1024;

    while (len > 0) {
        ssize_t sent = send(fd, data, len, 0);
        if (sent < 0) {
            std::cerr << "[send] send() failed to " << peer << " after " << sentSoFar << "/"
                      << total << " bytes: " << std::strerror(errno) << '\n';
            return false;
        }
        data += static_cast<size_t>(sent);
        len -= static_cast<size_t>(sent);
        sentSoFar += static_cast<size_t>(sent);

        if (sentSoFar == total || sentSoFar >= nextProgressLog) {
            std::cerr << "[send] stream progress: " << sentSoFar << "/" << total << " bytes\n"
                      << std::flush;
            while (nextProgressLog <= sentSoFar) {
                nextProgressLog += kLogEveryBytes;
            }
        }
    }
    return true;
}

void printUsage(const char *argv0) {
    std::cerr
        << "Usage:\n"
        << "  " << argv0 << " listen [host:port]\n"
        << "      Default (crawler-3): " << kDefaultListenBind << " — bind all interfaces.\n"
        << "      Reads each connection until the peer closes. Repeat until SIGINT.\n\n"
        << "  " << argv0 << " send [host:port] [lines]\n"
        << "      Default peer (crawler-1 → crawler-3): " << kDefaultSendPeer << "\n"
        << "      Default line count: 500. If the first optional arg has no ':', it is "
           "treated as line count only.\n"
        << "      Each line: url<TAB>anchor1<TAB>anchor2<NEWLINE> (sendBatchToPeer format).\n\n"
        << "GCP (see source header): crawler-1=" << kCrawler1ExternalIp
        << " sender, crawler-3=" << kCrawler3ExternalIp << " listener.\n\n"
        << "Examples:\n"
        << "  " << argv0 << " listen\n"
        << "  " << argv0 << " send\n"
        << "  " << argv0 << " send 2000\n"
        << "  " << argv0 << " listen 127.0.0.1:9000\n"
        << "  " << argv0 << " send 127.0.0.1:9000 100\n";
}

int runListen(const std::string &addr) {
    int listenFd = openListeningSocket(addr);
    if (listenFd < 0) {
        return 1;
    }

    signal(SIGINT, onSignal);
    signal(SIGTERM, onSignal);

    std::cerr << "[listen] main loop: waiting for TCP connections (1s select timeout; Ctrl+C to "
                 "exit)...\n"
              << std::flush;

    int connCount = 0;
    int idleSelectTicks = 0;
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
                if (g_stop) {
                    std::cerr << "[listen] select interrupted (shutting down)\n";
                }
                continue;
            }
            std::cerr << "[listen] select failed: " << std::strerror(errno) << '\n';
            break;
        }
        if (ready == 0) {
            idleSelectTicks++;
            if (idleSelectTicks % 30 == 0) {
                std::cerr << "[listen] still waiting (no inbound connection yet; ~" << idleSelectTicks
                          << "s of idle select)...\n"
                          << std::flush;
            }
            continue;
        }

        std::cerr << "[listen] select: listen socket readable — calling accept()...\n"
                  << std::flush;
        int clientFd = accept(listenFd, nullptr, nullptr);
        if (clientFd < 0) {
            if (errno == EINTR) {
                continue;
            }
            std::cerr << "[listen] accept failed: " << std::strerror(errno) << '\n';
            continue;
        }
        idleSelectTicks = 0;

        connCount++;
        std::cerr << "[listen] accepted connection #" << connCount << ", client fd=" << clientFd
                  << "; reading until peer closes...\n"
                  << std::flush;

        std::string payload;
        char buf[4096];
        size_t recvTotal = 0;
        size_t nextRecvLog = 0;
        const size_t kRecvLogEvery = 1024 * 1024;
        while (true) {
            ssize_t n = recv(clientFd, buf, sizeof(buf), 0);
            if (n == 0) {
                std::cerr << "[listen] peer closed connection (EOF) after " << recvTotal
                          << " payload bytes\n";
                break;
            }
            if (n < 0) {
                if (errno == EINTR) {
                    continue;
                }
                std::cerr << "[listen] recv failed: " << std::strerror(errno) << '\n';
                break;
            }
            payload.append(buf, static_cast<size_t>(n));
            recvTotal += static_cast<size_t>(n);
            if (recvTotal >= nextRecvLog) {
                std::cerr << "[listen] recv progress: " << recvTotal << " bytes so far...\n"
                          << std::flush;
                while (nextRecvLog <= recvTotal) {
                    nextRecvLog += kRecvLogEvery;
                }
            }
        }
        close(clientFd);

        size_t lines = 0;
        for (char c : payload) {
            if (c == '\n') {
                lines++;
            }
        }
        std::cerr << "[listen] [connection " << connCount << "] done: " << payload.size()
                  << " bytes total, " << lines << " newline-terminated lines\n";
        if (!payload.empty() && payload.size() <= 240) {
            std::cerr << "[listen] payload:\n" << payload;
        } else if (!payload.empty()) {
            std::cerr << "[listen] first 120 bytes:\n"
                      << std::string(payload.data(), std::min(payload.size(), size_t{120})) << "...\n";
        }
        std::cerr << "[listen] ready for next connection.\n" << std::flush;
    }

    close(listenFd);
    std::cerr << "[listen] listener shut down.\n";
    return 0;
}

int runSend(const std::string &peer, size_t lineCount) {
    std::cerr << "[send] starting: will send " << lineCount << " synthetic lines to " << peer << '\n'
              << std::flush;

    int fd = connectToPeer(peer);
    if (fd < 0) {
        return 1;
    }

    std::cerr << "[send] building payload in memory...\n" << std::flush;
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

    std::cerr << "[send] payload size " << payload.size() << " bytes; calling send() loop...\n"
              << std::flush;
    if (!sendAll(fd, payload.data(), payload.size(), peer)) {
        close(fd);
        return 1;
    }
    std::cerr << "[send] all bytes acknowledged by kernel send(); closing socket...\n";
    close(fd);
    std::cerr << "[send] done: " << lineCount << " lines (" << payload.size() << " bytes) to "
              << peer << '\n';
    return 0;
}

} // namespace

static bool parseLineCount(const char *s, size_t *out) {
    char *end = nullptr;
    unsigned long v = std::strtoul(s, &end, 10);
    if (end == s || *end != '\0' || v == 0) {
        return false;
    }
    *out = static_cast<size_t>(v);
    return true;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }

    std::string mode = argv[1];

    if (mode == "listen") {
        if (argc > 3) {
            std::cerr << "[main] too many arguments\n";
            printUsage(argv[0]);
            return 1;
        }
        const char *addr = (argc >= 3) ? argv[2] : kDefaultListenBind;
        std::cerr << "[main] socket_stream_test mode=listen";
        if (argc >= 3) {
            std::cerr << " address=\"" << addr << "\" (override)\n";
        } else {
            std::cerr << " address=\"" << addr << "\" (default crawler-3 bind; clients use external "
                      << kCrawler3ExternalIp << ")\n";
        }
        return runListen(addr);
    }

    if (mode == "send") {
        std::string peer = kDefaultSendPeer;
        size_t lines = 500;

        if (argc >= 3) {
            if (std::strchr(argv[2], ':') != nullptr) {
                peer = argv[2];
                if (argc >= 4) {
                    if (!parseLineCount(argv[3], &lines)) {
                        std::cerr << "[main] invalid line count: " << argv[3] << '\n';
                        return 1;
                    }
                }
            } else {
                if (!parseLineCount(argv[2], &lines)) {
                    std::cerr << "[main] expected line count or host:port, got: " << argv[2] << '\n';
                    return 1;
                }
            }
        }
        if (argc > 4) {
            std::cerr << "[main] too many arguments\n";
            printUsage(argv[0]);
            return 1;
        }

        std::cerr << "[main] socket_stream_test mode=send peer=\"" << peer << "\"";
        if (argc >= 3 && std::strchr(argv[2], ':') == nullptr) {
            std::cerr << " (default peer " << kDefaultSendPeer << "; line count from argv)";
        } else if (argc < 3) {
            std::cerr << " (default; crawler-1 → crawler-3)";
        }
        std::cerr << " line_count=" << lines << '\n';
        std::cerr << "[main] hint: ingress to crawler-3 must allow TCP from " << kCrawler1ExternalIp
                  << " (see file header)\n";
        return runSend(peer, lines);
    }

    printUsage(argv[0]);
    return 1;
}
