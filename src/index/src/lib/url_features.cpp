#include "url_features.h"

namespace {

char toLowerAscii(char c) {
    if (c >= 'A' && c <= 'Z') {
        return static_cast<char>(c + ('a' - 'A'));
    }
    return c;
}

::string lowerAscii(const ::string &value) {
    ::string lowered = value;
    for (char &ch : lowered) {
        ch = toLowerAscii(ch);
    }
    return lowered;
}

bool isAsciiDigit(char c) { return c >= '0' && c <= '9'; }

size_t computePathDepth(const ::string &path) {
    if (path.size() == 0) {
        return 0;
    }

    size_t path_end = path.size();
    size_t query_pos = path.find_first_of("?#");
    if (query_pos != ::string::npos) {
        path_end = query_pos;
    }

    if (path_end == 0) {
        return 0;
    }

    if (path_end > 1 && path[path_end - 1] == '/') {
        --path_end;
    }

    size_t depth = 0;
    for (size_t i = 0; i < path_end; ++i) {
        if (path[i] == '/') {
            ++depth;
        }
    }
    return depth;
}

size_t computeBaseDomainLength(const ::string &host) {
    if (host.size() == 0) {
        return 0;
    }

    size_t last_dot = ::string::npos;
    size_t second_last_dot = ::string::npos;
    for (size_t i = host.size(); i > 0; --i) {
        if (host[i - 1] == '.') {
            if (last_dot == ::string::npos) {
                last_dot = i - 1;
            } else {
                second_last_dot = i - 1;
                break;
            }
        }
    }

    if (last_dot == ::string::npos) {
        return host.size();
    }
    if (second_last_dot == ::string::npos) {
        return last_dot;
    }
    return last_dot - second_last_dot - 1;
}

size_t countQueryParams(const ::string &path) {
    size_t question_pos = path.find('?');
    if (question_pos == ::string::npos) {
        return 0;
    }

    size_t param_count = 1;
    for (size_t i = question_pos + 1; i < path.size(); ++i) {
        if (path[i] == '&') {
            ++param_count;
        }
    }
    return param_count;
}

size_t countNumericPathChars(const ::string &path) {
    size_t path_end = path.size();
    size_t query_pos = path.find_first_of("?#");
    if (query_pos != ::string::npos) {
        path_end = query_pos;
    }

    size_t digit_count = 0;
    for (size_t i = 0; i < path_end; ++i) {
        if (isAsciiDigit(path[i])) {
            ++digit_count;
        }
    }
    return digit_count;
}

size_t countChar(const ::string &value, char needle) {
    size_t count = 0;
    for (char ch : value) {
        if (ch == needle) {
            ++count;
        }
    }
    return count;
}

bool looksLikeIpv4Address(const ::string &host) {
    if (host.size() == 0) {
        return false;
    }

    uint32_t segment_count = 0;
    uint32_t segment_value = 0;
    uint32_t segment_length = 0;
    for (char ch : host) {
        if (isAsciiDigit(ch)) {
            segment_value = segment_value * 10 + static_cast<uint32_t>(ch - '0');
            ++segment_length;
            if (segment_length > 3 || segment_value > 255) {
                return false;
            }
            continue;
        }

        if (ch != '.') {
            return false;
        }

        if (segment_length == 0) {
            return false;
        }

        ++segment_count;
        segment_value = 0;
        segment_length = 0;
    }

    if (segment_length == 0) {
        return false;
    }

    ++segment_count;
    return segment_count == 4;
}

double normalizedTldScore(const ::string &tld) {
    if (tld.size() == 0) {
        return 0.0;
    }

    if (tld == "edu" || tld == "gov")
        return 1.0;

    if (tld == "com" || tld == "org")
        return 0.75;

    if (tld == "net" || tld == "io" || tld == "dev" || tld == "ai" || tld == "co" || tld == "app")
        return 0.5;

    if (tld == "us" || tld == "uk" || tld == "ca" || tld == "de" || tld == "au" || tld == "fr" ||
        tld == "jp" || tld == "mil" || tld == "int" || tld == "info" || tld == "me" || tld == "biz")
        return 0.35;

    return 0.15;
}

} // namespace

ParsedUrl parseUrl(const ::string &url) {
    ParsedUrl result;
    if (url.size() == 0) {
        return result;
    }

    size_t authority_start = 0;
    size_t scheme_pos = url.find("://");
    if (scheme_pos != ::string::npos) {
        authority_start = scheme_pos + 3;
    }

    ::string remainder = url.substr(authority_start);
    size_t host_end = remainder.find_first_of("/?#");
    if (host_end == ::string::npos) {
        result.host = remainder;
    } else {
        result.host = remainder.substr(0, host_end);
        result.path = remainder.substr(host_end);
    }

    size_t at_pos = ::string::npos;
    for (size_t i = result.host.size(); i > 0; --i) {
        if (result.host[i - 1] == '@') {
            at_pos = i - 1;
            break;
        }
    }
    if (at_pos != ::string::npos && at_pos + 1 < result.host.size()) {
        result.host = result.host.substr(at_pos + 1);
    }

    size_t port_pos = result.host.find(':');
    if (port_pos != ::string::npos) {
        result.host.erase(port_pos);
    }

    result.domain_hyphen_count = countChar(result.host, '-');
    result.is_ip_address = looksLikeIpv4Address(result.host);

    size_t last_dot = ::string::npos;
    for (size_t i = result.host.size(); i > 0; --i) {
        if (result.host[i - 1] == '.') {
            last_dot = i - 1;
            break;
        }
    }

    if (last_dot != ::string::npos && last_dot + 1 < result.host.size()) {
        result.tld = lowerAscii(result.host.substr(last_dot + 1));
    }

    result.base_domain_length = computeBaseDomainLength(result.host);
    result.path_depth = computePathDepth(result.path);
    result.query_param_count = countQueryParams(result.path);
    result.numeric_path_char_count = countNumericPathChars(result.path);
    return result;
}

