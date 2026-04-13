#include "url_features.h"
#include <algorithm>
#include <climits>

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

    size_t fragment_pos = path.find('#');
    if (fragment_pos != ::string::npos && fragment_pos < question_pos) {
        return 0;
    }

    size_t query_end = path.size();
    if (fragment_pos != ::string::npos && fragment_pos > question_pos) {
        query_end = fragment_pos;
    }

    size_t param_count = 0;
    size_t segment_start = question_pos + 1;
    for (size_t i = question_pos + 1; i <= query_end; ++i) {
        if (i == query_end || path[i] == '&') {
            if (i > segment_start) {
                ++param_count;
            }
            segment_start = i + 1;
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

uint8_t urlBaseDomainLength(const ParsedUrl &parsed) {
    return static_cast<uint8_t>(
        std::min(parsed.base_domain_length, static_cast<size_t>(UINT8_MAX)));
}

uint16_t urlPathLength(const ParsedUrl &parsed) {
    return static_cast<uint16_t>(std::min(parsed.path.size(), static_cast<size_t>(UINT16_MAX)));
}

uint8_t urlPathDepth(const ParsedUrl &parsed) {
    return static_cast<uint8_t>(std::min(parsed.path_depth, static_cast<size_t>(UINT8_MAX)));
}

uint8_t urlQueryParamCount(const ParsedUrl &parsed) {
    return static_cast<uint8_t>(std::min(parsed.query_param_count, static_cast<size_t>(UINT8_MAX)));
}

uint8_t urlNumericPathCharCount(const ParsedUrl &parsed) {
    return static_cast<uint8_t>(
        std::min(parsed.numeric_path_char_count, static_cast<size_t>(UINT8_MAX)));
}

uint8_t urlDomainHyphenCount(const ParsedUrl &parsed) {
    return static_cast<uint8_t>(
        std::min(parsed.domain_hyphen_count, static_cast<size_t>(UINT8_MAX)));
}

bool urlHasHttps(const ::string &url) {
    return url.size() >= 8 && url[0] == 'h' && url[1] == 't' && url[2] == 't' && url[3] == 'p' &&
           url[4] == 's' && url[5] == ':' && url[6] == '/' && url[7] == '/';
}
