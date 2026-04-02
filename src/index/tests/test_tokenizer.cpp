// tests/test_tokenizer.cpp
#include <iostream>
#include <vector>
#include "../src/lib/tokenizer.h"
#include "../src/lib/types.h"

#define TEST_ASSERT(condition, message) \
    do { \
        if (!(condition)) { \
            std::cerr << "FAIL: " << message << " (Line: " << __LINE__ << ")\n"; \
            exit(1); \
        } \
    } while (false)

void test_normalization() {
    std::cout << "Running test_normalization...\n";
    Tokenizer tokenizer;

    HtmlParser mock_doc;
    mock_doc.base = "http://test.com";
    mock_doc.words = {"HELLO", "world!", "!@#", "real-time", "universities"};
    
    TokenizedDocument out = tokenizer.processDocument(mock_doc);

    // Expected logic based on your tokenizer.cpp:
    // 1. "HELLO" -> "hello"
    // 2. "world!" -> "world"
    // 3. "!@#" -> (discarded)
    // 4. "real-time" -> "real-time", "real", "time"
    // 5. "universities" -> "universities" (TODO: change to "univers" when stemmer is ready)

    TEST_ASSERT(out.tokens.size() == 6, "Should emit exactly 6 valid tokens");

    TEST_ASSERT(out.tokens[0].term == "hello", "Should lowercase");
    TEST_ASSERT(out.tokens[1].term == "world", "Should strip trailing punctuation");
    
    // hyphen handling
    TEST_ASSERT(out.tokens[2].term == "real-time", "Should keep full hyphenated word");
    TEST_ASSERT(out.tokens[3].term == "real", "Should split hyphen left");
    TEST_ASSERT(out.tokens[4].term == "time", "Should split hyphen right");
    
    // check that all parts of the hyphenated word share the same location
    TEST_ASSERT(out.tokens[2].location == out.tokens[3].location, "Hyphen parts share location");
    TEST_ASSERT(out.tokens[3].location == out.tokens[4].location, "Hyphen parts share location");

    // TODO: update this assertion when your teammate finishes the Porter Stemmer!
    TEST_ASSERT(out.tokens[5].term == "universities", "Currently unstemmed. Update to 'univers' later.");

    std::cout << "test_normalization PASSED.\n";
}

void test_global_location_counter() {
    std::cout << "Running test_global_location_counter...\n";
    Tokenizer tokenizer;

    HtmlParser doc1;
    doc1.base = "doc1";
    doc1.words = {"A", "B"};

    HtmlParser doc2;
    doc2.base = "doc2";
    doc2.words = {"C"};

    TokenizedDocument out1 = tokenizer.processDocument(doc1);
    TokenizedDocument out2 = tokenizer.processDocument(doc2);

    TEST_ASSERT(out1.tokens[0].location == 0, "A is at 0");
    TEST_ASSERT(out1.tokens[1].location == 1, "B is at 1");
    TEST_ASSERT(out1.doc_end.location == 2, "Doc 1 End is at 2");
    
    TEST_ASSERT(out2.tokens[0].location == 3, "C is at 3");
    TEST_ASSERT(out2.doc_end.location == 4, "Doc 2 End is at 4");

    std::cout << "test_global_location_counter PASSED.\n";
}

int main() {
    test_normalization();
    test_global_location_counter();
    std::cout << "All Tokenizer tests passed!\n";
    return 0;
}