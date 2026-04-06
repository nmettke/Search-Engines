// src/lib/buffer_reader.h
#pragma once

#include "utils/STL_rewrite/string_view.hpp"
#include <cstdint>

class BufferReader {
  public:
    explicit BufferReader(const uint8_t *data) : ptr_(data) {}

    // reads a POD struct and advances the pointer.
    // returns a pointer to the mapped memory (zero-copy).
    template <typename T> const T *readPOD() {
        const T *val = reinterpret_cast<const T *>(ptr_);
        ptr_ += sizeof(T);
        return val;
    }

    // reads a 16-bit length prefix, then returns a string_view of the data.
    // does NOT copy the string. It just points to the mapped file!
    ::string_view readString16() {
        uint16_t len = *reinterpret_cast<const uint16_t *>(ptr_);
        ptr_ += sizeof(uint16_t);

        ::string_view str(reinterpret_cast<const char *>(ptr_), len);
        ptr_ += len;

        return str;
    }

    // skip bytes
    void skip(size_t bytes) { ptr_ += bytes; }

    // get the current pointer address
    const uint8_t *current() const { return ptr_; }

  private:
    const uint8_t *ptr_;
};
