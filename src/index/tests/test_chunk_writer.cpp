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
    std::vector<uint32_t> dummy_locations = {4, 15, 18, 302};

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

    std::vector<DocumentRecord> dummy_docs;
    dummy_docs.push_back({"https://example.com/cats", 0, 3, 3, 1});
    dummy_docs.push_back({"https://example.com/dogs", 4, 7, 3, 1});

    uint64_t offset = writer.writeDocumentTable(dummy_docs);
    TEST_ASSERT(offset == 0, "Document table should start at offset 0 in this isolated test");

    struct stat st;
    TEST_ASSERT(stat(test_file, &st) == 0, "File should exist");

    // Header: 4 bytes (num_documents)
    // Offsets array: 2 docs * 8 bytes each = 16 bytes
    // Doc 1: 2 (url_len) + 24 (url bytes) + 4 (start) + 4 (end) + 4 (wc) + 2 (title_wc) = 40 bytes
    // Doc 2: 2 (url_len) + 24 (url bytes) + 4 (start) + 4 (end) + 4 (wc) + 2 (title_wc) = 40 bytes
    // Total expected: 4 + 16 + 40 + 40 = 100 bytes
    TEST_ASSERT(st.st_size == 100, "Document table size should be exactly 100 bytes");

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
    std::vector<std::vector<DiskChunkWriter::DictionaryEntry>> buckets(2);

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
