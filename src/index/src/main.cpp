// src/main.cpp
#include "lib/chunk_flusher.h"
#include "lib/disk_chunk_reader.h"
#include "lib/in_memory_index.h"
#include "lib/tokenizer.h"

#include <exception>
#include <iostream>
#include <vector>

// this is a simple integration interface to show how the components fit together.

int main() {
    // 1. Mock the crawler output using the HtmlParser interface
    std::vector<HtmlParser> docs = {
        // Initialization: {words}, {titleWords}, {links}, base_url
        {{"The", "CAT", "sat."}, {}, {}, "http://example.com/cats"},
        {{"A", "Dog", "Running", "after", "the", "cat!"}, {}, {}, "http://example.com/dogs"},
    };

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
    const std::string path = "chunk_0001.idx";
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

    // Reusing the teammate's ISR component
    ISR isr = reader.createISR("cat");
    std::cout << "Posting list for 'cat': ";
    while (!isr.done()) {
        std::cout << isr.next() << ", ";
    }
    std::cout << "\n";

    auto doc0 = reader.getDocument(0);
    if (doc0) {
        std::cout << "doc0 url=" << doc0->url << ", range=[" << doc0->start_location << ", "
                  << doc0->end_location << "]\n";
    }

    auto doc1 = reader.getDocument(1);
    if (doc1) {
        std::cout << "doc1 url=" << doc1->url << ", range=[" << doc1->start_location << ", "
                  << doc1->end_location << "]\n";
    }

    // get all documents containing "cat" by iterating through
    // the ISR and mapping locations to documents
    isr = reader.createISR("cat");

    std::cout << "Pages containing 'cat':\n";

    std::string last_printed_url = "";

    // iterate through every location
    while (!isr.done()) {
        uint32_t loc = isr.next();

        std::cout << "Looking up location: " << loc << "\n";
        // map the location to the actual Document Record
        auto doc = reader.getDocumentByLocation(loc);

        // if (doc.has_value() && doc->url != last_printed_url) {
        std::cout << "- " << doc->url << " (Found at location: " << loc << ")\n";
        last_printed_url = doc->url;
        // }
    }

    return 0;
}