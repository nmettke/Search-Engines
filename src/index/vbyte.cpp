#include "vbyte.h"

size_t VariableByteEncoder::encode(uint32_t value, uint8_t *out) {
    size_t written = 0;
    while (true) {
        uint8_t byte = static_cast<uint8_t>(value & 0x7F);
        value >>= 7;

        // set high bit to 1
        if (value != 0)
            byte |= 0x80;
        out[written] = byte;
        written += 1;

        if (value == 0)
            break;
    }
    return written;
}

uint32_t VariableByteEncoder::decode(const uint8_t *&ptr) {
    uint32_t result = 0;
    uint32_t shift = 0;

    while (true) {
        uint8_t b = *ptr;
        result |= static_cast<uint32_t>(b & 0x7F) << shift;

        if ((b & 0x80) == 0)
            break;

        ptr += 1;
        shift += 7;
    }

    return result;
}

std::vector<uint8_t> VariableByteEncoder::encodeDeltaList(const std::vector<uint32_t> &locations) {
    std::vector<uint8_t> out;

    uint32_t prev = 0;
    uint32_t value;

    // each value is 32 bits, need at most 5 7 bit chunks to encode
    uint8_t buf[5];
    size_t n;

    for (size_t i = 0; i < locations.size(); ++i) {
        value = locations[i] - prev;
        n = encode(value, buf);
        out.insert(out.end(), buf, buf + n);
        prev = locations[i];
    }

    return out;
}

std::vector<uint32_t> VariableByteEncoder::decodeDeltaList(const uint8_t *data, size_t count) {
    std::vector<uint32_t> out;
    uint32_t prev = 0;
    uint32_t delta;

    for (size_t i = 0; i < count; ++i) {
        delta = decode(data);
        out.push_back(prev + delta);
        prev += delta;
        data += 1;
    }

    return out;
}