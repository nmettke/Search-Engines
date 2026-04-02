// index/tests/test_integration.cpp
#include "../src/lib/Common.h" // For hashString
#include "../src/lib/disk_chunk_writer.h"
#include "../src/lib/in_memory_index.h"
#include <iostream>
#include <sys/stat.h>
#include <unistd.h>

void flushIndexChunk(const InMemoryIndex &mem_index, const std::string &filename);

#define TEST_ASSERT(condition, message)                                                            \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            std::cerr << "FAIL: " << message << " (Line: " << __LINE__ << ")\n";                   \
            exit(1);                                                                               \
        }                                                                                          \
    } while (false)

void test_end_to_end_flush() {
    std::cout << "Running test_end_to_end_flush...\n";
    const char *test_file = "test_chunk_final.idx";
    unlink(test_file);

    InMemoryIndex mem_index;

    // doc 1: "the cat sat"
    mem_index.addToken({"the", 0});
    mem_index.addToken({"cat", 1});
    mem_index.addToken({"sat", 2});
    mem_index.finishDocument({3, "http://cats.com", 3, 0, 0});

    // doc 2: "a dog run"
    mem_index.addToken({"a", 4});
    mem_index.addToken({"dog", 5});
    mem_index.addToken({"run", 6});
    mem_index.finishDocument({7, "http://dogs.com", 3, 0, 4});

    // flush to disk
    flushIndexChunk(mem_index, test_file);

    // verify file was created and has a reasonable size
    struct stat st;
    TEST_ASSERT(stat(test_file, &st) == 0, "Chunk file should be created");
    TEST_ASSERT(st.st_size > 100, "Chunk file should contain header, lists, docs, and dict");

    std::cout << "Successfully flushed chunk! Size: " << st.st_size << " bytes.\n";
    unlink(test_file);
}

int main() {
    test_end_to_end_flush();
    std::cout << "Integration test passed!\n";
    return 0;
}
