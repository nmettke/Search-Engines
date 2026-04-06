// src/lib/isr.cpp
#include "isr.h"
#include "vbyte.h"

ISRWord::ISRWord() = default;

ISRWord::ISRWord(const uint8_t *data, uint32_t num_postings, std::optional<SeekTable> table)
    : num_postings(num_postings), data(data), current_ptr(data), seek_table(std::move(table)) {

    is_exhausted = (num_postings == 0);

    if (!is_exhausted) {
        next();
    }
}

uint32_t ISRWord::next() {
    if (done())
        return ISRSentinel;

    if (current_index >= num_postings) {
        is_exhausted = true;
        current_loc = ISRSentinel;
        return ISRSentinel;
    }

    uint32_t delta = VariableByteEncoder::decode(current_ptr);
    current_loc += delta;
    current_index += 1;
    return current_loc;
}

uint32_t ISRWord::seek(uint32_t target) {
    if (is_exhausted)
        return ISRSentinel;

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