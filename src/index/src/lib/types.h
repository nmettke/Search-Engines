// src/lib/types.h
#pragma once

#include "utils/string.hpp"
#include "utils/vector.hpp"
#include <cstdint>
#include <limits>

constexpr uint32_t magic = 0x49445831; // magic for writing chunk
constexpr uint32_t version = 1;        // chunk file encoding version
constexpr uint32_t ISRSentinel = std::numeric_limits<uint32_t>::max();
constexpr const char *docEndToken = "#DocEnd";

enum DocumentFeatureFlag : uint8_t {
    kFeaturesPresent = 1u << 0,
    kHttps = 1u << 1,
    kSawBodyTag = 1u << 2,
    kSawCloseHtmlTag = 1u << 3,
    kHtmlTruncated = 1u << 4,
    kHasOpenDiscardSection = 1u << 5,
};

struct DocumentFeatures {
    uint8_t flags = 0;
    uint8_t base_domain_length = 0;
    uint16_t url_length = 0;
    uint16_t path_length = 0;
    uint8_t path_depth = 0;
    uint8_t query_param_count = 0;
    uint8_t numeric_path_char_count = 0;
    uint8_t domain_hyphen_count = 0;
    uint16_t outgoing_link_count = 0;
    uint16_t outgoing_anchor_word_count = 0;
    ::string raw_tld;
};

struct __attribute__((packed)) DocumentFeaturesDisk {
    uint8_t flags = 0;
    uint8_t base_domain_length = 0;
    uint16_t url_length = 0;
    uint16_t path_length = 0;
    uint8_t path_depth = 0;
    uint8_t query_param_count = 0;
    uint8_t numeric_path_char_count = 0;
    uint8_t domain_hyphen_count = 0;
    uint16_t outgoing_link_count = 0;
    uint16_t outgoing_anchor_word_count = 0;
};

struct TokenOutput {
    ::string term;
    uint32_t location;
};

struct DocEndOutput {
    uint32_t location;
    ::string url;
    uint32_t word_count;
    uint16_t title_word_count;
    uint32_t doc_start_loc;
    uint8_t seed_distance;
    DocumentFeatures features;
};

struct PostingList {
    ::vector<uint32_t> locations;
    uint32_t doc_frequency = 0;
    uint64_t collection_frequency = 0;
};

struct DocumentRecord {
    ::string url;
    uint32_t start_location = 0;
    uint32_t end_location = 0;
    uint32_t word_count = 0;
    uint16_t title_word_count = 0;
    uint8_t seed_distance = 0;
    DocumentFeatures features;
};

struct FileHeader {
    uint32_t magic = ::magic;
    uint32_t version = ::version;
    uint32_t num_documents = 0;
    uint64_t total_locations = 0;
    uint32_t num_unique_terms = 0;
    uint64_t dict_offset = 0;
    uint64_t postings_offset = 0;
    uint64_t doctable_offset = 0;
    uint64_t global_loc_base = 0;
};

struct BucketDisk {
    uint8_t occupied = 0;
    uint32_t string_offset = 0;
    uint16_t string_length = 0;
    uint64_t posting_offset = 0;
    uint32_t doc_frequency = 0;
    uint64_t collection_frequency = 0;
};

struct PostingListHeader {
    uint32_t num_postings = 0;
    uint8_t has_seek_table = 0;
    uint32_t data_size = 0;
};

struct DictionaryHeader {
    uint32_t table_size = 0;
    uint32_t string_pool_size = 0;
};

// Represents the fixed-size metadata of a document on disk.
// The URL length/string and raw-TLD length/string immediately precede this in the file.
struct __attribute__((packed)) DocumentRecordDisk {
    uint32_t start_location = 0;
    uint32_t end_location = 0;
    uint32_t word_count = 0;
    uint16_t title_word_count = 0;
    uint8_t seed_distance = 0;
    DocumentFeaturesDisk features;
};
