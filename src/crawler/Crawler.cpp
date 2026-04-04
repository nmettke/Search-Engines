// File stub for crawler orchestrator file
#include "Crawler.h"
#include "ipc/unix_socket.h"
#include "ipc/wire_document.h"
#include "url_dedup.h"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <pthread.h>
#include <unistd.h>
#include <vector>

Frontier f("src/crawler/seedList.txt");
UrlBloomFilter bloom(1000000, 0.0001);
unsigned int cores = std::thread::hardware_concurrency();

static std::mutex g_index_send_mutex;
static int g_index_socket_fd = -1;

static std::string toStdString(const string &s) {
    return std::string(s.cstr(), s.size());
}

static void tryConnectIndexerSocket() {
    const char *path = std::getenv("SEARCH_ENGINE_INDEX_SOCKET");
    if (path && std::strcmp(path, "-") == 0) {
        std::cerr << "Indexer IPC disabled (SEARCH_ENGINE_INDEX_SOCKET=-).\n";
        return;
    }
    if (!path || path[0] == '\0')
        path = "/tmp/search_engine_index.sock";

    int fd = ipcUnixConnect(path);
    if (fd >= 0) {
        g_index_socket_fd = fd;
        std::cerr << "Connected to indexer at " << path << "\n";
        return;
    }
    std::cerr << "Warning: could not connect to indexer at " << path
              << " (start indexer first, or set SEARCH_ENGINE_INDEX_SOCKET=- to skip)\n";
}

static void sendParsedToIndexer(const string &page_url, const HtmlParser &parsed) {
    if (g_index_socket_fd < 0)
        return;

    WireDocument wd;
    wd.base = toStdString(page_url);
    wd.words.reserve(parsed.words.size());
    for (const auto &w : parsed.words)
        wd.words.push_back(toStdString(w));
    wd.titleWords.reserve(parsed.titleWords.size());
    for (const auto &w : parsed.titleWords)
        wd.titleWords.push_back(toStdString(w));
    wd.links.reserve(parsed.links.size());
    for (const auto &link : parsed.links) {
        WireLink wl;
        wl.url = toStdString(link.URL);
        wl.anchorText.reserve(link.anchorText.size());
        for (const auto &a : link.anchorText)
            wl.anchorText.push_back(toStdString(a));
        wd.links.push_back(std::move(wl));
    }

    std::vector<std::uint8_t> payload;
    wireEncodeDocument(wd, payload);

    std::lock_guard<std::mutex> lock(g_index_send_mutex);
    if (g_index_socket_fd < 0)
        return;
    if (!wireSendFramedMessage(g_index_socket_fd, payload)) {
        std::cerr << "Indexer send failed; closing indexer connection.\n";
        ::close(g_index_socket_fd);
        g_index_socket_fd = -1;
    }
}

void *WorkerThread(void *arg) {
    while (std::optional<FrontierItem> item = f.pop()) {
        struct TaskCompletionGuard {
            Frontier &frontier;
            ~TaskCompletionGuard() { frontier.taskDone(); }
        } taskCompletionGuard{f};

        string page = readURL(item->link);
        HtmlParser parsed(page.cstr(), page.size());
        vector<string> discoveredLinks;

        for (const Link &link : parsed.links) {
            if (link.URL.find("http") != link.URL.npos) {
                string canonical;
                if (shouldEnqueueUrl(link.URL, bloom, canonical)) {
                    discoveredLinks.pushBack(canonical);
                }
            }
        }

        sendParsedToIndexer(item->link, parsed);

        f.pushMany(discoveredLinks);
        std::cout << "Crawled " << item->link << '\n';
    }
    std::cout << "No more Frontier!" << '\n';

    return nullptr;
}

int main() {
    tryConnectIndexerSocket();

    size_t ThreadCount = cores * 3;
    vector<pthread_t> threads(ThreadCount);

    for (int i = 0; i < ThreadCount; i++) {
        pthread_create(&threads[i], nullptr, WorkerThread, nullptr);
    }

    for (int i = 0; i < ThreadCount; i++) {
        pthread_join(threads[i], nullptr);
    }

    if (g_index_socket_fd >= 0) {
        ::close(g_index_socket_fd);
        g_index_socket_fd = -1;
    }
}
