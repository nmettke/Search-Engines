// tests/test_chunk_reader.cpp
#include "../src/lib/Common.h"
#include "../src/lib/chunk_flusher.h"
#include "../src/lib/disk_chunk_reader.h"
#include "../src/lib/disk_chunk_writer.h"
#include "../src/lib/in_memory_index.h"
#include <fcntl.h>
#include <iostream>
#include <unistd.h>

#define TEST_ASSERT(condition, message)                                                            \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            std::cerr << "FAIL: " << message << " (Line: " << __LINE__ << ")\n";                   \
            exit(1);                                                                               \
        }                                                                                          \
    } while (false)

static DocumentFeatures makeFeatures(uint8_t seed) {
    DocumentFeatures features;
    features.flags = kFeaturesPresent | kHttps;
    features.base_domain_length = 7 + seed;
    features.url_length = 20 + seed;
    features.path_length = 5 + seed;
    features.path_depth = 1 + seed;
    features.query_param_count = seed;
    features.numeric_path_char_count = 2 + seed;
    features.domain_hyphen_count = seed;
    features.outgoing_link_count = 3 + seed;
    features.outgoing_anchor_word_count = 4 + seed;
    features.raw_tld = seed == 0 ? "com" : "edu";
    return features;
}

static void assertFeaturesEqual(const DocumentFeatures &actual, const DocumentFeatures &expected) {
    TEST_ASSERT(actual.flags == expected.flags, "Feature flags should round-trip");
    TEST_ASSERT(actual.raw_tld == expected.raw_tld, "Raw TLD should round-trip");
    TEST_ASSERT(actual.base_domain_length == expected.base_domain_length,
                "Base domain length should round-trip");
    TEST_ASSERT(actual.url_length == expected.url_length, "URL length should round-trip");
    TEST_ASSERT(actual.path_length == expected.path_length, "Path length should round-trip");
    TEST_ASSERT(actual.path_depth == expected.path_depth, "Path depth should round-trip");
    TEST_ASSERT(actual.query_param_count == expected.query_param_count,
                "Query param count should round-trip");
    TEST_ASSERT(actual.numeric_path_char_count == expected.numeric_path_char_count,
                "Numeric path chars should round-trip");
    TEST_ASSERT(actual.domain_hyphen_count == expected.domain_hyphen_count,
                "Domain hyphen count should round-trip");
    TEST_ASSERT(actual.outgoing_link_count == expected.outgoing_link_count,
                "Outgoing link count should round-trip");
    TEST_ASSERT(actual.outgoing_anchor_word_count == expected.outgoing_anchor_word_count,
                "Outgoing anchor word count should round-trip");
}

void test_open_and_mmap() {
    std::cout << "Running test_open_and_mmap...\n";
    const char *test_file = "test_chunk_reader.idx";
    unlink(test_file);

    // 1. Create a valid chunk file to read
    InMemoryIndex mem_index;
    mem_index.addToken({"test", 0});
    mem_index.finishDocument({1, "http://test.com", 1, 0, 0, 0, {}});
    flushIndexChunk(mem_index, test_file);

    // 2. Test the reader
    DiskChunkReader reader;

    // Should fail on non-existent file
    TEST_ASSERT(!reader.open("does_not_exist.idx"), "Should return false for bad file");

    // Should succeed on our valid file
    TEST_ASSERT(reader.open(test_file), "Should successfully open and mmap the valid chunk");

    // Verify header was parsed correctly
    const FileHeader &header = reader.header();
    TEST_ASSERT(header.magic == magic, "Magic number should match");
    TEST_ASSERT(header.num_documents == 1, "Should have 1 document");
    TEST_ASSERT(header.num_unique_terms == 2, "Should have 2 unique terms (test + #DocEnd)");

    std::cout << "test_open_and_mmap PASSED.\n";
    unlink(test_file);
}

