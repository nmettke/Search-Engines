#pragma once

#include "utils/string.hpp"
#include "utils/vector.hpp"
#include <cstddef>

class UrlFilter {
  public:
    UrlFilter() = default;

    void loadBlacklist(const char *path);

    void addBlockedDomain(const string &entry);

    bool isAllowed(const string &url) const;

  private:
    vector<string> blockedHosts;

    static string extractHost(const string &url);
    static bool hostMatchesDomain(const string &host, const string &domain);
    static bool hasCrawlableExtension(const string &url);
};
