// tests/test_vbyte.cpp
#include "../src/lib/vbyte.h"
#include <iostream>
#include <limits>
#include <vector>

#define TEST_ASSERT(condition, message)                                                            \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            std::cerr << "FAIL: " << message << " (Line: " << __LINE__ << ")\n";                   \
            exit(1);                                                                               \
        }                                                                                          \
    } while (false)

void test_single_values() {
    std::cout << "Running test_single_values...\n";
    uint8_t buffer[5];

    // test small value (1 byte)
    size_t written = VariableByteEncoder::encode(5, buffer);
    TEST_ASSERT(written == 1, "Value 5 should take 1 byte");
    const uint8_t *ptr = buffer;
    TEST_ASSERT(VariableByteEncoder::decode(ptr) == 5, "Should decode back to 5");

    // test 2-byte value
    written = VariableByteEncoder::encode(300, buffer);
    TEST_ASSERT(written == 2, "Value 300 should take 2 bytes");
    ptr = buffer;
    TEST_ASSERT(VariableByteEncoder::decode(ptr) == 300, "Should decode back to 300");

    // test Maximum 32-bit value
    uint32_t max_val = std::numeric_limits<uint32_t>::max();
    written = VariableByteEncoder::encode(max_val, buffer);
    TEST_ASSERT(written == 5, "Max uint32 should take 5 bytes");
    ptr = buffer;
    TEST_ASSERT(VariableByteEncoder::decode(ptr) == max_val, "Should decode max value perfectly");

    std::cout << "test_single_values PASSED.\n";
}

void test_delta_lists() {
    std::cout << "Running test_delta_lists...\n";

    // test typical locations
    std::vector<uint32_t> locations = {4, 15, 18, 302};
    std::vector<uint8_t> compressed = VariableByteEncoder::encodeDeltaList(locations);

    // deltas are 4, 11, 3, 284. All fit in 1 byte except 284 (2 bytes). Total = 5 bytes.
    TEST_ASSERT(compressed.size() == 5, "List should compress to 5 bytes");

    std::vector<uint32_t> decoded = VariableByteEncoder::decodeDeltaList(compressed.data(), 4);
    TEST_ASSERT(decoded.size() == 4, "Should decode 4 items");
    TEST_ASSERT(decoded[0] == 4 && decoded[1] == 15 && decoded[2] == 18 && decoded[3] == 302,
                "Decoded list should match exactly");

    // test large gaps
    std::vector<uint32_t> gap_locations = {0, 1000000};
    std::vector<uint8_t> gap_compressed = VariableByteEncoder::encodeDeltaList(gap_locations);
    std::vector<uint32_t> gap_decoded =
        VariableByteEncoder::decodeDeltaList(gap_compressed.data(), 2);
    TEST_ASSERT(gap_decoded[1] == 1000000, "Should handle massive deltas perfectly");

    std::cout << "test_delta_lists PASSED.\n";
}

int main() {
    test_single_values();
    test_delta_lists();
    std::cout << "All VByte tests passed!\n";
    return 0;
}