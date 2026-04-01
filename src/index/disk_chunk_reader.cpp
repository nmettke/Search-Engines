#include "Common.h"
#include "disk_chunk_reader.h"
#include "buffer_reader.h"
#include "types.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <cstring>
#include <iostream>

DiskChunkReader::DiskChunkReader() 
    : fd_(-1), mapped_size_(0), data_(nullptr) {}

DiskChunkReader::~DiskChunkReader() {
    if (data_ != nullptr && data_ != MAP_FAILED) {
        munmap((void*)data_, mapped_size_);
    }
    if (fd_ >= 0) {
        close(fd_);
    }
}

bool DiskChunkReader::open(const std::string& filename) {
    fd_ = ::open(filename.c_str(), O_RDONLY);
    if (fd_ < 0) return false;

    // get file size
    struct stat st;
    if (fstat(fd_, &st) < 0) return false;
    mapped_size_ = st.st_size;

    // ensure it's at least large enough to hold a FileHeader
    if (mapped_size_ < sizeof(FileHeader)) return false;

    void* mapped = mmap(nullptr, mapped_size_, PROT_READ, MAP_PRIVATE, fd_, 0);
    if (mapped == MAP_FAILED) return false;

    // cast to byte pointer for safe offset arithmetic later
    data_ = static_cast<const uint8_t*>(mapped);

    // validate the header
    BufferReader reader(data_);
    const FileHeader* file_header = reader.readPOD<FileHeader>();

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

ISR DiskChunkReader::createISR(const std::string& term) const {
    if (!data_) return ISR();

    // hash the term
    uint64_t hash_val = hashString(term.c_str());

    // jump to Dictionary Header
    const uint8_t* dict_ptr = data_ + header_.dict_offset;
    size_t num_buckets = *reinterpret_cast<const size_t*>(dict_ptr);
    if (num_buckets == 0) return ISR();

    // look up the Bucket Offset
    const size_t* buckets_array = reinterpret_cast<const size_t*>(dict_ptr + sizeof(size_t));
    size_t chain_offset = buckets_array[hash_val % num_buckets];
    if (chain_offset == 0) return ISR();

    // walk the chain
    BufferReader reader(dict_ptr + chain_offset);

    while (true) {
        const BucketDisk* b_disk = reader.readPOD<BucketDisk>();
        if (b_disk->occupied == 0) break;

        // read the string immediately following the struct
        std::string_view current_term(reinterpret_cast<const char*>(reader.current()), b_disk->string_length);
        reader.skip(b_disk->string_length);

        if (std::string(current_term) == term) {
            // jump to the posting list offset
            BufferReader p_reader(data_ + header_.postings_offset + b_disk->posting_offset);
            
            // read PostingListHeader
            const PostingListHeader* p_header = p_reader.readPOD<PostingListHeader>();
            
            // read the compressed VByte data
            const uint8_t* compressed_data = p_reader.current();

            return ISR(compressed_data, p_header->num_postings);
        }
    }

    return ISR();
}

std::optional<DocumentRecord> DiskChunkReader::getDocument(uint32_t doc_id) const {
    if (!data_ || doc_id >= header_.num_documents) {
        return std::nullopt;
    }

    BufferReader reader(data_ + header_.doctable_offset);

    // skip the total number of documents header
    reader.skip(sizeof(uint32_t));

    // scan until we find the target document ID
    for (uint32_t i = 0; i <= doc_id; ++i) {
        if (i == doc_id) {
            std::string_view url_view = reader.readString16();
            const DocumentRecordDisk* disk_rec = reader.readPOD<DocumentRecordDisk>();

            DocumentRecord doc;
            doc.url = std::string(url_view);
            doc.start_location = disk_rec->start_location;
            doc.end_location = disk_rec->end_location;
            doc.word_count = disk_rec->word_count;
            doc.title_word_count = disk_rec->title_word_count;
            return doc;
        } else {
            reader.readString16();  // advance past URL
            reader.skip(sizeof(DocumentRecordDisk));
        }
    }

    return std::nullopt;
}

std::optional<DocumentRecord> DiskChunkReader::getDocumentByLocation(uint32_t location) const {
    if (!data_) return std::nullopt;

    BufferReader reader(data_ + header_.doctable_offset);
    reader.skip(sizeof(uint32_t));

    for (uint32_t i = 0; i < header_.num_documents; ++i) {
        // Read cleanly!
        std::string_view url_view = reader.readString16();
        const DocumentRecordDisk* disk_rec = reader.readPOD<DocumentRecordDisk>();

        if (location >= disk_rec->start_location && location <= disk_rec->end_location) {
            DocumentRecord doc;
            doc.url = std::string(url_view); 
            doc.start_location = disk_rec->start_location;
            doc.end_location = disk_rec->end_location;
            doc.word_count = disk_rec->word_count;
            doc.title_word_count = disk_rec->title_word_count;
            return doc;
        }
    }

    return std::nullopt;
}