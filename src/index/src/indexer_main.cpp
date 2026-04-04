// Indexer process: listens on a Unix domain socket, receives framed WireDocuments
// from the crawler, tokenizes and builds an in-memory index, then flushes a chunk.

#include "ipc/unix_socket.h"
#include "ipc/wire_document.h"

#include "lib/chunk_flusher.h"
#include "lib/html_parser.h"
#include "lib/in_memory_index.h"
#include "lib/tokenizer.h"

#include <sys/socket.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

static HtmlParser wireToHtmlParser(WireDocument &&wd) {
    HtmlParser hp;
    hp.base = std::move(wd.base);
    hp.words = std::move(wd.words);
    hp.titleWords = std::move(wd.titleWords);
    for (auto &wl : wd.links) {
        Link lk(std::move(wl.url));
        lk.anchorText = std::move(wl.anchorText);
        hp.links.push_back(std::move(lk));
    }
    return hp;
}

int main(int argc, char **argv) {
    const char *socket_path = "/tmp/search_engine_index.sock";
    const char *chunk_path = "chunk_0001.idx";
    if (argc >= 2)
        socket_path = argv[1];
    if (argc >= 3)
        chunk_path = argv[2];

    int listen_fd = ipcUnixListen(socket_path);
    if (listen_fd < 0) {
        std::perror("ipcUnixListen");
        std::cerr << "Failed to bind Unix socket: " << socket_path << "\n";
        return 1;
    }

    std::cout << "Indexer listening on " << socket_path << "\n";
    std::cout
        << "Start crawler (connects to same path; override with SEARCH_ENGINE_INDEX_SOCKET).\n";

    int client_fd = ::accept(listen_fd, nullptr, nullptr);
    ::close(listen_fd);
    listen_fd = -1;

    if (client_fd < 0) {
        std::perror("accept");
        return 1;
    }

    Tokenizer tokenizer;
    InMemoryIndex index;
    std::vector<std::uint8_t> payload;
    std::vector<WireDocument> docs;
    std::size_t doc_count = 0;

    while (wireRecvFramedMessage(client_fd, payload)) {
        if (!wireDecodePayloadDocuments(payload.data(), payload.size(), docs)) {
            std::cerr << "Bad document payload, closing.\n";
            break;
        }

        for (auto &wd : docs) {
            HtmlParser doc = wireToHtmlParser(std::move(wd));
            auto tokenized = tokenizer.processDocument(doc);
            for (const auto &tok : tokenized.tokens) {
                index.addToken(tok);
            }
            index.finishDocument(tokenized.doc_end);
            ++doc_count;
        }
    }

    ::close(client_fd);

    std::cout << "Indexed " << doc_count << " document(s) from crawler.\n";

    const std::string path = chunk_path;
    try {
        flushIndexChunk(index, path);
        std::cout << "Wrote chunk to: " << path << "\n";
    } catch (const std::exception &e) {
        std::cerr << "Failed to write chunk: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
