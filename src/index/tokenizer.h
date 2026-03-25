// This header includes code needed for tokenizing
#pragma once

#include "../parser/HtmlParser.h"
#include "types.h"

#include <string>
#include <vector>

struct TokenizedDocument {
    std::vector<TokenOutput> tokens;
    DocEndOutput doc_end;
};

class Tokenizer {
  public:
    Tokenizer(uint32_t base_location = 0) : next_location(base_location) {}
    TokenizedDocument processDocument(const HtmlParser &doc);

  private:
    static std::string makeLowerCase(std::string s);
    static std::string stripPunc(const std::string &s);
    static std::vector<std::string> processToken(const std::string &raw);
    static bool isAlphaNumeric(unsigned char c);

    uint32_t next_location = 0;
};

class PorterStemmer {
  public:
    static std::string stem(const std::string &token);
};