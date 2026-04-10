#include "RobotsCache.h"
#include "../utils/SSL/LinuxSSL_Crawler.hpp"
#include <cstring>
#include <time.h>

static const char *CRAWLER_USER_AGENT = "LinuxGetSsl";
static const int64_t DEFAULT_CRAWL_DELAY_MS = 500;

uint64_t RobotsCache::hashCString(const char *key) {
    uint64_t h = 0;
    while (*key) {
        h = h * 37 + *key;
        key++;
    }
    return h;
}

bool RobotsCache::compareCStrings(const char *a, const char *b) { return strcmp(a, b) == 0; }

RobotsCache::RobotsCache() : cache(compareCStrings, hashCString, 512) {}

RobotsCache::~RobotsCache() {
    HashTable<const char *, CacheEntry>::Iterator it = cache.begin();
    HashTable<const char *, CacheEntry>::Iterator end = cache.end();
    while (it != end) {
        delete[] it->key;
        delete it->value.robots;
        ++it;
    }
}

string RobotsCache::extractOrigin(const string &url) {
    size_t schemeEnd = url.find("://");
    if (schemeEnd == string::npos)
        return "";

    size_t hostStart = schemeEnd + 3;
    for (size_t i = hostStart; i < url.size(); i++) {
        if (url[i] == '/')
            return url.substr(0, i);
    }

    return url;
}

int64_t RobotsCache::nowMillis() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

RobotsTxt *RobotsCache::fetchRobotsTxt(const string &origin) {
    string robotsUrl = origin + "/robots.txt";

    string content = readURL(robotsUrl);
    if (content.empty())
        return nullptr;

    return new RobotsTxt((const Utf8 *)content.cstr(), content.size());
}

static char *dupCString(const char *src) {
    size_t len = strlen(src);
    char *copy = new char[len + 1];
    memcpy(copy, src, len + 1);
    return copy;
}

RobotCheckResult RobotsCache::checkAndReserve(const string &url) {
    string origin = extractOrigin(url);
    if (origin.empty())
        return {RobotCheckStatus::ALLOWED, 0};

    const char *originKey = origin.cstr();

    Tuple<const char *, CacheEntry> *entry = nullptr;
    RobotsTxt *robotsPtr = nullptr;

    // Step 1: try to find in cache
    {
        lock_guard<mutex> guard(cacheMutex);
        entry = cache.Find(originKey);
        if (entry) {
            robotsPtr = entry->value.robots;
        }
    }

    if (!entry) {
        RobotsTxt *robots = fetchRobotsTxt(origin);

        lock_guard<mutex> guard(cacheMutex);
        char *heapKey = dupCString(originKey);
        CacheEntry initial = {nullptr, 0};
        entry = cache.Find(heapKey, initial);

        if (entry->key != heapKey) {
            // another thread inserted this origin while we were fetching
            delete[] heapKey;
            if (robots)
                delete robots;
        } else {
            entry->value.robots = robots;
        }
        robotsPtr = entry->value.robots;
    }

    int crawlDelay = 0;
    bool allowed = true;
    if (robotsPtr) {
        allowed = robotsPtr->UrlAllowed((const Utf8 *)CRAWLER_USER_AGENT, (const Utf8 *)url.cstr(),
                                        &crawlDelay);
    }

    if (!allowed) {
        return {RobotCheckStatus::DISALLOWED, 0};
    }

    int64_t now = nowMillis();
    int64_t delayMs = (int64_t)crawlDelay * 1000;
    if (delayMs < DEFAULT_CRAWL_DELAY_MS)
        delayMs = DEFAULT_CRAWL_DELAY_MS;

    {
        lock_guard<mutex> guard(cacheMutex);
        int64_t last = entry->value.lastAccessedMs;
        if (last != 0 && now < last + delayMs) {
            return {RobotCheckStatus::DELAYED, last + delayMs};
        }
        entry->value.lastAccessedMs = now;
    }

    return {RobotCheckStatus::ALLOWED, 0};
}
