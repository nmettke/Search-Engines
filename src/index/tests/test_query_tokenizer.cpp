#include "../src/lib/query_tokenizer.h"
#include <iostream>
#include <vector>

#define TEST_ASSERT(condition, message)                                                            \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            std::cerr << "FAIL: " << message << " (Line: " << __LINE__ << ")\n";                   \
            exit(1);                                                                               \
        }                                                                                          \
    } while (false)

void test_simple_words() {
    std::cout << "Running test_simple_words...\n";
    auto tokens = QueryTokenizer::tokenize("hello world");

    TEST_ASSERT(tokens.size() == 2, "Should have 2 tokens");
    TEST_ASSERT(tokens[0].type == QueryTokenType::WORD, "First token is WORD");
    TEST_ASSERT(tokens[0].text == "hello", "First token text is hello");
    TEST_ASSERT(tokens[1].type == QueryTokenType::WORD, "Second token is WORD");

    std::cout << "test_simple_words PASSED.\n";
}

void test_complex_syntax() {
    std::cout << "Running test_complex_syntax...\n";
    // Query: "cat dog" | -bird
    // We expect: QUOTE, WORD(cat), WORD(dog), QUOTE, OR, NOT, WORD(bird)
    auto tokens = QueryTokenizer::tokenize("\"cat dogs\" | -birds");

    TEST_ASSERT(tokens.size() == 7, "Should perfectly isolate 7 tokens");

    TEST_ASSERT(tokens[0].type == QueryTokenType::QUOTE, "Token 0 is QUOTE");
    TEST_ASSERT(tokens[1].type == QueryTokenType::WORD && tokens[1].text == "cat",
                "Token 1 is cat");
    TEST_ASSERT(tokens[2].type == QueryTokenType::WORD && tokens[2].text == "dog",
                "Token 2 is dog");
    TEST_ASSERT(tokens[3].type == QueryTokenType::QUOTE, "Token 3 is QUOTE");

    TEST_ASSERT(tokens[4].type == QueryTokenType::OR, "Token 4 is OR");
    TEST_ASSERT(tokens[5].type == QueryTokenType::NOT, "Token 5 is NOT");

    TEST_ASSERT(tokens[6].type == QueryTokenType::WORD && tokens[6].text == "bird",
                "Token 6 is bird");

    std::cout << "test_complex_syntax PASSED.\n";
}

void test_text_operators() {
    std::cout << "Running test_text_operators...\n";
    auto tokens = QueryTokenizer::tokenize("cat OR dog AND NOT fish");

    TEST_ASSERT(tokens.size() == 6, "Should extract text operators");
    TEST_ASSERT(tokens[1].type == QueryTokenType::OR, "OR text matches");
    TEST_ASSERT(tokens[3].type == QueryTokenType::AND, "AND text matches");
    TEST_ASSERT(tokens[4].type == QueryTokenType::NOT, "NOT text matches");

    std::cout << "test_text_operators PASSED.\n";
}

int main() {
    test_simple_words();
    test_complex_syntax();
    test_text_operators();
    std::cout << "All Query Tokenizer tests passed!\n";
    return 0;
}