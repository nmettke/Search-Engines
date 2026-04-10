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

TldBucket urlTldBucket(const ParsedUrl &parsed);
uint32_t urlBaseDomainLength(const ParsedUrl &parsed);
uint32_t urlPathLength(const ParsedUrl &parsed);
uint32_t urlPathDepth(const ParsedUrl &parsed);
uint32_t urlQueryParamCount(const ParsedUrl &parsed);
uint32_t urlNumericPathCharCount(const ParsedUrl &parsed);
uint32_t urlDomainHyphenCount(const ParsedUrl &parsed);
bool urlHasHttps(const ::string &url);

double urlTldScoreForTld(const ::string &tld);
double urlTldScore(TldBucket bucket);
double urlPathDepthScore(uint32_t path_depth);
double urlLengthScore(uint32_t url_length);
double urlHttpsScore(bool is_https);
double urlQueryParamScore(uint32_t query_param_count);
double urlNumericDensityScore(uint32_t numeric_path_char_count, uint32_t path_length);

double urlTldScore(const ParsedUrl &parsed);
double urlPathDepthScore(const ParsedUrl &parsed);
double urlNumericDensityScore(const ParsedUrl &parsed);

double urlTldScore(const ::string &url);
double urlPathDepthScore(const ::string &url);
double urlLengthScore(const ::string &url);
double urlHttpsScore(const ::string &url);
double urlQueryParamScore(const ::string &url);
double urlNumericDensityScore(const ::string &url);
