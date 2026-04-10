// src/lib/disk_chunk_reader.cpp
#include "disk_chunk_reader.h"
#include "Common.h"
#include "buffer_reader.h"
#include "types.h"
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

DiskChunkReader::DiskChunkReader() : fd_(-1), mapped_size_(0), data_(nullptr) {}

DiskChunkReader::~DiskChunkReader() {
    if (data_ != nullptr && data_ != MAP_FAILED) {
        munmap((void *)data_, mapped_size_);
    }
    if (fd_ >= 0) {
        close(fd_);
    }
}

bool DiskChunkReader::open(const ::string &filename) {
    fd_ = ::open(filename.c_str(), O_RDONLY);
    if (fd_ < 0)
        return false;

    // get file size
    struct stat st;
    if (fstat(fd_, &st) < 0)
        return false;
    mapped_size_ = st.st_size;

    // ensure it's at least large enough to hold a FileHeader
    if (mapped_size_ < sizeof(FileHeader))
        return false;

    void *mapped = mmap(nullptr, mapped_size_, PROT_READ, MAP_PRIVATE, fd_, 0);
    if (mapped == MAP_FAILED)
        return false;

    // cast to byte pointer for safe offset arithmetic later
    data_ = static_cast<const uint8_t *>(mapped);

    // validate the header
    BufferReader reader(data_);
    const FileHeader *file_header = reader.readPOD<FileHeader>();

    if (file_header->magic != ::magic) {
        std::cerr << "Invalid chunk file: Magic number mismatch.\n";
        return false;
    }
    if (file_header->version != ::version) {
        std::cerr << "Invalid chunk file: Version mismatch.\n";
        return false;
    }

    header_ = *file_header;

    return true;
}

std::unique_ptr<ISRWord> DiskChunkReader::createISR(const ::string &term) const {
    if (!data_)
        return nullptr;

    uint64_t hash_val = hashString(term.c_str());
    const uint8_t *dict_ptr = data_ + header_.dict_offset;
    size_t num_buckets = *reinterpret_cast<const size_t *>(dict_ptr);
    if (num_buckets == 0)
        return nullptr;

    const size_t *buckets_array = reinterpret_cast<const size_t *>(dict_ptr + sizeof(size_t));
    size_t chain_offset = buckets_array[hash_val % num_buckets];
    if (chain_offset == 0)
        return nullptr;

    BufferReader reader(dict_ptr + chain_offset);

    while (true) {
        const BucketDisk *b_disk = reader.readPOD<BucketDisk>();
        if (b_disk->occupied == 0)
            break;

        ::string_view current_term(reinterpret_cast<const char *>(reader.current()),
                                   b_disk->string_length);
        reader.skip(b_disk->string_length);

        if (::string(current_term) == term) {
            BufferReader p_reader(data_ + header_.postings_offset + b_disk->posting_offset);
            const PostingListHeader *p_header = p_reader.readPOD<PostingListHeader>();
            std::optional<SeekTable> table = std::nullopt;

            if (p_header->has_seek_table) {
                const uint8_t *table_data = p_reader.current();
                p_reader.skip(SeekTable::SerializedSize);
                const uint8_t *compressed_data = p_reader.current();
                table = SeekTable::deserialize(table_data, compressed_data, p_header->data_size,
                                               p_header->num_postings);

                return std::make_unique<ISRWord>(compressed_data, p_header->num_postings, table);
            } else {
                const uint8_t *compressed_data = p_reader.current();
                return std::make_unique<ISRWord>(compressed_data, p_header->num_postings);
            }
        }
    }

    return nullptr;
}

std::optional<DocumentRecord> DiskChunkReader::getDocument(uint32_t doc_id) const {
    if (!data_ || doc_id >= header_.num_documents) {
        return std::nullopt;
    }

    const uint8_t *doctable_ptr = data_ + header_.doctable_offset;

    const uint64_t *offsets_array =
        reinterpret_cast<const uint64_t *>(doctable_ptr + sizeof(uint32_t));

    uint64_t doc_offset = offsets_array[doc_id];

    BufferReader reader(doctable_ptr + doc_offset);

    ::string_view url_view = reader.readString16();
    ::string_view raw_tld_view = reader.readString16();
    const DocumentRecordDisk *disk_rec = reader.readPOD<DocumentRecordDisk>();

    DocumentRecord doc;
    doc.url = ::string(url_view);
    doc.start_location = disk_rec->start_location;
    doc.end_location = disk_rec->end_location;
    doc.word_count = disk_rec->word_count;
    doc.title_word_count = disk_rec->title_word_count;
    doc.seed_distance = disk_rec->seed_distance;
    doc.features.flags = disk_rec->features.flags;
    doc.features.base_domain_length = disk_rec->features.base_domain_length;
    doc.features.url_length = disk_rec->features.url_length;
    doc.features.path_length = disk_rec->features.path_length;
    doc.features.path_depth = disk_rec->features.path_depth;
    doc.features.query_param_count = disk_rec->features.query_param_count;
    doc.features.numeric_path_char_count = disk_rec->features.numeric_path_char_count;
    doc.features.domain_hyphen_count = disk_rec->features.domain_hyphen_count;
    doc.features.latin_alpha_count = disk_rec->features.latin_alpha_count;
    doc.features.total_alpha_count = disk_rec->features.total_alpha_count;
    doc.features.outgoing_link_count = disk_rec->features.outgoing_link_count;
    doc.features.outgoing_anchor_word_count = disk_rec->features.outgoing_anchor_word_count;
    doc.features.raw_tld = ::string(raw_tld_view);

    return doc;
}

std::optional<DocumentRecord> DiskChunkReader::getDocumentByLocation(uint32_t location) const {
    if (!data_)
        return std::nullopt;

    BufferReader reader(data_ + header_.doctable_offset);
    reader.skip(sizeof(uint32_t));
    reader.skip(header_.num_documents * sizeof(uint64_t));

    for (uint32_t i = 0; i < header_.num_documents; ++i) {
        // Read cleanly!
        ::string_view url_view = reader.readString16();
        ::string_view raw_tld_view = reader.readString16();
        const DocumentRecordDisk *disk_rec = reader.readPOD<DocumentRecordDisk>();

        if (location >= disk_rec->start_location && location <= disk_rec->end_location) {
            DocumentRecord doc;
            doc.url = ::string(url_view);
            doc.start_location = disk_rec->start_location;
            doc.end_location = disk_rec->end_location;
            doc.word_count = disk_rec->word_count;
            doc.title_word_count = disk_rec->title_word_count;
            doc.seed_distance = disk_rec->seed_distance;
            doc.features.flags = disk_rec->features.flags;
            doc.features.base_domain_length = disk_rec->features.base_domain_length;
            doc.features.url_length = disk_rec->features.url_length;
            doc.features.path_length = disk_rec->features.path_length;
            doc.features.path_depth = disk_rec->features.path_depth;
            doc.features.query_param_count = disk_rec->features.query_param_count;
            doc.features.numeric_path_char_count = disk_rec->features.numeric_path_char_count;
            doc.features.domain_hyphen_count = disk_rec->features.domain_hyphen_count;
            doc.features.latin_alpha_count = disk_rec->features.latin_alpha_count;
            doc.features.total_alpha_count = disk_rec->features.total_alpha_count;
            doc.features.outgoing_link_count = disk_rec->features.outgoing_link_count;
            doc.features.outgoing_anchor_word_count = disk_rec->features.outgoing_anchor_word_count;
            doc.features.raw_tld = ::string(raw_tld_view);
            return doc;
        }
    }

    return std::nullopt;
}
