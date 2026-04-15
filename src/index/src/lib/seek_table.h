// Header file for building the seek table
#pragma once

#include "utils/vector.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

struct SeekTableEntry {
    uint32_t byte_offset = 0;
    uint32_t base_location = 0;
    uint32_t posting_index = std::numeric_limits<uint32_t>::max();
};

class SeekTable {
  public:
    static constexpr uint32_t IndexBits = 16;                // use top 16 bits for index
    static constexpr uint32_t EntryCount = 1u << IndexBits; // use top 16 bits for index
    static constexpr uint32_t BuildThreshold = 512;
    static constexpr uint32_t NoPosting = std::numeric_limits<uint32_t>::max();
    static constexpr size_t SerializedEntrySize = sizeof(uint32_t) * 2;
    static constexpr size_t SerializedSize = EntryCount * SerializedEntrySize;

    SeekTable();

    static bool shouldBuild(uint32_t num_postings);
    static SeekTable build(const uint8_t *encoded_deltas, size_t data_size, uint32_t num_postings);
    static SeekTable build(const ::vector<uint8_t> &encoded_deltas, uint32_t num_postings);
    ::vector<uint8_t> serialize() const;
    static SeekTable deserialize(const uint8_t *data, const uint8_t *encoded_deltas,
                                 size_t data_size, uint32_t num_postings,
                                 size_t serialized_size = SerializedSize);

    uint8_t indexFor(uint32_t location) const;
    const SeekTableEntry &entryForIndex(uint8_t index) const;
    const SeekTableEntry &entryForLocation(uint32_t location) const;
    const std::array<SeekTableEntry, EntryCount> &entries() const;

  private:
    static uint8_t extractIndex(uint32_t location);
    void addPostingIndexes(const uint8_t *encoded_deltas, size_t data_size, uint32_t num_postings);

    std::array<SeekTableEntry, EntryCount> _entries;
};
