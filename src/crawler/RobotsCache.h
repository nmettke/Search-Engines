#pragma once

#include "../utils/HashTable.h"
#include "../utils/string.hpp"
#include "../utils/threads/lock_guard.hpp"
#include "../utils/threads/mutex.hpp"
#include "RobotsTxt.h"

class RobotsCache {
  public:
    RobotsCache();
    ~RobotsCache();

    RobotsCache(const RobotsCache &) = delete;
    RobotsCache &operator=(const RobotsCache &) = delete;

    bool isAllowed(const string &url, int *crawlDelay = nullptr);

  private:
    static uint64_t hashCString(const char *key);
    static bool compareCStrings(const char *a, const char *b);

    static string extractOrigin(const string &url);
    RobotsTxt *fetchRobotsTxt(const string &origin);

    // keys are heap alloced c strings owned by cache
    // values are RobotsTxt* (nullptr means fetch failed or no rules)
    HashTable<const char *, RobotsTxt *> cache;
    mutex cacheMutex;
};
