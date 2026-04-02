#include "../tokenizer.h"
#include <chrono>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

struct TestCase {
    std::string input;
    std::string expected;
};

void run_test_and_benchmark() {
    std::vector<TestCase> tests = {{"caresses", "caress"},
                                   {"ponies", "poni"},
                                   {"ties", "ti"},
                                   {"caress", "caress"},
                                   {"cats", "cat"},
                                   {"feed", "feed"},
                                   {"agreed", "agree"},
                                   {"disabled", "disabl"},
                                   {"matting", "mat"},
                                   {"mating", "mate"},
                                   {"meeting", "meet"},
                                   {"milling", "mill"},
                                   {"messing", "mess"},
                                   {"meetings", "meet"},
                                   {"conflated", "conflate"},
                                   {"troubled", "trouble"},
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
                                   {"relational", "relate"},
                                   {"conditional", "condit"},
                                   {"rational", "ration"},
                                   {"valency", "valence"},
                                   {"hesitancy", "hesitance"},
                                   {"digitizer", "digitize"},
                                   {"conformability", "conform"},
                                   {"radically", "radic"},
                                   {"differentli", "differ"},
                                   {"vileli", "vile"},
                                   {"analogousli", "analogous"},
                                   {"vietnamization", "vietnamize"},
                                   {"predication", "predicate"}};

    std::cout << std::left << std::setw(20) << "Input" << std::setw(20) << "Expected"
              << std::setw(20) << "Actual"
              << "Result" << std::endl;
    std::cout << std::string(70, '-') << std::endl;

    int passed = 0;
    auto start = std::chrono::high_resolution_clock::now();

    // Loop through tests
    for (const auto &t : tests) {
        std::string result = PorterStemmer::stem(t.input);
        bool match = (result == t.expected);
        if (match)
            passed++;

        std::cout << std::left << std::setw(20) << t.input << std::setw(20) << t.expected
                  << std::setw(20) << result << (match ? "PASS" : "FAIL") << std::endl;
    }

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::micro> elapsed = end - start;

    std::cout << "\n" << std::string(70, '-') << std::endl;
    std::cout << "Summary: " << passed << "/" << tests.size() << " passed." << std::endl;
    std::cout << "Total time for batch: " << elapsed.count() << " microseconds" << std::endl;
    std::cout << "Average time per word: " << (elapsed.count() / tests.size()) << " microseconds"
              << std::endl;
}

int main() {
    run_test_and_benchmark();
    return 0;
}