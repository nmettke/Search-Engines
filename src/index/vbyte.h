// This header includes code needed for variable byte encoding
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

class VariableByteEncoder {
  public:
    static size_t encode(uint32_t value, uint8_t *out); // returns total writte byte
    static uint32_t decode(const uint8_t *&ptr);        // returns decoded value
    static std::vector<uint8_t> encodeDeltaList(const std::vector<uint32_t> &locations);
    static std::vector<uint32_t> decodeDeltaList(const uint8_t *data, size_t count);
};