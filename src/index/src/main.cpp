// src/main.cpp
#include "lib/chunk_flusher.h"
#include "lib/disk_chunk_reader.h"
#include "lib/in_memory_index.h"
#include "lib/query_engine.h"
#include "lib/tokenizer.h"

#include "utils/string.hpp"
#include "utils/vector.hpp"
#include <cstdint>
#include <exception>
#include <iostream>

// this is a simple integration interface to show how the components fit together.

int main() {
    // 1. Mock the crawler output using the HtmlParser interface
    ::vector<HtmlParser> docs;
    // Initialization: {words}, {titleWords}, {links}, base_url
    docs.pushBack(HtmlParser{{"The", "CAT", "sat."}, {}, {}, "doc1"});
    docs.pushBack(HtmlParser{{"A", "Dog", "Running", "after", "my", "cat!"}, {}, {}, "doc2"});
    docs.pushBack(HtmlParser{{"My", "cat", "and", "my", "dog", "are", "friends."}, {}, {}, "doc3"});
    docs.pushBack(HtmlParser{{"No", "cats", "here."}, {}, {}, "doc4"});
    docs.pushBack(HtmlParser{{"Just", "a", "cat."}, {}, {}, "doc5"});
    docs.pushBack(HtmlParser{{"I", "love", "my", "cat.", "and", "my", "dog."}, {}, {}, "doc6"});
    docs.pushBack(HtmlParser{{"My", "cat", "hates", "dogs."}, {}, {}, "doc7"});
    docs.pushBack(HtmlParser{{"My", "cat", "loves", "ice", "cream."}, {}, {}, "doc8"});

    // 2. Tokenize and build the In-Memory Index
    Tokenizer tokenizer;
    InMemoryIndex index;
    for (const auto &doc : docs) {
        auto tokenized = tokenizer.processDocument(doc);
        for (const auto &tok : tokenized.tokens) {
            index.addToken(tok);
        }
        index.finishDocument(tokenized.doc_end);
    }

    // 3. Flush the chunk to disk using OUR flusher interface
    const ::string path = "chunk_0001.idx";
    try {
        flushIndexChunk(index, path);
        std::cout << "Successfully wrote chunk to: " << path << "\n";
    } catch (const std::exception &e) {
        std::cerr << "Failed to write chunk: " << e.what() << "\n";
        return 1;
    }

    DiskChunkReader reader;
    if (!reader.open(path)) {
        std::cerr << "Failed to open chunk\n";
        return 1;
    }

    std::cout << "Chunk written and reopened: " << path << "\n";
    std::cout << "docs=" << reader.header().num_documents
              << ", terms=" << reader.header().num_unique_terms
              << ", total_locations=" << reader.header().total_locations << "\n";

    QueryEngine engine(reader);

    ::string query1 = "cat AND dog";
    ::string query2 = "cat OR dog";
    ::string query3 = "\"my cat\" -dog";
    ::string query4 = "\"my dog\"";

    auto results1 = engine.search(query1);
    auto results2 = engine.search(query2);
    auto results3 = engine.search(query3);
    auto results4 = engine.search(query4);

    std::cout << "\nResults for '" << query1 << "':\n";
    for (const auto &doc : results1) {
        if (auto record = reader.getDocument(doc.doc_id)) {
            std::cout << "- " << record->url << " (" << doc.score << ")\n";
        }
    }

    std::cout << "\nResults for '" << query2 << "':\n";
    for (const auto &doc : results2) {
        if (auto record = reader.getDocument(doc.doc_id)) {
            std::cout << "- " << record->url << " (" << doc.score << ")\n";
        }
    }

    std::cout << "\nResults for '" << query3 << "':\n";
    for (const auto &doc : results3) {
        if (auto record = reader.getDocument(doc.doc_id)) {
            std::cout << "- " << record->url << " (" << doc.score << ")\n";
        }
    }

    std::cout << "\nResults for '" << query4 << "':\n";
    for (const auto &doc : results4) {
        if (auto record = reader.getDocument(doc.doc_id)) {
            std::cout << "- " << record->url << " (" << doc.score << ")\n";
        }
    }

    return 0;
}
