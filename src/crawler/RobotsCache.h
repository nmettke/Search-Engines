#pragma once

#include "../utils/hash/HashTable.h"
#include "../utils/string.hpp"
#include "../utils/threads/lock_guard.hpp"
#include "../utils/threads/mutex.hpp"
#include "RobotsTxt.h"

#include <cstdint>

enum class RobotCheckStatus { ALLOWED, DELAYED, DISALLOWED };

struct RobotCheckResult {
    RobotCheckStatus status;
    int64_t readyAtMs;
};

class RobotsCache {
  public:
    RobotsCache();
    ~RobotsCache();

    RobotsCache(const RobotsCache &) = delete;
    RobotsCache &operator=(const RobotsCache &) = delete;

    // checks if we can access url and if so changes the last accessed timstamp
    RobotCheckResult checkAndReserve(const string &url);

  private:
    struct CacheEntry {
        RobotsTxt *robots;
        int64_t lastAccessedMs;
    };

    static uint64_t hashCString(const char *key);
    static bool compareCStrings(const char *a, const char *b);

    static string extractOrigin(const string &url);
    static int64_t nowMillis();

    RobotsTxt *fetchRobotsTxt(const string &origin);

    // keys are heap alloced c strings owned by cache
    HashTable<const char *, CacheEntry> cache;
    mutex cacheMutex;
};
