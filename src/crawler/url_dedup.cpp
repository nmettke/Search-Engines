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

UrlBloomFilter::Snapshot UrlBloomFilter::snapshot() const {
    Snapshot snap;
    vector<bool> bitsCopy;
    {
        lock_guard<mutex> guard(m_);
        snap.bitCount = bitCount;
        snap.hashCount = hashCount;
        bitsCopy = bits;
    }

    snap.packedBits = ::vector<uint8_t>((bitCount + 7) / 8, 0);
    for (std::size_t i = 0; i < snap.bitCount; ++i) {
        if (bitsCopy[i])
            snap.packedBits[i / 8] |= (1 << (i % 8));
    }
    return snap;
}

std::size_t UrlBloomFilter::memoryUsageBytes() const {
    lock_guard<mutex> guard(m_);
    return sizeof(UrlBloomFilter) + bits.capacity() * sizeof(bool);
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
    "com", "org", "gov", "edu", "ca", "co.uk", "com.au", "uk", "au", "net", "mil", "int",
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

    while (last > first && input[last - 1] == '/') {
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

bool hasAllowedFileExtension(const string &url) {
    // Reject known bad extensions
    const char *badExts[] = {".pdf",  ".doc", ".docx", ".xls", ".xlsx", ".zip",  ".exe", ".jpg",
                             ".jpeg", ".png", ".gif",  ".bmp", ".svg",  ".webp", ".mp4", ".webm",
                             ".avi",  ".mov", ".mkv",  ".flv", ".mp3",  ".wav",  ".aac", ".flac",
                             ".m4a",  ".tar", ".gz",   ".rar", ".7z"};

    size_t numBadExts = sizeof(badExts) / sizeof(badExts[0]);
    string lowerUrl;
    for (size_t i = 0; i < url.size(); ++i) {
        char c = url[i];
        lowerUrl.pushBack((c >= 'A' && c <= 'Z') ? (c - 'A' + 'a') : c);
    }

    for (size_t i = 0; i < numBadExts; ++i) {
        size_t extLen = 0;
        for (const char *p = badExts[i]; *p; ++p) {
            ++extLen;
        }

        if (lowerUrl.size() >= extLen) {
            bool matches = true;
            for (size_t j = 0; j < extLen; ++j) {
                if (lowerUrl[lowerUrl.size() - extLen + j] != badExts[i][j]) {
                    matches = false;
                    break;
                }
            }
            if (matches) {
                return false;
            }
        }
    }

    return true;
}

bool hasReasonablePercentEncoding(const string &normalizedUrl) {
    // Reject heavily encoded URLs (often non-content, tracking, or low-value paths).
    static constexpr size_t kMaxPercentSigns = 8;
    static constexpr size_t kMinTailLengthForDensity = 24;
    static constexpr double kMaxPercentDensity = 0.10;

    std::size_t schemePos = normalizedUrl.find("://");
    if (schemePos == string::npos) {
        return false;
    }

    string rest = normalizedUrl.substr(schemePos + 3);
    std::size_t hostEnd = rest.find_first_of("/?");
    if (hostEnd == string::npos) {
        return true;
    }

    string tail = rest.substr(hostEnd);
    if (tail.empty()) {
        return true;
    }

    size_t percentCount = 0;
    for (size_t i = 0; i < tail.size(); ++i) {
        if (tail[i] == '%') {
            ++percentCount;
        }
    }

    if (percentCount <= kMaxPercentSigns) {
        return true;
    }

    if (tail.size() < kMinTailLengthForDensity) {
        return false;
    }

    double density = static_cast<double>(percentCount) / static_cast<double>(tail.size());
    return density <= kMaxPercentDensity;
}

bool startsWith(const string &value, const char *prefix) {
    std::size_t i = 0;
    while (prefix[i] != '\0') {
        if (i >= value.size() || value[i] != prefix[i]) {
            return false;
        }
        ++i;
    }
    return true;
}

bool hasExplicitUnsupportedScheme(const string &value) {
    std::size_t colonPos = value.find(':');
    if (colonPos == string::npos) {
        return false;
    }
    std::size_t firstSeparator = value.find_first_of("/?#");
    return firstSeparator == string::npos || colonPos < firstSeparator;
}

string removeDotSegments(const string &path) {
    vector<string> segments;
    std::size_t i = 0;
    while (i < path.size()) {
        while (i < path.size() && path[i] == '/') {
            ++i;
        }
        std::size_t start = i;
        while (i < path.size() && path[i] != '/') {
            ++i;
        }
        if (i == start) {
            continue;
        }
        string seg = path.substr(start, i - start);
        if (seg == ".") {
            continue;
        }
        if (seg == "..") {
            if (!segments.empty()) {
                segments.popBack();
            }
            continue;
        }
        segments.pushBack(seg);
    }

    string normalized = "/";
    for (std::size_t j = 0; j < segments.size(); ++j) {
        if (j > 0) {
            normalized.pushBack('/');
        }
        normalized += segments[j];
    }

    if (!path.empty() && path[path.size() - 1] == '/' && normalized[normalized.size() - 1] != '/') {
        normalized.pushBack('/');
    }
    return normalized;
}

// Strips trailing '/' from the path portion of tail (path + optional '?query'), so e.g.
// /foo/?q=1 -> /foo?q=1 and /?q=1 -> ?q=1 (empty path before query).
string stripTrailingSlashFromPath(const string &tail) {
    if (tail.empty()) {
        return tail;
    }

    std::size_t qpos = tail.find('?');
    string pathPart = (qpos == string::npos) ? tail : tail.substr(0, qpos);
    string queryPart = (qpos == string::npos) ? "" : tail.substr(qpos);

    while (!pathPart.empty() && pathPart[pathPart.size() - 1] == '/') {
        pathPart.pop_back();
    }

    return pathPart + queryPart;
}

} // namespace

string absolutizeUrl(const string &rawUrl, const string &sourceUrl, const string &baseUrl) {
    string cleaned = trim(rawUrl);
    if (cleaned.empty() || cleaned[0] == '#') {
        return "";
    }

    if (startsWith(cleaned, "http://") || startsWith(cleaned, "https://")) {
        return normalizeUrl(cleaned);
    }

    if (hasExplicitUnsupportedScheme(cleaned)) {
        return "";
    }

    string context = normalizeUrl(baseUrl);
    if (context.empty()) {
        context = normalizeUrl(sourceUrl);
    }
    if (context.empty()) {
        return "";
    }

    std::size_t schemePos = context.find("://");
    if (schemePos == string::npos) {
        return "";
    }

    string scheme = context.substr(0, schemePos);
    string rest = context.substr(schemePos + 3);

    std::size_t hostEnd = rest.find_first_of("/?");
    string authority = hostEnd == string::npos ? rest : rest.substr(0, hostEnd);
    string pathAndQuery = hostEnd == string::npos ? "/" : rest.substr(hostEnd);

    std::size_t queryPos = pathAndQuery.find('?');
    string path = queryPos == string::npos ? pathAndQuery : pathAndQuery.substr(0, queryPos);
    if (path.empty()) {
        path = "/";
    }

    if (startsWith(cleaned, "//")) {
        return normalizeUrl(scheme + ":" + cleaned);
    }

    if (cleaned[0] == '?') {
        return normalizeUrl(scheme + "://" + authority + path + cleaned);
    }

    string resolvedPath;
    if (cleaned[0] == '/') {
        resolvedPath = removeDotSegments(cleaned);
    } else {
        std::size_t slashPos = path.size();
        while (slashPos > 0 && path[slashPos - 1] != '/') {
            --slashPos;
        }
        string baseDir = slashPos == 0 ? "/" : path.substr(0, slashPos);
        resolvedPath = removeDotSegments(baseDir + cleaned);
    }

    return normalizeUrl(scheme + "://" + authority + resolvedPath);
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
    return scheme + "://" + host + stripTrailingSlashFromPath(tail);
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
    lock_guard<mutex> guard(m_);
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
    lock_guard<mutex> guard(m_);
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
    lock_guard<mutex> guard(m_);
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
    if (!passesUrlQualityChecks(rawUrl, canonicalOut)) {
        return false;
    }

    // Atomically check if URL is in bloom filter and insert it if not
    // This avoids TOCTOU race condition where two threads could both think
    // the URL is new and both add it to the frontier
    return bloom.checkAndInsert(canonicalOut);
}

bool passesUrlQualityChecks(const string &rawUrl, string &canonicalOut) {
    string normalizeOut = normalizeUrl(rawUrl);
    if (normalizeOut.empty()) {
        return false;
    }
    if (!hasAllowedTld(normalizeOut)) {
        return false;
    }
    if (!hasAllowedFileExtension(normalizeOut)) {
        return false;
    }
    if (!hasReasonablePercentEncoding(normalizeOut)) {
        return false;
    }

    canonicalOut = normalizeOut;
    return true;
}
