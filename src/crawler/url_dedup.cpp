#include "url_dedup.h"

#include "utils/threads/lock_guard.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <openssl/md5.h>
#include <stdexcept>

UrlBloomFilter::UrlBloomFilter(std::size_t bc, std::uint32_t hc, ::vector<bool> b)
    : bitCount(bc), hashCount(hc), bits(std::move(b)) {}

bool UrlBloomFilter::serializeToStream(FILE *f) const {
    lock_guard guard(m_);

    fwrite(&bitCount, sizeof(bitCount), 1, f);
    fwrite(&hashCount, sizeof(hashCount), 1, f);

    ::vector<uint8_t> packed((bitCount + 7) / 8, 0);
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
    ::vector<uint8_t> packed(packedSize);
    fread(packed.data(), 1, packedSize, f);

    ::vector<bool> b(bc, false);
    for (std::size_t i = 0; i < bc; ++i) {
        if (packed[i / 8] & (1 << (i % 8)))
            b[i] = true;
    }
    return UrlBloomFilter(bc, hc, std::move(b));
}

namespace {

static const char *kAllowedTlds[] = {
    "com", "org", "gov", "edu", "ca", "co.uk", "uk", "au", "net",
};
static constexpr std::size_t kAllowedTldCount = sizeof(kAllowedTlds) / sizeof(kAllowedTlds[0]);

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

bool hostHasAllowedTld(const string &host) {
    if (host.empty()) {
        return false;
    }

    for (std::size_t i = 0; i < kAllowedTldCount; ++i) {
        const char *tld = kAllowedTlds[i];
        std::size_t tldLen = std::strlen(tld);

        if (host.size() < tldLen) {
            continue;
        }

        std::size_t suffixStart = host.size() - tldLen;
        bool suffixMatches = true;
        for (std::size_t j = 0; j < tldLen; ++j) {
            if (host[suffixStart + j] != tld[j]) {
                suffixMatches = false;
                break;
            }
        }

        if (!suffixMatches) {
            continue;
        }

        if (host.size() == tldLen) {
            return true;
        }

        if (host[suffixStart - 1] == '.') {
            return true;
        }
    }

    return false;
}

bool hasAllowedTld(const string &normalizedUrl) {
    std::size_t schemePos = normalizedUrl.find("://");
    if (schemePos == string::npos) {
        return false;
    }

    string rest = normalizedUrl.substr(schemePos + 3);
    if (rest.empty()) {
        return false;
    }

    std::size_t hostEnd = rest.find_first_of("/?");
    string hostPort = hostEnd == string::npos ? rest : rest.substr(0, hostEnd);
    if (hostPort.empty()) {
        return false;
    }

    // Strip optional userinfo (user:pass@host).
    std::size_t atPos = hostPort.find('@');
    if (atPos != string::npos) {
        hostPort = hostPort.substr(atPos + 1);
    }

    if (hostPort.empty()) {
        return false;
    }

    // Exclude IPv6 literals and host:port forms that do not end in the allowed TLDs.
    if (hostPort[0] == '[') {
        return false;
    }

    std::size_t colonPos = string::npos;
    for (std::size_t i = 0; i < hostPort.size(); ++i) {
        if (hostPort[i] == ':') {
            colonPos = i;
        }
    }
    string host = colonPos == string::npos ? hostPort : hostPort.substr(0, colonPos);

    while (!host.empty() && host[host.size() - 1] == '.') {
        host.pop_back();
    }

    return hostHasAllowedTld(host);
}

} // namespace

string resolveUrl(const string &baseUrl, const string &rawHref) {
    string href = trim(rawHref);
    if (href.empty()) {
        return "";
    }

    // Already absolute
    if (href.find("://") != string::npos) {
        return href;
    }

    // Skip junk schemes
    if (href.size() >= 11 && strncasecmp(href.cstr(), "javascript:", 11) == 0) {
        return "";
    }
    if (href.size() >= 7 && strncasecmp(href.cstr(), "mailto:", 7) == 0) {
        return "";
    }
    if (href.size() >= 5 && strncasecmp(href.cstr(), "data:", 5) == 0) {
        return "";
    }

    // Fragment-only
    if (href[0] == '#') {
        return "";
    }

    // Parse base URL to extract scheme, host, path
    string base = trim(baseUrl);
    std::size_t schemeEnd = base.find("://");
    if (schemeEnd == string::npos) {
        return "";
    }
    string scheme = base.substr(0, schemeEnd);
    string rest = base.substr(schemeEnd + 3);

    std::size_t hostEnd = rest.find('/');
    string host = hostEnd == string::npos ? rest : rest.substr(0, hostEnd);
    string basePath = hostEnd == string::npos ? "/" : rest.substr(hostEnd);

    if (host.empty()) {
        return "";
    }

    // Protocol-relative: //host/path
    if (href.size() >= 2 && href[0] == '/' && href[1] == '/') {
        return scheme + ":" + href;
    }

    // Absolute path: /path
    if (href[0] == '/') {
        return scheme + "://" + host + href;
    }

    // Query-only: ?key=val
    if (href[0] == '?') {
        // Strip query/fragment from basePath
        std::size_t qPos = basePath.find('?');
        if (qPos != string::npos) {
            basePath = basePath.substr(0, qPos);
        }
        std::size_t fPos = basePath.find('#');
        if (fPos != string::npos) {
            basePath = basePath.substr(0, fPos);
        }
        return scheme + "://" + host + basePath + href;
    }

    // Relative path: append to directory of base
    std::size_t lastSlash = string::npos;
    for (std::size_t i = 0; i < basePath.size(); ++i) {
        if (basePath[i] == '/') {
            lastSlash = i;
        }
    }
    string dir = lastSlash != string::npos ? basePath.substr(0, lastSlash + 1) : "/";
    return scheme + "://" + host + dir + href;
}

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

::pair<std::uint64_t, std::uint64_t> UrlBloomFilter::hashKey(const string &key) {
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
    lock_guard guard(m_);
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
    lock_guard guard(m_);
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

bool UrlBloomFilter::checkAndInsert(const string &key) {
    lock_guard guard(m_);
    if (key.empty()) {
        return false;
    }

    auto [h1, h2] = hashKey(key);
    for (std::uint32_t i = 0; i < hashCount; i++) {
        std::size_t index = static_cast<std::size_t>((h1 + static_cast<std::uint64_t>(i + 1) * h2) %
                                                     static_cast<std::uint64_t>(bitCount));
        if (!getBit(index)) {
            // Found a bit that's not set, so key is not in filter
            // Now we need to set all bits for this key
            auto [h1_inner, h2_inner] = hashKey(key);
            for (std::uint32_t j = 0; j < hashCount; j++) {
                std::size_t idx = static_cast<std::size_t>(
                    (h1_inner + static_cast<std::uint64_t>(j + 1) * h2_inner) %
                    static_cast<std::uint64_t>(bitCount));
                setBit(idx);
            }
            return true; // Key was not present, we inserted it
        }
    }
    return false; // Key was already present
}

bool shouldEnqueueUrl(const string &rawUrl, UrlBloomFilter &bloom, string &canonicalOut) {
    string normalizeOut = normalizeUrl(rawUrl);
    if (normalizeOut.empty()) {
        return false;
    }
    if (!hasAllowedTld(normalizeOut)) {
        return false;
    }
    canonicalOut = normalizeOut;
    // Atomically check if URL is in bloom filter and insert it if not
    // This avoids TOCTOU race condition where two threads could both think
    // the URL is new and both add it to the frontier
    return bloom.checkAndInsert(normalizeOut);
}
