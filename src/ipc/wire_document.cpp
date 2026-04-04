#include "wire_document.h"

#include <cstring>
#include <unistd.h>

namespace {

void appendU32(std::vector<std::uint8_t> &out, std::uint32_t v) {
    out.push_back(static_cast<std::uint8_t>(v & 0xff));
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
    out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xff));
    out.push_back(static_cast<std::uint8_t>((v >> 24) & 0xff));
}

bool readU32(const std::uint8_t *&p, const std::uint8_t *end, std::uint32_t &out) {
    if (end - p < 4)
        return false;
    out = static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
          (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
    p += 4;
    return true;
}

bool readBytes(const std::uint8_t *&p, const std::uint8_t *end, std::size_t n, std::string &out) {
    if (static_cast<std::size_t>(end - p) < n)
        return false;
    out.assign(reinterpret_cast<const char *>(p), n);
    p += n;
    return true;
}

bool writeAll(int fd, const void *buf, std::size_t len) {
    const auto *p = static_cast<const std::uint8_t *>(buf);
    std::size_t left = len;
    while (left > 0) {
        ssize_t n = ::write(fd, p, left);
        if (n < 0)
            return false;
        if (n == 0)
            return false;
        p += static_cast<std::size_t>(n);
        left -= static_cast<std::size_t>(n);
    }
    return true;
}

bool readAll(int fd, void *buf, std::size_t len) {
    auto *p = static_cast<std::uint8_t *>(buf);
    std::size_t left = len;
    while (left > 0) {
        ssize_t n = ::read(fd, p, left);
        if (n < 0)
            return false;
        if (n == 0)
            return false;
        p += static_cast<std::size_t>(n);
        left -= static_cast<std::size_t>(n);
    }
    return true;
}

} // namespace

bool wireEncodeDocument(const WireDocument &doc, std::vector<std::uint8_t> &out) {
    out.clear();
    auto putStr = [&out](const std::string &s) {
        appendU32(out, static_cast<std::uint32_t>(s.size()));
        for (unsigned char c : s)
            out.push_back(c);
    };
    putStr(doc.base);
    appendU32(out, static_cast<std::uint32_t>(doc.words.size()));
    for (const auto &w : doc.words)
        putStr(w);
    appendU32(out, static_cast<std::uint32_t>(doc.titleWords.size()));
    for (const auto &w : doc.titleWords)
        putStr(w);
    appendU32(out, static_cast<std::uint32_t>(doc.links.size()));
    for (const auto &lk : doc.links) {
        putStr(lk.url);
        appendU32(out, static_cast<std::uint32_t>(lk.anchorText.size()));
        for (const auto &a : lk.anchorText)
            putStr(a);
    }
    return true;
}

bool wireDecodeDocument(const std::uint8_t *data, std::size_t len, WireDocument &out) {
    out = WireDocument{};
    const std::uint8_t *p = data;
    const std::uint8_t *end = data + len;

    auto getStr = [&p, end](std::string &s) -> bool {
        std::uint32_t n = 0;
        if (!readU32(p, end, n))
            return false;
        if (static_cast<std::size_t>(end - p) < n)
            return false;
        return readBytes(p, end, n, s);
    };

    if (!getStr(out.base))
        return false;
    std::uint32_t nw = 0;
    if (!readU32(p, end, nw))
        return false;
    out.words.resize(nw);
    for (std::uint32_t i = 0; i < nw; ++i) {
        if (!getStr(out.words[i]))
            return false;
    }
    std::uint32_t nt = 0;
    if (!readU32(p, end, nt))
        return false;
    out.titleWords.resize(nt);
    for (std::uint32_t i = 0; i < nt; ++i) {
        if (!getStr(out.titleWords[i]))
            return false;
    }
    std::uint32_t nl = 0;
    if (!readU32(p, end, nl))
        return false;
    out.links.resize(nl);
    for (std::uint32_t i = 0; i < nl; ++i) {
        if (!getStr(out.links[i].url))
            return false;
        std::uint32_t na = 0;
        if (!readU32(p, end, na))
            return false;
        out.links[i].anchorText.resize(na);
        for (std::uint32_t j = 0; j < na; ++j) {
            if (!getStr(out.links[i].anchorText[j]))
                return false;
        }
    }
    return p == end;
}

bool wireEncodeDocumentBatch(const std::vector<WireDocument> &docs,
                             std::vector<std::uint8_t> &out) {
    out.clear();
    appendU32(out, kWireBatchMagic);
    appendU32(out, kWireBatchVersion);
    appendU32(out, static_cast<std::uint32_t>(docs.size()));

    std::vector<std::uint8_t> encoded_doc;
    for (const auto &doc : docs) {
        if (!wireEncodeDocument(doc, encoded_doc))
            return false;
        appendU32(out, static_cast<std::uint32_t>(encoded_doc.size()));
        out.insert(out.end(), encoded_doc.begin(), encoded_doc.end());
    }

    return true;
}

bool wireDecodeDocumentBatch(const std::uint8_t *data, std::size_t len,
                             std::vector<WireDocument> &out) {
    out.clear();
    const std::uint8_t *p = data;
    const std::uint8_t *end = data + len;

    std::uint32_t magic = 0;
    std::uint32_t version = 0;
    std::uint32_t doc_count = 0;
    if (!readU32(p, end, magic) || !readU32(p, end, version) || !readU32(p, end, doc_count))
        return false;
    if (magic != kWireBatchMagic || version != kWireBatchVersion)
        return false;

    out.reserve(doc_count);
    for (std::uint32_t i = 0; i < doc_count; ++i) {
        std::uint32_t doc_len = 0;
        if (!readU32(p, end, doc_len))
            return false;
        if (static_cast<std::size_t>(end - p) < doc_len)
            return false;

        WireDocument doc;
        if (!wireDecodeDocument(p, doc_len, doc))
            return false;
        p += doc_len;
        out.push_back(std::move(doc));
    }

    return p == end;
}

bool wireDecodePayloadDocuments(const std::uint8_t *data, std::size_t len,
                                std::vector<WireDocument> &out) {
    if (wireDecodeDocumentBatch(data, len, out))
        return true;

    WireDocument doc;
    if (!wireDecodeDocument(data, len, doc))
        return false;

    out.clear();
    out.push_back(std::move(doc));
    return true;
}

bool wireSendFramedMessage(int fd, const std::vector<std::uint8_t> &payload) {
    std::uint8_t header[12];
    std::uint32_t magic = kWireFrameMagic;
    std::uint32_t ver = kWireFrameVersion;
    std::uint32_t plen = static_cast<std::uint32_t>(payload.size());
    std::memcpy(header + 0, &magic, 4);
    std::memcpy(header + 4, &ver, 4);
    std::memcpy(header + 8, &plen, 4);
    if (!writeAll(fd, header, sizeof(header)))
        return false;
    if (!payload.empty() && !writeAll(fd, payload.data(), payload.size()))
        return false;
    return true;
}

bool wireRecvFramedMessage(int fd, std::vector<std::uint8_t> &payload) {
    std::uint8_t header[12];
    if (!readAll(fd, header, sizeof(header)))
        return false;
    std::uint32_t magic = 0, ver = 0, plen = 0;
    std::memcpy(&magic, header + 0, 4);
    std::memcpy(&ver, header + 4, 4);
    std::memcpy(&plen, header + 8, 4);
    if (magic != kWireFrameMagic || ver != kWireFrameVersion)
        return false;
    if (plen > kWireMaxPayload)
        return false;
    payload.resize(plen);
    if (plen > 0 && !readAll(fd, payload.data(), plen))
        return false;
    return true;
}