double urlTldScoreForTld(const ::string &tld) { return normalizedTldScore(lowerAscii(tld)); }

double urlTldScore(TldBucket bucket) {
    switch (bucket) {
    case TldBucket::GovEdu:
        return 1.0;
    case TldBucket::ComOrg:
        return 0.75;
    case TldBucket::NetTech:
        return 0.5;
    case TldBucket::CountryOrKnownMisc:
    case TldBucket::Biz:
        return 0.35;
    case TldBucket::Other:
        return 0.15;
    case TldBucket::IpAddress:
    case TldBucket::None:
        return 0.0;
    }

    return 0.0;
}

double urlPathDepthScore(uint32_t path_depth) {
    if (path_depth <= 1) {
        return 1.0;
    }
    if (path_depth >= 8) {
        return 0.0;
    }
    return 1.0 - ((path_depth - 1) / 7.0);
}

double urlLengthScore(uint32_t url_length) {
    if (url_length == 0) {
        return 1.0;
    }

    if (url_length <= 30) {
        return 1.0;
    }
    if (url_length >= 200) {
        return 0.0;
    }
    return 1.0 - ((url_length - 30) / 170.0);
}

double urlHttpsScore(bool is_https) { return is_https ? 1.0 : 0.0; }

double urlQueryParamScore(uint32_t query_param_count) {
    if (query_param_count == 0) {
        return 1.0;
    }

    double score = 1.0 - (query_param_count * 0.15);
    if (score < 0.0) {
        return 0.0;
    }
    return score;
}

double urlNumericDensityScore(uint32_t numeric_path_char_count, uint32_t path_length) {
    if (path_length == 0) {
        return 1.0;
    }

    double ratio = static_cast<double>(numeric_path_char_count) / static_cast<double>(path_length);
    return 1.0 - ratio;
}

TldBucket urlTldBucket(const ParsedUrl &parsed) {
    if (parsed.is_ip_address) {
        return TldBucket::IpAddress;
    }
    if (parsed.tld.size() == 0) {
        return TldBucket::None;
    }
    if (parsed.tld == "edu" || parsed.tld == "gov") {
        return TldBucket::GovEdu;
    }
    if (parsed.tld == "com" || parsed.tld == "org") {
        return TldBucket::ComOrg;
    }
    if (parsed.tld == "net" || parsed.tld == "io" || parsed.tld == "dev" || parsed.tld == "ai" ||
        parsed.tld == "app") {
        return TldBucket::NetTech;
    }
    if (parsed.tld == "biz") {
        return TldBucket::Biz;
    }
    if (parsed.tld == "us" || parsed.tld == "uk" || parsed.tld == "ca" || parsed.tld == "de" ||
        parsed.tld == "au" || parsed.tld == "fr" || parsed.tld == "jp" || parsed.tld == "mil" ||
        parsed.tld == "int" || parsed.tld == "info" || parsed.tld == "me" || parsed.tld == "co") {
        return TldBucket::CountryOrKnownMisc;
    }
    return TldBucket::Other;
}

uint32_t urlBaseDomainLength(const ParsedUrl &parsed) {
    return static_cast<uint32_t>(parsed.base_domain_length);
}

uint32_t urlPathLength(const ParsedUrl &parsed) {
    return static_cast<uint32_t>(parsed.path.size());
}

uint32_t urlPathDepth(const ParsedUrl &parsed) { return static_cast<uint32_t>(parsed.path_depth); }

uint32_t urlQueryParamCount(const ParsedUrl &parsed) {
    return static_cast<uint32_t>(parsed.query_param_count);
}

uint32_t urlNumericPathCharCount(const ParsedUrl &parsed) {
    return static_cast<uint32_t>(parsed.numeric_path_char_count);
}

uint32_t urlDomainHyphenCount(const ParsedUrl &parsed) {
    return static_cast<uint32_t>(parsed.domain_hyphen_count);
}

bool urlHasHttps(const ::string &url) {
    if (url.size() < 8) {
        return false;
    }
    return url.substr(0, 8) == "https://";
}

double urlTldScore(const ParsedUrl &parsed) { return urlTldScoreForTld(parsed.tld); }

double urlPathDepthScore(const ParsedUrl &parsed) {
    return urlPathDepthScore(static_cast<uint32_t>(parsed.path_depth));
}

double urlNumericDensityScore(const ParsedUrl &parsed) {
    size_t path_len = parsed.path.size();
    size_t query_pos = parsed.path.find_first_of("?#");
    if (query_pos != ::string::npos) {
        path_len = query_pos;
    }
    return urlNumericDensityScore(static_cast<uint32_t>(parsed.numeric_path_char_count),
                                  static_cast<uint32_t>(path_len));
}

double urlTldScore(const ::string &url) { return urlTldScore(parseUrl(url)); }

double urlPathDepthScore(const ::string &url) { return urlPathDepthScore(parseUrl(url)); }

double urlLengthScore(const ::string &url) {
    return urlLengthScore(static_cast<uint32_t>(url.size()));
}

double urlHttpsScore(const ::string &url) { return urlHttpsScore(urlHasHttps(url)); }

double urlQueryParamScore(const ::string &url) {
    ParsedUrl parsed = parseUrl(url);
    return urlQueryParamScore(static_cast<uint32_t>(parsed.query_param_count));
}

double urlNumericDensityScore(const ::string &url) { return urlNumericDensityScore(parseUrl(url)); }
