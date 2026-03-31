// disk_chunk_writer.cpp
#include "disk_chunk_writer.h"
#include "vbyte.h"
#include <fcntl.h>
#include <system_error>
#include <unistd.h>

DiskChunkWriter::DiskChunkWriter(const std::string &filename) {
    fd_ = open(filename.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd_ < 0) {
        throw std::system_error(errno, std::generic_category(),
                                "Failed to open chunk file for writing");
    }
}

DiskChunkWriter::~DiskChunkWriter() {
    if (fd_ >= 0) {
        close(fd_);
    }
}

void DiskChunkWriter::writeHeader(const FileHeader &header) {
    ssize_t bytes_written = write(fd_, &header, sizeof(FileHeader));
    if (bytes_written != sizeof(FileHeader)) {
        throw std::system_error(errno, std::generic_category(), "Failed to write complete header");
    }
}

uint64_t DiskChunkWriter::writePostingList(const std::vector<uint32_t> &locations) {
    off_t current_offset = lseek(fd_, 0, SEEK_CUR);
    if (current_offset == (off_t)-1) {
        throw std::system_error(errno, std::generic_category(), "Failed to get file offset");
    }

    // compress the locations using Variable Byte Encoding
    std::vector<uint8_t> compressed_data = VariableByteEncoder::encodeDeltaList(locations);

    // prepare the posting list header
    PostingListHeader pl_header;
    pl_header.num_postings = locations.size();
    pl_header.has_seek_table = 0; // 0 for MVP
    pl_header.data_size = compressed_data.size();

    // write the header
    ssize_t header_written = write(fd_, &pl_header, sizeof(PostingListHeader));
    if (header_written != sizeof(PostingListHeader)) {
        throw std::system_error(errno, std::generic_category(),
                                "Failed to write PostingListHeader");
    }

    // write the compressed payload
    ssize_t data_written = write(fd_, compressed_data.data(), compressed_data.size());
    if (data_written < 0 || static_cast<size_t>(data_written) != compressed_data.size()) {
        throw std::system_error(errno, std::generic_category(),
                                "Failed to write compressed posting data");
    }

    return static_cast<uint64_t>(current_offset);
}

uint64_t DiskChunkWriter::writeDocumentTable(const std::vector<DocumentRecord> &documents) {
    off_t current_offset = lseek(fd_, 0, SEEK_CUR);
    if (current_offset == (off_t)-1) {
        throw std::system_error(errno, std::generic_category(), "Failed to get file offset");
    }

    // write Document Table Header (num_documents)
    uint32_t num_docs = static_cast<uint32_t>(documents.size());
    if (write(fd_, &num_docs, sizeof(uint32_t)) != sizeof(uint32_t)) {
        throw std::system_error(errno, std::generic_category(), "Failed to write document count");
    }

    // write each document record sequentially
    for (const auto &doc : documents) {
        // write URL length (uint16_t)
        uint16_t url_len = static_cast<uint16_t>(doc.url.size());
        if (write(fd_, &url_len, sizeof(uint16_t)) != sizeof(uint16_t)) {
            throw std::system_error(errno, std::generic_category(), "Failed to write URL length");
        }

        // write raw URL string (without null terminator)
        if (url_len > 0) {
            if (write(fd_, doc.url.data(), url_len) != url_len) {
                throw std::system_error(errno, std::generic_category(),
                                        "Failed to write URL string");
            }
        }

        // write remaining fixed-size fields
        struct __attribute__((packed)) FixedDocData {
            uint32_t start_location;
            uint32_t end_location;
            uint32_t word_count;
            uint16_t title_word_count;
        } fixed_data = {doc.start_location, doc.end_location, doc.word_count, doc.title_word_count};

        if (write(fd_, &fixed_data, sizeof(FixedDocData)) != sizeof(FixedDocData)) {
            throw std::system_error(errno, std::generic_category(),
                                    "Failed to write fixed document metadata");
        }
    }

    return static_cast<uint64_t>(current_offset);
}


uint64_t
DiskChunkWriter::writeDictionary(const std::vector<std::vector<DictionaryEntry>> &buckets) {
    off_t dict_start_offset = lseek(fd_, 0, SEEK_CUR);
    if (dict_start_offset == (off_t)-1)
        throw std::system_error(errno, std::generic_category(), "lseek failed");

    // write num_buckets
    size_t num_buckets = buckets.size();
    write(fd_, &num_buckets, sizeof(size_t));

    // write placeholder Buckets[] array
    off_t bucket_array_start = lseek(fd_, 0, SEEK_CUR);
    std::vector<size_t> bucket_offsets(num_buckets, 0);
    write(fd_, bucket_offsets.data(), num_buckets * sizeof(size_t));

    // write the chains and record their start offsets
    for (size_t i = 0; i < num_buckets; ++i) {
        if (buckets[i].empty())
            continue;

        // record the offset for this bucket relative to the start of the dict section
        off_t current_chain_start = lseek(fd_, 0, SEEK_CUR);
        bucket_offsets[i] = current_chain_start - dict_start_offset;

        // write each entry in the chain
        for (const auto &entry : buckets[i]) {
            BucketDisk b_disk = entry.disk_info;
            b_disk.string_length = static_cast<uint16_t>(entry.term.size());

            write(fd_, &b_disk, sizeof(BucketDisk));
            write(fd_, entry.term.data(), b_disk.string_length);
        }

        // write Sentinel (Length/Occupied = 0) to mark end of chain
        BucketDisk sentinel;
        sentinel.occupied = 0;
        write(fd_, &sentinel, sizeof(BucketDisk));
    }

    // seek back and overwrite the placeholder Buckets[] array with real offsets
    off_t end_of_dict = lseek(fd_, 0, SEEK_CUR);
    lseek(fd_, bucket_array_start, SEEK_SET);
    write(fd_, bucket_offsets.data(), num_buckets * sizeof(size_t));

    // seek back to the end of the file so future writes append correctly
    lseek(fd_, end_of_dict, SEEK_SET);

    return static_cast<uint64_t>(dict_start_offset);
}

void DiskChunkWriter::finish(const FileHeader &final_header) {
    // seek to byte 0 and overwrite the header
    if (lseek(fd_, 0, SEEK_SET) == (off_t)-1) {
        throw std::system_error(errno, std::generic_category(),
                                "Failed to seek to beginning of file");
    }

    if (write(fd_, &final_header, sizeof(FileHeader)) != sizeof(FileHeader)) {
        throw std::system_error(errno, std::generic_category(), "Failed to write final header");
    }
}
