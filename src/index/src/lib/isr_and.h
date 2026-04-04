// src/lib/isr_and.h
#pragma once

#include "disk_chunk_reader.h"
#include "isr.h"
#include <memory>
#include <optional>
#include <vector>

class ISRAnd : public ISR {
  public:
    ISRAnd(std::vector<std::unique_ptr<ISR>> terms, std::unique_ptr<ISRWord> doc_end_isr,
           const DiskChunkReader &reader);

    uint32_t next() override;
    uint32_t seek(uint32_t target) override;
    uint32_t currentLocation() const override { return current_loc_; }
    bool done() const override { return is_exhausted_; }

    std::optional<DocumentRecord> currentDocument() const { return current_doc_; }

  private:
    std::vector<std::unique_ptr<ISR>> terms_;
    std::unique_ptr<ISRWord> doc_end_isr_;
    const DiskChunkReader &reader_;

    uint32_t current_loc_;
    bool is_exhausted_;
    std::optional<DocumentRecord> current_doc_;

    uint32_t advanceToNextMatch();
};
