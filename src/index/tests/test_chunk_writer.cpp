// tests/test_chunk_writer.cpp
#include "../src/lib/disk_chunk_writer.h"
#include "../src/lib/types.h"
#include "../src/lib/vbyte.h"
#include <cassert>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <sys/stat.h>
#include <unistd.h>

#define TEST_ASSERT(condition, message)                                                            \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            std::cerr << "FAIL: " << message << " (Line: " << __LINE__ << ")\n";                   \
            exit(1);                                                                               \
        }                                                                                          \
    } while (false)

static DocumentFeatures makeFeatures(uint32_t seed) {
    DocumentFeatures features;
    features.flags = kFeaturesPresent | kHttps;
    features.base_domain_length = 7 + seed;
    features.url_length = 24 + seed;
    features.path_length = 8 + seed;
    features.path_depth = 2 + seed;
    features.query_param_count = 1 + seed;
    features.numeric_path_char_count = 3 + seed;
    features.domain_hyphen_count = seed;
    features.latin_alpha_count = 10 + seed;
    features.total_alpha_count = 12 + seed;
    features.outgoing_link_count = 2 + seed;
    features.outgoing_anchor_word_count = 4 + seed;
    features.raw_tld = "com";
    return features;
}

void test_write_header() {
    std::cout << "Running test_write_header...\n";
    const char *test_file = "test_chunk_0001.idx";

    unlink(test_file);

    FileHeader header;
    header.num_documents = 100;
    header.num_unique_terms = 5000;

    {
        DiskChunkWriter writer(test_file);
        writer.writeHeader(header);
    }

    struct stat st;
    TEST_ASSERT(stat(test_file, &st) == 0, "File should exist on disk");
    TEST_ASSERT(st.st_size == sizeof(FileHeader), "File size should match exactly 56 bytes");

    int fd = open(test_file, O_RDONLY);
    TEST_ASSERT(fd >= 0, "Should be able to open the file for reading");

    FileHeader read_header;
    ssize_t bytes_read = read(fd, &read_header, sizeof(FileHeader));
    TEST_ASSERT(bytes_read == sizeof(FileHeader), "Should read exactly 56 bytes");

    TEST_ASSERT(read_header.magic == magic, "Magic number should match types.h");
    TEST_ASSERT(read_header.num_documents == 100, "num_documents should match");
    TEST_ASSERT(read_header.num_unique_terms == 5000, "num_unique_terms should match");

    close(fd);
    unlink(test_file);
    std::cout << "test_write_header PASSED.\n";
}

void test_write_posting_list() {
    std::cout << "Running test_write_posting_list...\n";
    const char *test_file = "test_chunk_0002.idx";
    unlink(test_file);

    DiskChunkWriter writer(test_file);
    ::vector<uint32_t> dummy_locations;
    dummy_locations.pushBack(4);
    dummy_locations.pushBack(15);
    dummy_locations.pushBack(18);
    dummy_locations.pushBack(302);

    uint64_t offset = writer.writePostingList(dummy_locations);

    TEST_ASSERT(offset == 0, "First posting list should start at offset 0");

    // Size = sizeof(PostingListHeader) (which is 9 bytes) + compressed data size
    // Deltas: [4, 11, 3, 284]. VByte should compress this to roughly 5 bytes.
    struct stat st;
    stat(test_file, &st);
    TEST_ASSERT(static_cast<unsigned long>(st.st_size) > sizeof(PostingListHeader),
                "File should contain header and data");

    unlink(test_file);
    std::cout << "test_write_posting_list PASSED.\n";
}

void test_write_document_table() {
    std::cout << "Running test_write_document_table...\n";
    const char *test_file = "test_chunk_0003.idx";
    unlink(test_file);

    DiskChunkWriter writer(test_file);

    ::vector<DocumentRecord> dummy_docs;
    dummy_docs.push_back({"https://example.com/cats", 0, 3, 3, 1, 0, makeFeatures(0)});
    dummy_docs.push_back({"https://example.com/dogs", 4, 7, 3, 1, 1, makeFeatures(1)});

    uint64_t offset = writer.writeDocumentTable(dummy_docs);
    TEST_ASSERT(offset == 0, "Document table should start at offset 0 in this isolated test");

    struct stat st;
    TEST_ASSERT(stat(test_file, &st) == 0, "File should exist");

    TEST_ASSERT(sizeof(DocumentFeaturesDisk) == 48, "Feature block should be 48 bytes");
    TEST_ASSERT(sizeof(DocumentRecordDisk) == 63, "Document record should be 63 bytes packed");

    // Header: 4 bytes (num_documents)
    // Offsets array: 2 docs * 8 bytes each = 16 bytes
    // Each doc: 2 (url_len) + 24 (url bytes) + 2 (tld_len) + 3 (tld bytes) + 63 bytes = 94 bytes
    // Total expected: 4 + 16 + 94 + 94 = 208 bytes
    TEST_ASSERT(st.st_size == 208,
                "Document table size should include the raw TLD and feature block");

    unlink(test_file);
    std::cout << "test_write_document_table PASSED.\n";
}

void test_write_dictionary_and_finish() {
    std::cout << "Running test_write_dictionary_and_finish...\n";
    const char *test_file = "test_chunk_0004.idx";
    unlink(test_file);

    DiskChunkWriter writer(test_file);

    // write dummy header (will be overwritten later)
    FileHeader header;
    writer.writeHeader(header);

    // Bucket 0 is empty. Bucket 1 has one term.
    ::vector<::vector<DiskChunkWriter::DictionaryEntry>> buckets(2);

    BucketDisk cat_disk_info;
    cat_disk_info.occupied = 1;
    cat_disk_info.posting_offset = 1024;
    cat_disk_info.doc_frequency = 5;
    cat_disk_info.collection_frequency = 10;

    buckets[1].push_back({"cat", cat_disk_info});

    // write dictionary
    uint64_t dict_offset = writer.writeDictionary(buckets);
    TEST_ASSERT(dict_offset == sizeof(FileHeader), "Dict should start right after header");

    // finish the file (overwrite header)
    header.dict_offset = dict_offset;
    header.num_unique_terms = 1;
    writer.finish(header);

    int fd = open(test_file, O_RDONLY);
    FileHeader read_header;
    read(fd, &read_header, sizeof(FileHeader));
    TEST_ASSERT(read_header.dict_offset == sizeof(FileHeader), "Header should be updated");

    lseek(fd, read_header.dict_offset, SEEK_SET);
    size_t num_buckets;
    read(fd, &num_buckets, sizeof(size_t));
    TEST_ASSERT(num_buckets == 2, "Should have 2 buckets");

    size_t bucket_offsets[2];
    read(fd, bucket_offsets, 2 * sizeof(size_t));
    TEST_ASSERT(bucket_offsets[0] == 0, "Bucket 0 should be empty (offset 0)");
    TEST_ASSERT(bucket_offsets[1] > 0, "Bucket 1 should have an offset");

    close(fd);
    unlink(test_file);
    std::cout << "test_write_dictionary_and_finish PASSED.\n";
}

int main() {
    test_write_header();
    test_write_posting_list();
    test_write_document_table();
    test_write_dictionary_and_finish();

    std::cout << "All Chunk Writer tests passed!\n";
    return 0;
}
