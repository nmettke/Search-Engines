// index/binary_writer.h
#pragma once

#include <string>
#include <system_error>
#include <unistd.h>
#include <fcntl.h>

class BinaryWriter {
public:
    explicit BinaryWriter(int fd) : fd_(fd) {}

    // writes Plain Old Data (POD) structs or primitive types
    template <typename T>
    void writePOD(const T& data) {
        ssize_t bytes_written = write(fd_, &data, sizeof(T));
        if (bytes_written != sizeof(T)) {
            throw std::system_error(errno, std::generic_category(), "BinaryWriter failed to write POD");
        }
    }

    // writes a string prefixed by a 16-bit length
    void writeString16(const std::string& str) {
        uint16_t len = static_cast<uint16_t>(str.size());
        writePOD(len); // Write the length prefix
        
        if (len > 0) {
            ssize_t bytes_written = write(fd_, str.data(), len);
            if (bytes_written != len) {
                throw std::system_error(errno, std::generic_category(), "BinaryWriter failed to write string data");
            }
        }
    }

    // writes an array of PODs (like our std::vector<uint8_t> compressed bytes)
    template <typename T>
    void writeBuffer(const T* data, size_t count) {
        if (count == 0) return;
        size_t total_bytes = count * sizeof(T);
        ssize_t bytes_written = write(fd_, data, total_bytes);
        if (bytes_written < 0 || static_cast<size_t>(bytes_written) != total_bytes) {
            throw std::system_error(errno, std::generic_category(), "BinaryWriter failed to write buffer");
        }
    }

    off_t currentOffset() const {
        off_t offset = lseek(fd_, 0, SEEK_CUR);
        if (offset == (off_t)-1) {
            throw std::system_error(errno, std::generic_category(), "BinaryWriter failed to get offset");
        }
        return offset;
    }

    void seekSet(off_t offset) {
        if (lseek(fd_, offset, SEEK_SET) == (off_t)-1) {
            throw std::system_error(errno, std::generic_category(), "BinaryWriter failed to seek");
        }
    }

private:
    int fd_;
};