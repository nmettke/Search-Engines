#include "url_dedup.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <openssl/md5.h>
#include <stdexcept>

UrlBloomFilter::UrlBloomFilter(std::size_t bc, std::uint32_t hc, std::vector<bool> b)
    : bitCount(bc), hashCount(hc), bits(std::move(b)) {}

bool UrlBloomFilter::serializeToStream(FILE *f) const {
    fwrite(&bitCount, sizeof(bitCount), 1, f);
    fwrite(&hashCount, sizeof(hashCount), 1, f);

    std::vector<uint8_t> packed((bitCount + 7) / 8, 0);
    for (std::size_t i = 0; i < bitCount; ++i) {
        if (bits[i])
            packed[i / 8] |= (1 << (i % 8));
    }
    fwrite(packed.data(), 1, packed.size(), f);
    return true;
}

UrlBloomFilter UrlBloomFilter::deserializeFromStream(FILE *f) {
    std::size_t bc;
    std::uint32_t hc;
    fread(&bc, sizeof(bc), 1, f);
    fread(&hc, sizeof(hc), 1, f);

    std::size_t packedSize = (bc + 7) / 8;
    std::vector<uint8_t> packed(packedSize);
    fread(packed.data(), 1, packedSize, f);

    std::vector<bool> b(bc, false);
    for (std::size_t i = 0; i < bc; ++i) {
        if (packed[i / 8] & (1 << (i % 8)))
            b[i] = true;
    }
    return UrlBloomFilter(bc, hc, std::move(b));
}

namespace {

string trim(const string &input) {
    std::size_t first = 0;
    while (first < input.size() && std::isspace(static_cast<unsigned char>(input[first]))) {
        first++;
    }

    std::size_t last = input.size();
    while (last > first && std::isspace(static_cast<unsigned char>(input[last - 1]))) {
        last--;
    }

    return input.substr(first, last - first);
}

string toLower(string value) {
    for (char &ch : value) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return value;
}

} // namespace

string normalizeUrl(const string &rawUrl) {
    string cleaned = trim(rawUrl);
    if (cleaned.empty()) {
        return "";
    }

    std::size_t schemePos = cleaned.find("://");
    if (schemePos == string::npos) {
        return "";
    }

    string scheme = toLower(cleaned.substr(0, schemePos));
    if (scheme != "http" && scheme != "https") {
        return "";
    }

    string rest = cleaned.substr(schemePos + 3);
    if (rest.empty()) {
        return "";
    }

    std::size_t fragmentPos = rest.find('#');
    if (fragmentPos != string::npos) {
        rest = rest.substr(0, fragmentPos);
    }

    if (rest.empty()) {
        return "";
    }

    std::size_t hostEnd = rest.find_first_of("/?");
    string host = hostEnd == string::npos ? rest : rest.substr(0, hostEnd);
    string tail = hostEnd == string::npos ? "" : rest.substr(hostEnd);

    if (host.empty()) {
        return "";
    }

    host = toLower(host);
    return scheme + "://" + host + tail;
}

UrlBloomFilter::UrlBloomFilter(std::size_t expectedItems, double falsePositiveRate)
    : bitCount(0), hashCount(0), bits() {
    if (expectedItems == 0 || falsePositiveRate <= 0.0 || falsePositiveRate >= 1.0) {
        throw std::invalid_argument("invalid bloom configuration");
    }

    double m = -static_cast<double>(expectedItems) * std::log(falsePositiveRate) /
               (std::log(2.0) * std::log(2.0));
    bitCount = static_cast<std::size_t>(std::ceil(m));
    if (bitCount == 0) {
        bitCount = 1;
    }

    double k = (static_cast<double>(bitCount) / static_cast<double>(expectedItems)) * std::log(2.0);
    hashCount = static_cast<std::uint32_t>(std::ceil(k));
    if (hashCount == 0) {
        hashCount = 1;
    }

    bits.assign(bitCount, false);
}

std::pair<std::uint64_t, std::uint64_t> UrlBloomFilter::hashKey(const string &key) {
    std::uint64_t h1 = 0;
    std::uint64_t h2 = 0;
    unsigned char digest[MD5_DIGEST_LENGTH];
    MD5(reinterpret_cast<const unsigned char *>(key.c_str()), key.size(), digest);
    std::memcpy(&h1, digest, sizeof(std::uint64_t));
    std::memcpy(&h2, digest + sizeof(std::uint64_t), sizeof(std::uint64_t));
    return {h1, h2};
}

bool UrlBloomFilter::getBit(std::size_t index) const { return bits[index]; }

void UrlBloomFilter::setBit(std::size_t index) { bits[index] = true; }

bool UrlBloomFilter::probablyContains(const string &key) const {
    if (key.empty()) {
        return false;
    }

    auto [h1, h2] = hashKey(key);
    for (std::uint32_t i = 0; i < hashCount; i++) {
        std::size_t index = static_cast<std::size_t>((h1 + static_cast<std::uint64_t>(i + 1) * h2) %
                                                     static_cast<std::uint64_t>(bitCount));
        if (!getBit(index)) {
            return false;
        }
    }
    return true;
}

void UrlBloomFilter::insert(const string &key) {
    if (key.empty()) {
        return;
    }

    auto [h1, h2] = hashKey(key);
    for (std::uint32_t i = 0; i < hashCount; i++) {
        std::size_t index = static_cast<std::size_t>((h1 + static_cast<std::uint64_t>(i + 1) * h2) %
                                                     static_cast<std::uint64_t>(bitCount));
        setBit(index);
    }
}

bool shouldEnqueueUrl(const string &rawUrl, UrlBloomFilter &bloom, string &canonicalOut) {
    string normalizeOut = normalizeUrl(rawUrl);
    if (normalizeOut.empty()) {
        return false;
    }
    if (bloom.probablyContains(normalizeOut)) {
        return false;
    }
    canonicalOut = normalizeOut;
    bloom.insert(normalizeOut);
    return true;
}
