// src/lib/isr_container.h
#pragma once
#include "disk_chunk_reader.h"
#include "isr.h"
#include "isr_and.h"
#include <memory>
#include <vector>

class ISRContainer : public ISR {
  public:
    ISRContainer(std::unique_ptr<ISR> positive_isr, std::vector<std::unique_ptr<ISR>> negative_isrs,
                 std::unique_ptr<ISRWord> doc_end_isr, const DiskChunkReader &reader);

    uint32_t next() override;
    uint32_t seek(uint32_t target) override;
    uint32_t currentLocation() const override { return current_loc_; }
    bool done() const override { return is_exhausted_; }

  private:
    std::unique_ptr<ISR> positive_isr_;
    std::vector<std::unique_ptr<ISR>> negative_isrs_;
    std::unique_ptr<ISRWord> doc_end_isr_;
    const DiskChunkReader &reader_;

    uint32_t current_loc_;
    bool is_exhausted_;

    uint32_t advanceToNextMatch();
};
