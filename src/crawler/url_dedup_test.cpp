#include "url_dedup.h"

#include <cassert>
#include <iostream>
#include <stdexcept>
#include <string>

int main() {
    UrlBloomFilter bloom(1000, 0.01);
    std::string canonical;

    bool first = shouldEnqueueUrl("https://example.com/path", bloom, canonical);
    assert(first);
    assert(canonical == "https://example.com/path");

    bool duplicate = shouldEnqueueUrl("https://example.com/path", bloom, canonical);
    assert(!duplicate);

    bool caseNormalized = shouldEnqueueUrl("HTTPS://Example.COM/path", bloom, canonical);
    assert(!caseNormalized);

    bool fragmentDuplicate = shouldEnqueueUrl("https://example.com/path#section", bloom, canonical);
    assert(!fragmentDuplicate);

    bool invalidScheme = shouldEnqueueUrl("ftp://example.com/path", bloom, canonical);
    assert(!invalidScheme);

    bool empty = shouldEnqueueUrl("   ", bloom, canonical);
    assert(!empty);

    bool inserted = shouldEnqueueUrl("http://example.com/other", bloom, canonical);
    assert(inserted);

    bool seen = bloom.probablyContains("http://example.com/other");
    assert(seen);

    return 0;
}
