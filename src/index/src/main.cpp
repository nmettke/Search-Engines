// src/main.cpp
#include "lib/chunk_flusher.h"
#include "lib/disk_chunk_reader.h"
#include "lib/in_memory_index.h"
#include "lib/query_engine.h"
#include "lib/tokenizer.h"

#include <cstdint>
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
        {{"Cat", "and", "Dog", "are", "friends."}, {}, {}, "http://example.com/friends"},
        {{"No", "cats", "here."}, {}, {}, "http://example.com/nocats"},
        {{"Just", "a", "cat."}, {}, {}, "http://example.com/justacat"},
        {{"I", "love", "my", "cat."}, {}, {}, "http://example.com/lovecat"},
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

    auto cat_isr = reader.createISR("cat");
    std::cout << "Posting list for 'cat': ";
    while (!cat_isr || !cat_isr->done()) {
        std::cout << cat_isr->currentLocation() << ", ";
        cat_isr->next();
    }
    std::cout << "\n";

    auto dog_isr = reader.createISR("dog");
    std::cout << "Posting list for 'dog': ";
    while (!dog_isr || !dog_isr->done()) {
        std::cout << dog_isr->currentLocation() << ", ";
        dog_isr->next();
    }
    std::cout << "\n";

    auto doc_end_isr = reader.createISR(docEndToken);
    std::cout << "Posting list for docEndToken: ";
    while (!doc_end_isr || !doc_end_isr->done()) {
        std::cout << doc_end_isr->currentLocation() << ", ";
        doc_end_isr->next();
    }
    std::cout << "\n";

    for (uint32_t i = 0; i < reader.header().num_documents; i++) {
        auto doc = reader.getDocument(i);
        if (doc) {
            std::cout << "Document " << i << ": url=" << doc->url << ", range=["
                      << doc->start_location << ", " << doc->end_location << "]\n";
        }
    }

    // get all documents containing "cat" by iterating through
    // the ISR and mapping locations to documents
    cat_isr = reader.createISR("cat");

    std::cout << "Pages containing 'cat':\n";

    std::string last_printed_url = "";

    // iterate through every location
    while (!cat_isr || !cat_isr->done()) {
        uint32_t loc = cat_isr->currentLocation();
        cat_isr->next();

        std::cout << "Looking up location: " << loc << "\n";
        // map the location to the actual Document Record
        auto doc = reader.getDocumentByLocation(loc);

        // if (doc.has_value() && doc->url != last_printed_url) {
        std::cout << "- " << doc->url << " (Found at location: " << loc << ")\n";
        last_printed_url = doc->url;
    }

    QueryEngine engine(reader);

    // Search for a multi-word phrase!
    // TODO: tokenize the query
    std::vector<std::string> query = {"cat", "dog"};

    auto results = engine.search(query);

    std::cout << "\nResults for 'cat dog':\n";
    for (const auto &doc : results) {
        std::cout << "- " << doc.url << "\n";
    }

    return 0;
}