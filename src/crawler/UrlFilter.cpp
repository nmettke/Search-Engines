#include "UrlFilter.h"
#include <cctype>
#include <fstream>
#include <iostream>

void UrlFilter::loadBlacklist(const char *path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "Warning: could not open blacklist at " << path << '\n';
        return;
    }

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty())
            continue;
        string entry(line.c_str());
        addBlockedDomain(entry);
    }

    std::cerr << "Loaded " << blockedHosts.size() << " blacklisted domains\n";
}

void UrlFilter::addBlockedDomain(const string &entry) {
    string host = extractHost(entry);
    if (host.empty())
        host = entry;
    blockedHosts.pushBack(host);
}

bool UrlFilter::isAllowed(const string &url) const {
    if (!hasCrawlableExtension(url))
        return false;

    // Reject heavily percent-encoded URLs before host policy checks.
    static constexpr size_t kMaxPercentSigns = 8;
    static constexpr size_t kMinTailLengthForDensity = 24;
    static constexpr double kMaxPercentDensity = 0.10;

    size_t schemeEnd = url.find("://");
    if (schemeEnd != string::npos) {
        string rest = url.substr(schemeEnd + 3);
        size_t hostEnd = rest.find_first_of("/?");
        if (hostEnd != string::npos) {
            string tail = rest.substr(hostEnd);
            size_t percentCount = 0;
            for (size_t i = 0; i < tail.size(); ++i) {
                if (tail[i] == '%') {
                    ++percentCount;
                }
            }

            if (percentCount > kMaxPercentSigns) {
                if (tail.size() < kMinTailLengthForDensity) {
                    return false;
                }
                double density =
                    static_cast<double>(percentCount) / static_cast<double>(tail.size());
                if (density > kMaxPercentDensity) {
                    return false;
                }
            }
        }
    }

    string host = extractHost(url);
    if (host.empty())
        return true;

    for (size_t i = 0; i < blockedHosts.size(); ++i) {
        if (hostMatchesDomain(host, blockedHosts[i]))
            return false;
    }

    return true;
}

static const string allowedExtensions[] = {
    ".html", ".htm", ".shtml", ".xhtml", ".php", ".asp", ".aspx", ".jsp", ".jspx", ".cfm",
    ".cgi",  ".pl",  ".py",    ".txt",   ".xml", ".rss", ".atom", ".csv", ".tsv",
};
static constexpr size_t allowedExtCount = sizeof(allowedExtensions) / sizeof(allowedExtensions[0]);

static bool endsWithCaseInsensitive(const string &str, const string &suffix) {
    if (suffix.size() > str.size())
        return false;
    size_t offset = str.size() - suffix.size();
    for (size_t i = 0; i < suffix.size(); ++i) {
        if (tolower(static_cast<unsigned char>(str[offset + i])) !=
            tolower(static_cast<unsigned char>(suffix[i])))
            return false;
    }
    return true;
}

bool UrlFilter::hasCrawlableExtension(const string &url) {
    size_t schemeEnd = url.find("://");
    if (schemeEnd == string::npos)
        return true;

    size_t pathStart = string::npos;
    for (size_t i = schemeEnd + 3; i < url.size(); ++i) {
        if (url[i] == '/') {
            pathStart = i;
            break;
        }
    }
    if (pathStart == string::npos)
        return true;

    size_t pathEnd = url.size();
    for (size_t i = pathStart; i < url.size(); ++i) {
        if (url[i] == '?' || url[i] == '#') {
            pathEnd = i;
            break;
        }
    }

    // find the last '.' after the last '/' in the path to get the extension (no dot meaning no
    // extension which is allowed)
    size_t lastSlash = pathStart;
    for (size_t i = pathStart + 1; i < pathEnd; ++i) {
        if (url[i] == '/')
            lastSlash = i;
    }

    bool hasDot = false;
    for (size_t i = lastSlash + 1; i < pathEnd; ++i) {
        if (url[i] == '.') {
            hasDot = true;
            break;
        }
    }

    if (!hasDot)
        return true;

    string path = url.substr(pathStart, pathEnd - pathStart);

    for (size_t i = 0; i < allowedExtCount; ++i) {
        if (endsWithCaseInsensitive(path, allowedExtensions[i]))
            return true;
    }
    return false;
}

string UrlFilter::extractHost(const string &url) {
    size_t schemeEnd = url.find("://");
    if (schemeEnd == string::npos)
        return "";

    size_t hostStart = schemeEnd + 3;
    size_t hostEnd = url.size();
    for (size_t i = hostStart; i < url.size(); ++i) {
        if (url[i] == '/' || url[i] == ':' || url[i] == '?' || url[i] == '#') {
            hostEnd = i;
            break;
        }
    }

    if (hostEnd <= hostStart)
        return "";

    string host = url.substr(hostStart, hostEnd - hostStart);
    for (size_t i = 0; i < host.size(); ++i)
        host[i] = static_cast<char>(tolower(static_cast<unsigned char>(host[i])));
    return host;
}

bool UrlFilter::hostMatchesDomain(const string &host, const string &domain) {
    if (host == domain)
        return true;

    if (host.size() > domain.size() + 1) {
        size_t offset = host.size() - domain.size();
        if (host[offset - 1] == '.') {
            for (size_t i = 0; i < domain.size(); ++i) {
                if (host[offset + i] != domain[i])
                    return false;
            }
            return true;
        }
    }

    return false;
}
