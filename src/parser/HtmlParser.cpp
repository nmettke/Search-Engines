// HtmlParser.cpp

#include "HtmlParser.h"
#include "utils/Utf8.h"

static bool isLatinAlpha(Unicode cp) {
    if ((cp >= 0x41 && cp <= 0x5A)     // A-Z
        || (cp >= 0x61 && cp <= 0x7A)) // a-z
        return true;
    // Latin-1 Supplement through Latin Extended-B, excluding × (0xD7) and ÷ (0xF7)
    if (cp >= 0xC0 && cp <= 0x024F && cp != 0xD7 && cp != 0xF7)
        return true;
    return false;
}

// Known non-Latin alphabetic ranges. Not exhaustive, but covers
// the major scripts we'd encounter crawling the web.
static bool isNonLatinAlpha(Unicode cp) {
    return (cp >= 0x0370 && cp <= 0x03FF)     // Greek
           || (cp >= 0x0400 && cp <= 0x04FF)  // Cyrillic
           || (cp >= 0x0500 && cp <= 0x052F)  // Cyrillic Supplement
           || (cp >= 0x0590 && cp <= 0x05FF)  // Hebrew
           || (cp >= 0x0600 && cp <= 0x06FF)  // Arabic
           || (cp >= 0x0900 && cp <= 0x097F)  // Devanagari
           || (cp >= 0x0E00 && cp <= 0x0E7F)  // Thai
           || (cp >= 0x1100 && cp <= 0x11FF)  // Hangul Jamo
           || (cp >= 0x3040 && cp <= 0x30FF)  // Hiragana + Katakana
           || (cp >= 0x4E00 && cp <= 0x9FFF)  // CJK Unified Ideographs
           || (cp >= 0xAC00 && cp <= 0xD7AF); // Hangul Syllables
}

static void countAlpha(const ::vector<string> &wordList, size_t &latinCount, size_t &totalAlpha) {
    for (const auto &word : wordList) {
        const Utf8 *p = reinterpret_cast<const Utf8 *>(word.cstr());
        const Utf8 *bound = p + word.size();

        while (p < bound) {
            Unicode cp = ReadUtf8(&p, bound);
            if (cp == ReplacementCharacter)
                continue;

            if (isLatinAlpha(cp)) {
                ++latinCount;
                ++totalAlpha;
            } else if (isNonLatinAlpha(cp)) {
                ++totalAlpha;
            }
        }
    }
}

static const char *englishStopWords[] = {"the",  "of", "and",  "to",   "in",  "is", "for",
                                         "that", "it", "was",  "on",   "are", "as", "with",
                                         "be",   "at", "this", "from", "or",  "an"};
static const size_t numStopWords = 20;

AlphaStats HtmlParser::alphaStats() const {
    size_t latinCount = 0;
    size_t totalAlpha = 0;

    countAlpha(words, latinCount, totalAlpha);
    countAlpha(titleWords, latinCount, totalAlpha);

    AlphaStats stats;
    stats.latin_alpha_count = static_cast<uint32_t>(latinCount);
    stats.total_alpha_count = static_cast<uint32_t>(totalAlpha);
    return stats;
}

uint32_t HtmlParser::outgoingAnchorWordCount() const {
    uint32_t total = 0;
    for (const auto &link : links) {
        total += static_cast<uint32_t>(link.anchorText.size());
    }
    return total;
}

bool HtmlParser::isEnglish(double threshold) const {
    // Tier 1: HTML lang attribute — most reliable, instant
    if (!htmlLang.empty()) {
        if (htmlLang.size() >= 2 && (htmlLang[0] == 'e' || htmlLang[0] == 'E') &&
            (htmlLang[1] == 'n' || htmlLang[1] == 'N') &&
            (htmlLang.size() == 2 || htmlLang[2] == '-'))
            return true;
        return false;
    }

    // Tier 2: English stop word frequency — distinguishes English from other Latin-script languages
    size_t totalWords = words.size();
    if (totalWords >= 50) {
        size_t hits = 0;
        for (const auto &word : words) {
            for (size_t i = 0; i < numStopWords; ++i) {
                if (strcasecmp(word.cstr(), englishStopWords[i]) == 0) {
                    ++hits;
                    break;
                }
            }
        }
        return static_cast<double>(hits) / totalWords >= 0.03;
    }

    // Tier 3: Latin character percentage — fallback for pages with few words
    AlphaStats stats = alphaStats();
    if (stats.total_alpha_count < 50)
        return true;
    return static_cast<double>(stats.latin_alpha_count) / stats.total_alpha_count >= threshold;
}
