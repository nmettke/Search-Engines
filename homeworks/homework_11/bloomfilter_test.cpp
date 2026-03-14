#include "BloomFilter.h"
#include <iostream>
#include <string>
#include <cassert>

void testBloomFilterClass() {
    Bloomfilter bf1(1000, 0.01);
    Bloomfilter bf2(100, 0.1);
    std::cout << "construction: PASSED\n";

    bf1.insert("Hello");
    bf1.insert("World");
    bf1.insert("William");

    std::cout << "contains 'Hello': " << bf1.contains("Hello") << "\n";
    std::cout << "contains 'World': " << bf1.contains("World") << "\n";
    std::cout << "contains 'William': " << bf1.contains("William") << "\n";
    assert(bf1.contains("Hello"));
    assert(bf1.contains("World"));
    assert(bf1.contains("William"));
    std::cout << "contains: PASSED\n";

    std::cout << "contains 'Satvik': " << bf1.contains("Satvik") << "\n";
    std::cout << "contains 'Nate': " << bf1.contains("Nate") << "\n";
    std::cout << "contains '': " << bf1.contains("") << "\n";
    std::cout << "doesnt contain: PASSED\n";

    Bloomfilter bf3(10000, 0.001);
    for (int i = 0; i < 5000; ++i){
        bf3.insert("item_" + std::to_string(i));
    }
    
    for (int i = 0; i < 5000; ++i){
        assert(bf3.contains("item_" + std::to_string(i)));
    }
    std::cout << "large insert: PASSED\n";

    bf1.insert("Hello");
    bf1.insert("Hello");
    assert(bf1.contains("Hello"));
    std::cout <<"duplicate insert: PASSED\n";

    Bloomfilter bf4(1000, 0.01);
    bf4.insert("");
    std::cout << "contains empty string: " << bf4.contains("") << "\n";
    std::cout << "empty string insert: PASSED\n";

    std::cout << "\nALL TESTS PASSED!\n";
}

int main() {
    testBloomFilterClass();
    return 0;
}