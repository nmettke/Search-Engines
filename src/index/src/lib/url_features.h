#pragma once

#include "types.h"
#include "utils/string.hpp"

struct ParsedUrl {
    ::string host;
    ::string path;
    ::string tld;
    size_t path_depth = 0;
    size_t base_domain_length = 0;
    size_t query_param_count = 0;
    size_t numeric_path_char_count = 0;
    size_t domain_hyphen_count = 0;
    bool is_ip_address = false;
};

ParsedUrl parseUrl(const ::string &url);

uint8_t urlBaseDomainLength(const ParsedUrl &parsed);
uint16_t urlPathLength(const ParsedUrl &parsed);
uint8_t urlPathDepth(const ParsedUrl &parsed);
uint8_t urlQueryParamCount(const ParsedUrl &parsed);
uint8_t urlNumericPathCharCount(const ParsedUrl &parsed);
uint8_t urlDomainHyphenCount(const ParsedUrl &parsed);
bool urlHasHttps(const ::string &url);
