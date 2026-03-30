#include "isr.h"
#include "vbyte.h"

ISR::ISR(const uint8_t *data, uint32_t num_postings)
    : data(data), current_ptr(data), num_postings(num_postings) {}

uint32_t ISR::next() {
    if (done())
        return ISRSentinel;
    uint32_t delta = VariableByteEncoder::decode(current_ptr);
    current_loc += delta;
    current_index += 1;
    return current_loc;
}

uint32_t ISR::seek(uint32_t target) {
    uint32_t loc;
    while (!done()) {
        loc = next();
        if (loc >= target)
            return loc;
    }
    return ISRSentinel;
}