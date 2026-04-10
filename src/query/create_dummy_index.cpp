// query/create_dummy_indices.cpp
// This file is a utility to create dummy index files for testing the worker node.

#include "../index/src/lib/chunk_flusher.h"
#include "../index/src/lib/in_memory_index.h"
#include "../index/src/lib/tokenizer.h"

#include <exception>
#include <iostream>
#include <vector>

#include "../utils/string.hpp"
#include "../utils/vector.hpp"

void create_dummy_index(const ::string &path, const ::vector<HtmlParser> &docs) {
    Tokenizer tokenizer;
    InMemoryIndex index;
    for (const auto &doc : docs) {
        auto tokenized = tokenizer.processDocument(doc);
        for (const auto &tok : tokenized.tokens) {
            index.addToken(tok);
        }
        index.finishDocument(tokenized.doc_end);
    }

    try {
        flushIndexChunk(index, path);
        std::cout << "Successfully wrote chunk to: " << path << "\n";
    } catch (const std::exception &e) {
        std::cerr << "Failed to write chunk: " << e.what() << "\n";
    }
}

int main() {
    ::vector<HtmlParser> docs1 = {
        {{"The", "CAT", "sat."}, {}, {}, "doc1"},
        {{"A", "Dog", "Running", "after", "my", "cat!"}, {}, {}, "doc2"},
        {{"My", "cat", "and", "my", "dog", "are", "friends."}, {}, {}, "doc3"},
        {{"No", "cats", "here."}, {}, {}, "doc4"},
    };

    ::vector<HtmlParser> docs2 = {
        {{"Just", "a", "cat."}, {}, {}, "doc5"},
        {{"I", "love", "my", "cat.", "and", "my", "dog."}, {}, {}, "doc6"},
        {{"My", "cat", "hates", "dogs."}, {}, {}, "doc7"},
        {{"My", "cat", "loves", "ice", "cream."}, {}, {}, "doc8"},
    };

    create_dummy_index("chunk_0001.idx", docs1);
    create_dummy_index("chunk_0002.idx", docs2);
    return 0;
}