void test_reject_old_version() {
    std::cout << "Running test_reject_old_version...\n";
    const char *test_file = "test_chunk_reader_bad_version.idx";
    unlink(test_file);

    int fd = open(test_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    TEST_ASSERT(fd >= 0, "Should create bad-version chunk");

    FileHeader header{};
    header.version = 3;
    ssize_t written = write(fd, &header, sizeof(FileHeader));
    TEST_ASSERT(written == static_cast<ssize_t>(sizeof(FileHeader)),
                "Should write the header bytes");
    close(fd);

    DiskChunkReader reader;
    TEST_ASSERT(!reader.open(test_file), "Reader should reject the old chunk version");

    unlink(test_file);
    std::cout << "test_reject_old_version PASSED.\n";
}

void test_create_isr() {
    std::cout << "Running test_create_isr...\n";
    const char *test_file = "test_chunk_reader_isr.idx";
    unlink(test_file);

    // 1. Setup a chunk with two terms
    InMemoryIndex mem_index;
    mem_index.addToken({"cat", 5});
    mem_index.addToken({"cat", 10});
    mem_index.finishDocument({12, "http://cat.com", 2, 0, 0, 0, {}});
    flushIndexChunk(mem_index, test_file);

    // 2. Open reader
    DiskChunkReader reader;
    TEST_ASSERT(reader.open(test_file), "Should open chunk");

    // 3. Test successful lookup
    auto isr_cat = reader.createISR("cat");
    TEST_ASSERT(!isr_cat || !isr_cat->done(), "ISR for 'cat' should not be empty");
    TEST_ASSERT(isr_cat->currentLocation() == 5, "First location should be 5");
    TEST_ASSERT(isr_cat->next() == 10, "Second location should be 10");
    isr_cat->next(); // Move past the last posting
    TEST_ASSERT(isr_cat->done(), "ISR should be exhausted");

    // 4. Test missing lookup
    auto isr_dog = reader.createISR("dog");
    TEST_ASSERT(!isr_dog || isr_dog->done(), "ISR for missing term should be instantly done");

    std::cout << "test_create_isr PASSED.\n";
    unlink(test_file);
}

void test_get_document() {
    std::cout << "Running test_get_document...\n";
    const char *test_file = "test_chunk_reader_docs.idx";
    unlink(test_file);

    // 1. Setup a chunk with two documents
    InMemoryIndex mem_index;
    mem_index.addToken({"cat", 0});
    DocumentFeatures cat_features = makeFeatures(0);
    mem_index.finishDocument({1, "http://cat.com", 1, 0, 0, 4, cat_features});

    mem_index.addToken({"dog", 2});
    DocumentFeatures dog_features = makeFeatures(1);
    mem_index.finishDocument({3, "http://dog.com", 1, 0, 2, 5, dog_features});

    flushIndexChunk(mem_index, test_file);

    // 2. Open reader
    DiskChunkReader reader;
    TEST_ASSERT(reader.open(test_file), "Should open chunk");

    // 3. Test retrieving Doc 0
    auto doc0 = reader.getDocument(0);
    TEST_ASSERT(doc0.has_value(), "Doc 0 should exist");
    TEST_ASSERT(doc0->url == "http://cat.com", "Doc 0 URL should match");
    TEST_ASSERT(doc0->start_location == 0, "Doc 0 start location should be 0");
    TEST_ASSERT(doc0->seed_distance == 4, "Doc 0 seed distance should round-trip");
    TEST_ASSERT(doc0->word_count == 1, "Doc 0 word count should be 1");
    assertFeaturesEqual(doc0->features, cat_features);

    // 4. Test retrieving Doc 1
    auto doc1 = reader.getDocument(1);
    TEST_ASSERT(doc1.has_value(), "Doc 1 should exist");
    TEST_ASSERT(doc1->url == "http://dog.com", "Doc 1 URL should match");
    TEST_ASSERT(doc1->start_location == 2, "Doc 1 start location should be 2");
    TEST_ASSERT(doc1->seed_distance == 5, "Doc 1 seed distance should round-trip");
    assertFeaturesEqual(doc1->features, dog_features);

    // 5. Test Out of Bounds
    auto doc2 = reader.getDocument(2);
    TEST_ASSERT(!doc2.has_value(), "Doc 2 should not exist");

    std::cout << "test_get_document PASSED.\n";
    unlink(test_file);
}

int main() {
    test_open_and_mmap();
    test_reject_old_version();
    test_create_isr();
    test_get_document();
    std::cout << "All Chunk Reader tests passed!\n";
    return 0;
}
