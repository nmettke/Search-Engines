// tests/test_tokenizer.cpp
#include "../src/lib/tokenizer.h"
#include "../src/lib/types.h"
#include <iostream>
#include <vector>

#define TEST_ASSERT(condition, message)                                                            \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            std::cerr << "FAIL: " << message << " (Line: " << __LINE__ << ")\n";                   \
            exit(1);                                                                               \
        }                                                                                          \
    } while (false)

struct TestCase {
    std::string input;
    std::string expected;
};

void test_stemmer() {
    std::vector<TestCase> tests = {{"caresses", "caress"},
                                   {"ponies", "poni"},
                                   {"ties", "ti"},
                                   {"caress", "caress"},
                                   {"cats", "cat"},
                                   {"feed", "feed"},
                                   {"agreed", "agre"},
                                   {"disabled", "disabl"},
                                   {"matting", "mat"},
                                   {"mating", "mate"},
                                   {"meeting", "meet"},
                                   {"milling", "mill"},
                                   {"messing", "mess"},
                                   {"meetings", "meet"},
                                   {"conflated", "conflat"},
                                   {"troubled", "troubl"},
                                   {"sized", "size"},
                                   {"hopping", "hop"},
                                   {"tanned", "tan"},
                                   {"falling", "fall"},
                                   {"hissing", "hiss"},
                                   {"fizzed", "fizz"},
                                   {"failing", "fail"},
                                   {"filing", "file"},
                                   {"happy", "happi"},
                                   {"sky", "sky"},
                                   {"relational", "relat"},
                                   {"conditional", "condit"},
                                   {"rational", "ration"},
                                   {"valency", "valenc"},
                                   {"hesitancy", "hesit"},
                                   {"digitizer", "digit"},
                                   {"conformability", "conform"},
                                   {"radically", "radic"},
                                   {"differentli", "differ"},
                                   {"vileli", "vile"},
                                   {"analogousli", "analog"},
                                   {"vietnamization", "vietnam"},
                                   {"predication", "predic"}};

    std::cout << "Running test_stemmer...\n";
    for (const auto &test : tests) {
        std::string actual = PorterStemmer::stem(test.input);
        TEST_ASSERT(actual == test.expected, "Stemming failed for input: " + test.input);
    }
    std::cout << "test_stemmer PASSED.\n";
}

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
    // 4. "real-time" -> "real-tim", "real", "time"
    // 5. "universities" -> "universities" (TODO: change to "univers" when stemmer is ready)

    TEST_ASSERT(out.tokens.size() == 6, "Should emit exactly 6 valid tokens");

    TEST_ASSERT(out.tokens[0].term == "hello", "Should lowercase");
    TEST_ASSERT(out.tokens[1].term == "world", "Should strip trailing punctuation");

    // hyphen handling
    TEST_ASSERT(out.tokens[2].term == "real-tim", "Should keep full hyphenated word");
    TEST_ASSERT(out.tokens[3].term == "real", "Should split hyphen left");
    TEST_ASSERT(out.tokens[4].term == "time", "Should split hyphen right");

    // check that all parts of the hyphenated word share the same location
    TEST_ASSERT(out.tokens[2].location == out.tokens[3].location, "Hyphen parts share location");
    TEST_ASSERT(out.tokens[3].location == out.tokens[4].location, "Hyphen parts share location");

    // check stemming
    TEST_ASSERT(out.tokens[5].term == "univers", "Should stem to univers");

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
    test_stemmer();
    test_normalization();
    test_global_location_counter();
    std::cout << "All Tokenizer tests passed!\n";
    return 0;
}