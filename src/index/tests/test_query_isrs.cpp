#include "../src/lib/Common.h"
#include "../src/lib/chunk_flusher.h"
#include "../src/lib/disk_chunk_reader.h"
#include "../src/lib/in_memory_index.h"
#include "../src/lib/isr_and.h"
#include "../src/lib/isr_container.h"
#include "../src/lib/isr_or.h"
#include "../src/lib/isr_phrase.h"
#include <iostream>
#include <unistd.h>

#define TEST_ASSERT(condition, message)                                                            \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            std::cerr << "FAIL: " << message << " (Line: " << __LINE__ << ")\n";                   \
            exit(1);                                                                               \
        }                                                                                          \
    } while (false)

const char *TEST_FILE = "test_isr_tree.idx";

void setup_test_index() {
    unlink(TEST_FILE);
    InMemoryIndex mem_index;

    // Doc 0: "cat dog bird"
    mem_index.addToken({"cat", 0});
    mem_index.addToken({"dog", 1});
    mem_index.addToken({"bird", 2});
    mem_index.finishDocument({3, "doc0", 3, 0, 0});

    // Doc 1: "cat fish bird"
    mem_index.addToken({"cat", 4});
    mem_index.addToken({"fish", 5});
    mem_index.addToken({"bird", 6});
    mem_index.finishDocument({7, "doc1", 3, 0, 4});

    // Doc 2: "dog bird"
    mem_index.addToken({"dog", 8});
    mem_index.addToken({"bird", 9});
    mem_index.finishDocument({10, "doc2", 2, 0, 8});

    // Doc 3: "cat dog fish"
    mem_index.addToken({"cat", 11});
    mem_index.addToken({"dog", 12});
    mem_index.addToken({"fish", 13});
    mem_index.finishDocument({14, "doc3", 3, 0, 11});

    flushIndexChunk(mem_index, TEST_FILE);
}

void test_isr_and(const DiskChunkReader &reader) {
    std::cout << "Running test_isr_and...\n";
    // Query: cat AND bird (Should match Doc 0 and Doc 1)
    std::vector<std::unique_ptr<ISR>> terms;
    terms.push_back(reader.createISR("cat"));
    terms.push_back(reader.createISR("bird"));

    ISRAnd isr_and(std::move(terms), reader.createISR(docEndToken), reader);

    TEST_ASSERT(!isr_and.done(), "AND should have matches");
    TEST_ASSERT(isr_and.currentDocument()->url == "doc0", "First match is doc0");

    isr_and.next();
    TEST_ASSERT(!isr_and.done(), "Should have second match");
    TEST_ASSERT(isr_and.currentDocument()->url == "doc1", "Second match is doc1");

    isr_and.next();
    TEST_ASSERT(isr_and.done(), "AND should be exhausted");
    std::cout << "test_isr_and PASSED.\n";
}

void test_isr_or(const DiskChunkReader &reader) {
    std::cout << "Running test_isr_or...\n";
    // Query: fish OR dog (Locations: 1, 5, 8, 12, 13)
    std::vector<std::unique_ptr<ISR>> terms;
    terms.push_back(reader.createISR("fish")); // 5, 13
    terms.push_back(reader.createISR("dog"));  // 1, 8, 12

    ISROr isr_or(std::move(terms));

    TEST_ASSERT(isr_or.currentLocation() == 1, "First OR match is 1");
    TEST_ASSERT(isr_or.next() == 5, "Second OR match is 5");
    TEST_ASSERT(isr_or.next() == 8, "Third OR match is 8");
    TEST_ASSERT(isr_or.next() == 12, "Fourth OR match is 12");
    TEST_ASSERT(isr_or.next() == 13, "Fifth OR match is 13");

    isr_or.next();
    TEST_ASSERT(isr_or.done(), "OR should be exhausted");
    std::cout << "test_isr_or PASSED.\n";
}

void test_isr_phrase(const DiskChunkReader &reader) {
    std::cout << "Running test_isr_phrase...\n";
    // Query: "cat dog" (Should match Doc 0 and Doc 3)
    std::vector<std::unique_ptr<ISR>> terms;
    terms.push_back(reader.createISR("cat")); // 0, 4, 11
    terms.push_back(reader.createISR("dog")); // 1, 8, 12

    ISRPhrase isr_phrase(std::move(terms));

    // Phrase returns the END of the match
    TEST_ASSERT(!isr_phrase.done(), "Phrase should match");
    TEST_ASSERT(isr_phrase.currentLocation() == 1, "First match ends at loc 1 (Doc 0)");

    isr_phrase.next();
    TEST_ASSERT(!isr_phrase.done(), "Should have second match");
    TEST_ASSERT(isr_phrase.currentLocation() == 12, "Second match ends at loc 12 (Doc 3)");

    isr_phrase.next();
    TEST_ASSERT(isr_phrase.done(), "Phrase should be exhausted");
    std::cout << "test_isr_phrase PASSED.\n";
}

void test_isr_container(const DiskChunkReader &reader) {
    std::cout << "Running test_isr_container (NOT)...\n";
    // Query: bird NOT fish
    // bird is in Docs: 0, 1, 2.  fish is in Docs: 1, 3.
    // Expected Matches: Doc 0, Doc 2.

    auto pos = reader.createISR("bird");
    std::vector<std::unique_ptr<ISR>> negs;
    negs.push_back(reader.createISR("fish"));

    ISRContainer isr_container(std::move(pos), std::move(negs), reader.createISR(docEndToken),
                               reader);

    TEST_ASSERT(!isr_container.done(), "Container should have matches");
    TEST_ASSERT(isr_container.currentLocation() == 2, "First match is bird at loc 2 (Doc 0)");

    isr_container.next();
    // Doc 1 has bird at 6, but is excluded by fish at 5!
    TEST_ASSERT(!isr_container.done(), "Container should have second match");
    TEST_ASSERT(isr_container.currentLocation() == 9, "Second match is bird at loc 9 (Doc 2)");

    isr_container.next();
    TEST_ASSERT(isr_container.done(), "Container should be exhausted");
    std::cout << "test_isr_container PASSED.\n";
}

int main() {
    setup_test_index();

    DiskChunkReader reader;
    TEST_ASSERT(reader.open(TEST_FILE), "Should open test chunk");

    test_isr_and(reader);
    test_isr_or(reader);
    test_isr_phrase(reader);
    test_isr_container(reader);

    std::cout << "All Constraint Solver ISR tests passed!\n";
    unlink(TEST_FILE);
    return 0;
}