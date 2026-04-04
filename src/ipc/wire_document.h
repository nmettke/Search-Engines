#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// One-way crawler -> indexer document payload (binary), used inside a framed stream.

struct WireLink {
    std::string url;
    std::vector<std::string> anchorText;
};

struct WireDocument {
    std::string base;
    std::vector<std::string> words;
    std::vector<std::string> titleWords;
    std::vector<WireLink> links;
};

bool wireEncodeDocument(const WireDocument &doc, std::vector<std::uint8_t> &out);
bool wireDecodeDocument(const std::uint8_t *data, std::size_t len, WireDocument &out);

constexpr std::uint32_t kWireFrameMagic = 0x444F4350; // 'DOCP'
constexpr std::uint32_t kWireFrameVersion = 1;
constexpr std::size_t kWireMaxPayload = 64 * 1024 * 1024;

// Frame: magic(4) | version(4) | payload_len(4) | payload
bool wireSendFramedMessage(int fd, const std::vector<std::uint8_t> &payload);
bool wireRecvFramedMessage(int fd, std::vector<std::uint8_t> &payload);
