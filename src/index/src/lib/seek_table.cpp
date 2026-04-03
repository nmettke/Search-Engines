#include "seek_table.h"
#include "vbyte.h"
#include <cstring>
#include <stdexcept>

SeekTable::SeekTable() : _entries{} {}

bool SeekTable::shouldBuild(uint32_t num_postings) { return num_postings > BuildThreshold; }

uint8_t SeekTable::extractIndex(uint32_t location) {
    return static_cast<uint8_t>(location >> (32 - IndexBits));
}

uint8_t SeekTable::indexFor(uint32_t location) const { return extractIndex(location); }

const SeekTableEntry &SeekTable::entryForIndex(uint8_t index) const { return _entries[index]; }

const SeekTableEntry &SeekTable::entryForLocation(uint32_t location) const {
    return entryForIndex(indexFor(location));
}

const std::array<SeekTableEntry, SeekTable::EntryCount> &SeekTable::entries() const {
    return _entries;
}

SeekTable SeekTable::build(const uint8_t *encoded_deltas, size_t data_size, uint32_t num_postings) {
    SeekTable table;

    if (num_postings == 0 || data_size == 0) {
        return table;
    }

    if (encoded_deltas == nullptr) {
        throw std::invalid_argument("SeekTable::build requires a valid encoded stream");
    }

    std::array<bool, SeekTable::EntryCount> initialized{};
    const uint8_t *curr = encoded_deltas;
    const uint8_t *end = encoded_deltas + data_size;
    uint32_t current_location = 0;
    uint32_t delta;
    uint32_t byte_offset;
    uint8_t index;

    for (uint32_t posting_idx = 0; posting_idx < num_postings; ++posting_idx) {
        if (curr >= end) {
            throw std::runtime_error("SeekTable::build encountered truncated posting data");
        }

        // decode returns the delta and moves the ptr
        delta = VariableByteEncoder::decode(curr);
        current_location += delta;
        byte_offset = static_cast<uint32_t>(curr - encoded_deltas);

        index = extractIndex(current_location);

        if (!initialized[index]) {
            table._entries[index] = SeekTableEntry{byte_offset, current_location, posting_idx};
            initialized[index] = true;
        }
    }

    SeekTableEntry prev{};
    bool has_prev = false;
    // forward fill table index of those not initialized (closest known is prev)
    for (size_t i = 0; i < table._entries.size(); ++i) {
        if (initialized[i]) {
            prev = table._entries[i];
            has_prev = true;
            continue;
        }

        if (has_prev) {
            table._entries[i] = prev;
        }
    }

    return table;
}

SeekTable SeekTable::build(const std::vector<uint8_t> &encoded_deltas, uint32_t num_postings) {
    return build(encoded_deltas.data(), encoded_deltas.size(), num_postings);
}

std::vector<uint8_t> SeekTable::serialize() const {
    // create a vector of bytes for chunk writer
    std::vector<uint8_t> bytes(SerializedSize);
    uint8_t *ptr = bytes.data();
    for (const auto &entry : _entries) {
        std::memcpy(ptr, &entry.byte_offset, sizeof(entry.byte_offset));
        ptr += sizeof(entry.byte_offset);

        std::memcpy(ptr, &entry.base_location, sizeof(entry.base_location));
        ptr += sizeof(entry.base_location);
    }
    return bytes;
}

SeekTable SeekTable::deserialize(const uint8_t *data, const uint8_t *encoded_deltas,
                                 size_t data_size, uint32_t num_postings, size_t serialized_size) {
    if (data == nullptr) {
        throw std::invalid_argument("SeekTable::deserialize requires a valid byte buffer");
    }
    if (serialized_size != SerializedSize) {
        throw std::invalid_argument("SeekTable::deserialize received wrong number of bytes");
    }

    // populate seektable from serialized chunk
    SeekTable table;
    const uint8_t *ptr = data;
    for (auto &entry : table._entries) {
        std::memcpy(&entry.byte_offset, ptr, sizeof(entry.byte_offset));
        ptr += sizeof(entry.byte_offset);

        std::memcpy(&entry.base_location, ptr, sizeof(entry.base_location));
        ptr += sizeof(entry.base_location);

        entry.posting_index = NoPosting;
    }

    // recalculate the posting indices
    table.addPostingIndexes(encoded_deltas, data_size, num_postings);
    return table;
}

void SeekTable::addPostingIndexes(const uint8_t *encoded_deltas, size_t data_size,
                                  uint32_t num_postings) {
    if (encoded_deltas == nullptr || data_size == 0 || num_postings == 0) {
        return;
    }

    const uint8_t *curr = encoded_deltas;
    const uint8_t *end = encoded_deltas + data_size;
    uint32_t current_location = 0;
    size_t entry_idx = 0;
    uint32_t delta;
    uint32_t byte_offset;

    for (uint32_t posting_idx = 0; posting_idx < num_postings && entry_idx < _entries.size();
         ++posting_idx) {
        if (curr >= end) {
            throw std::runtime_error("SeekTable::addPostingIndexes encountered truncated data");
        }

        delta = VariableByteEncoder::decode(curr);
        current_location += delta;
        byte_offset = static_cast<uint32_t>(curr - encoded_deltas);

        while (entry_idx < _entries.size() && _entries[entry_idx].byte_offset == byte_offset &&
               _entries[entry_idx].base_location == current_location) {
            // Various bucket might have same value because forward fill
            _entries[entry_idx].posting_index = posting_idx;
            ++entry_idx;
        }
    }
}
