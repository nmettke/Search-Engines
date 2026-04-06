// src/lib/isr_phrase.h
#pragma once
#include "isr.h"
#include <memory>
#include "utils/vector.hpp"

class ISRPhrase : public ISR {
  public:
    explicit ISRPhrase(::vector<std::unique_ptr<ISR>> terms);

    uint32_t next() override;
    uint32_t seek(uint32_t target) override;
    uint32_t currentLocation() const override { return current_loc_; }
    bool done() const override { return is_exhausted_; }

  private:
    ::vector<std::unique_ptr<ISR>> terms_;
    uint32_t current_loc_;
    bool is_exhausted_;

    uint32_t advanceToNextMatch();
};