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
    ::vector<HtmlParser> docs1_1 = {
        {{"The", "CAT", "sat."}, {}, {}, "doc1"},
        {{"A", "Dog", "Running", "after", "my", "cat!"}, {}, {}, "doc2"},
    };

    ::vector<HtmlParser> docs1_2 = {
        {{"My", "cat", "and", "my", "dog", "are", "friends."}, {}, {}, "doc3"},
        {{"No", "cats", "here."}, {}, {}, "doc4"},
    };

    // make a directory "chunk1" and put the first two chunks in there
    system("mkdir -p chunk1");
    create_dummy_index("chunk1/chunk_0001.idx", docs1_1);
    create_dummy_index("chunk1/chunk_0002.idx", docs1_2);

    ::vector<HtmlParser> docs2_1 = {
        {{"Just", "a", "cat."}, {}, {}, "doc5"},
        {{"I", "love", "my", "cat.", "and", "my", "dog."}, {}, {}, "doc6"},
    };

    ::vector<HtmlParser> docs2_2 = {
        {{"My", "cat", "hates", "dogs."}, {}, {}, "doc7"},
        {{"My", "cat", "loves", "ice", "cream."}, {}, {}, "doc8"},
    };

    // make a directory "chunk2" and put the first two chunks in there
    system("mkdir -p chunk2");
    create_dummy_index("chunk2/chunk_0001.idx", docs2_1);
    create_dummy_index("chunk2/chunk_0002.idx", docs2_2);
    return 0;
}