// This header includes code needed for the index stream reader
#pragma once

#include "types.h"

class ISR {
  public:
    ISR();
    ISR(const uint8_t *data, uint32_t num_postings);

    uint32_t next();
    uint32_t seek(uint32_t target);

    uint32_t currentLocation() const { return current_loc; }
    uint32_t remaining() const { return num_postings - current_index; }
    bool done() const { return current_index >= num_postings; }

  private:
    uint32_t num_postings = 0;
    uint32_t current_index = 0;
    uint32_t current_loc = 0;
    const uint8_t *data = nullptr;
    const uint8_t *current_ptr = nullptr;
};