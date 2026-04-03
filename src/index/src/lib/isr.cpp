// src/lib/isr.cpp
#include "isr.h"
#include "vbyte.h"

ISR::ISR() = default;

ISR::ISR(const uint8_t *data, uint32_t num_postings, std::optional<SeekTable> table)
    : num_postings(num_postings), data(data), current_ptr(data), seek_table(std::move(table)) {}

uint32_t ISR::next() {
    if (done())
        return ISRSentinel;
    uint32_t delta = VariableByteEncoder::decode(current_ptr);
    current_loc += delta;
    current_index += 1;
    return current_loc;
}

uint32_t ISR::seek(uint32_t target) {
    if (current_loc >= target && current_index > 0) {
        return current_loc;
    }

    if (seek_table.has_value()) {
        auto entry = seek_table->entryForLocation(target);

        if (entry.posting_index != SeekTable::NoPosting &&
            entry.posting_index + 1 > current_index) {
            current_ptr = data + entry.byte_offset;
            current_loc = entry.base_location;

            current_index = entry.posting_index + 1;

            if (current_loc >= target) {
                return current_loc;
            }
        }
    }

    uint32_t loc;
    while (!done()) {
        loc = next();
        if (loc >= target)
            return loc;
    }
    return ISRSentinel;
}