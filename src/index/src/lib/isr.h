// src/lib/isr.h
#pragma once

#include "seek_table.h"
#include "types.h"
#include <memory>
#include <optional>

class ISR {
  public:
    virtual ~ISR() = default;

    virtual uint32_t next() = 0;
    virtual uint32_t seek(uint32_t target) = 0;

    virtual uint32_t currentLocation() const = 0;
    virtual bool done() const = 0;
};

class ISRWord : public ISR {
  public:
    ISRWord();
    ISRWord(const uint8_t *data, uint32_t num_postings,
            std::optional<SeekTable> table = std::nullopt);

    uint32_t next() override;
    uint32_t seek(uint32_t target) override;

    uint32_t currentLocation() const override { return current_loc; }
    bool done() const override { return is_exhausted; }

    // Specific to disk readers
    uint32_t currentIndex() const { return current_index; }
    uint32_t remaining() const { return num_postings - current_index; }

  private:
    uint32_t num_postings = 0;
    uint32_t current_index = 0;
    uint32_t current_loc = 0;
    const uint8_t *data = nullptr;
    const uint8_t *current_ptr = nullptr;

    std::optional<SeekTable> seek_table;
    bool is_exhausted = true;
};