// tests/test_chunk_flusher.cpp
#include "../../utils/string.hpp"
#include "../src/lib/Common.h" // For hashString
#include "../src/lib/disk_chunk_reader.h"
#include "../src/lib/disk_chunk_writer.h"
#include "../src/lib/in_memory_index.h"
#include <iostream>
#include <sys/stat.h>
#include <unistd.h>

void flushIndexChunk(const InMemoryIndex &mem_index, const ::string &filename);

#define TEST_ASSERT(condition, message)                                                            \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            std::cerr << "FAIL: " << message << " (Line: " << __LINE__ << ")\n";                   \
            exit(1);                                                                               \
        }                                                                                          \
    } while (false)

static DocumentFeatures makeFeatures(uint32_t seed) {
    DocumentFeatures features;
    features.flags = kFeaturesPresent;
    if (seed == 0) {
        features.flags |= kHttps;
    }
    features.base_domain_length = 6 + seed;
    features.url_length = 18 + seed;
    features.path_length = 4 + seed;
    features.path_depth = 1 + seed;
    features.query_param_count = seed;
    features.numeric_path_char_count = 1 + seed;
    features.domain_hyphen_count = seed;
    features.latin_alpha_count = 8 + seed;
    features.total_alpha_count = 9 + seed;
    features.outgoing_link_count = 2 + seed;
    features.outgoing_anchor_word_count = 3 + seed;
    features.raw_tld = seed == 0 ? "com" : "edu";
    return features;
}

void test_end_to_end_flush() {
    std::cout << "Running test_end_to_end_flush...\n";
    const char *test_file = "test_chunk_final.idx";
    unlink(test_file);

    InMemoryIndex mem_index;

    // doc 1: "the cat sat"
    mem_index.addToken({"the", 0});
    mem_index.addToken({"cat", 1});
    mem_index.addToken({"sat", 2});
    DocumentFeatures cats_features = makeFeatures(0);
    mem_index.finishDocument({3, "http://cats.com", 3, 0, 0, 2, cats_features});

    // doc 2: "a dog run"
    mem_index.addToken({"a", 4});
    mem_index.addToken({"dog", 5});
    mem_index.addToken({"run", 6});
    DocumentFeatures dogs_features = makeFeatures(1);
    mem_index.finishDocument({7, "http://dogs.com", 3, 0, 4, 3, dogs_features});

    // flush to disk
    flushIndexChunk(mem_index, test_file);

    // verify file was created and has a reasonable size
    struct stat st;
    TEST_ASSERT(stat(test_file, &st) == 0, "Chunk file should be created");
    TEST_ASSERT(st.st_size > 100, "Chunk file should contain header, lists, docs, and dict");

    DiskChunkReader reader;
    TEST_ASSERT(reader.open(test_file), "Flushed chunk should reopen");

    auto doc0 = reader.getDocument(0);
    auto doc1 = reader.getDocument(1);
    TEST_ASSERT(doc0.has_value(), "Doc 0 should exist");
    TEST_ASSERT(doc1.has_value(), "Doc 1 should exist");
    TEST_ASSERT(doc0->features.query_param_count == cats_features.query_param_count,
                "Flushed doc 0 should preserve features");
    TEST_ASSERT(doc1->features.outgoing_anchor_word_count ==
                    dogs_features.outgoing_anchor_word_count,
                "Flushed doc 1 should preserve features");
    TEST_ASSERT(doc0->seed_distance == 2, "Flushed doc 0 should preserve seed distance");
    TEST_ASSERT(doc1->seed_distance == 3, "Flushed doc 1 should preserve seed distance");
    TEST_ASSERT(doc0->features.raw_tld == cats_features.raw_tld,
                "Flushed doc 0 should preserve raw TLD");

    std::cout << "Successfully flushed chunk! Size: " << st.st_size << " bytes.\n";
    unlink(test_file);
}

int main() {
    test_end_to_end_flush();
    std::cout << "Integration test passed!\n";
    return 0;
}
