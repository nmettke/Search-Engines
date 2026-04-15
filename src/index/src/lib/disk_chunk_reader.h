// src/lib/disk_chunk_reader.h
#pragma once

#include "isr.h"
#include "types.h"
#include "utils/string.hpp"
#include <optional>

struct TermInfo {
    uint32_t doc_frequency = 0;
    uint64_t collection_frequency = 0;
};

class DiskChunkReader {
  public:
    DiskChunkReader();
    ~DiskChunkReader();

    // prevent copying because we manage raw memory resources
    DiskChunkReader(const DiskChunkReader &) = delete;
    DiskChunkReader &operator=(const DiskChunkReader &) = delete;

    // opens the file, mmaps it, and validates the header.
    // returns true on success, false on failure (e.g., bad magic number).
    bool open(const ::string &filename);

    // expose the parsed header
    const FileHeader &header() const { return header_; }

    // returns a unique_ptr to an ISRWord. Returns nullptr if term is not found.
    std::unique_ptr<ISRWord> createISR(const ::string &term) const;

    std::optional<TermInfo> getTermInfo(const ::string &term) const;
    bool hasTerm(const ::string &term) const { return getTermInfo(term).has_value(); }

    // retrieves a document by its integer ID (0 to num_documents - 1).
    // returns std::nullopt if the ID is out of bounds.
    std::optional<DocumentRecord> getDocument(uint32_t doc_id) const;

    // retrieves a document that contains the given location.
    // returns std::nullopt if no document contains that location.
    std::optional<DocumentRecord> getDocumentByLocation(uint32_t location) const;

  private:
    int fd_;              // File descriptor
    size_t mapped_size_;  // Total size of the mapped file
    const uint8_t *data_; // Pointer to the start of the memory-mapped bytes
    FileHeader header_;   // A copy of the validated header for easy access
};
