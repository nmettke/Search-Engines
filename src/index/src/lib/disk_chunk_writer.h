// src/lib/disk_chunk_writer.h
#pragma once

#include "types.h"
#include <stdexcept>
#include <string>

class DiskChunkWriter {
  public:
    explicit DiskChunkWriter(const std::string &filename);

    ~DiskChunkWriter();

    // disable copy to prevent double-closing the file descriptor
    DiskChunkWriter(const DiskChunkWriter &) = delete;
    DiskChunkWriter &operator=(const DiskChunkWriter &) = delete;

    void writeHeader(const FileHeader &header);

    // compresses a list of locations and writes them to disk.
    // returns the byte offset where this posting list begins.
    uint64_t writePostingList(const std::vector<uint32_t> &locations);

    // writes the document table and returns its starting byte offset
    uint64_t writeDocumentTable(const std::vector<DocumentRecord> &documents);

    struct DictionaryEntry {
        std::string term;
        BucketDisk disk_info;
    };

    // writes the chained dictionary. 'buckets' is a vector where each element
    // is a chain (vector) of entries for that specific bucket.
    uint64_t writeDictionary(const std::vector<std::vector<DictionaryEntry>> &buckets);

    // overwrites the placeholder header at the beginning of the file with the final offsets.
    void finish(const FileHeader &final_header);

  private:
    int fd_; // POSIX file descriptor
};