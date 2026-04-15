// src/lib/tokenizer.cpp
#include "tokenizer.h"
#include "document_features.h"

bool Tokenizer::isAlphaNumeric(unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

::string Tokenizer::makeLowerCase(::string s) {
    for (char &c : s) {
        // Use ASCII property to avoid std::tolower cost
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c + 32);
    }
    return s;
}

::string Tokenizer::stripPunc(const ::string &s) {
    // Strip punctuations on the sides
    if (s.empty())
        return s;
    size_t i = 0;
    size_t j = s.size();
    while (i < j && !isAlphaNumeric(static_cast<unsigned char>(s[i])))
        ++i;
    while (j > i && !isAlphaNumeric(static_cast<unsigned char>(s[j - 1])))
        --j;
    return s.substr(i, j - i);
}

::vector<::string> Tokenizer::processToken(const ::string &raw) {
    // Process token: lowercase, strip puncutation, stemming, explode hyphen
    ::vector<::string> out;
    ::string lowered = makeLowerCase(raw);
    ::string stripped = stripPunc(lowered);
    if (stripped.empty())
        return out;

    // Add full word
    out.pushBack(PorterStemmer::stem(stripped));

    // Check hyphen, add left right word if exist
    const size_t hyphen = stripped.find('-');
    if (hyphen != ::string::npos && hyphen > 0 && hyphen + 1 < stripped.size()) {
        ::string left = stripped.substr(0, hyphen);
        ::string right = stripped.substr(hyphen + 1);
        if (!left.empty())
            out.pushBack(PorterStemmer::stem(left));
        if (!right.empty())
            out.pushBack(PorterStemmer::stem(right));
    }
    return out;
}

TokenizedDocument Tokenizer::processDocument(const HtmlParser &doc) {
    TokenizedDocument out;
    const uint32_t doc_start = next_location;
    uint32_t body_word_count = 0;
    DocumentFeatures features = extractDocumentFeatures(doc);

    for (const ::string &raw_word : doc.words) {
        auto expanded = processToken(raw_word);
        for (const auto &token : expanded) {
            out.tokens.push_back(TokenOutput{token, next_location});
        }
        ++body_word_count;
        ++next_location;
    }

    for (const ::string &raw_word : doc.titleWords) {
        auto expanded = processToken(raw_word);
        for (const auto &token : expanded) {
            out.tokens.push_back(TokenOutput{"$" + token, next_location});
        }
        ++next_location;
    }

    out.doc_end = DocEndOutput{next_location,   doc.documentUrl(),
                               body_word_count, static_cast<uint16_t>(doc.titleWords.size()),
                               doc_start,       doc.seedDistance,
                               features};
    ++next_location; // shift one to account for #DocEnd
    return out;
}
