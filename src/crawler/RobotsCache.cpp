#include "RobotsCache.h"
#include "../utils/SSL/LinuxSSL_Crawler.hpp"
#include <cstring>

static const char *CRAWLER_USER_AGENT = "LinuxGetSsl";

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
    HashTable<const char *, RobotsTxt *>::Iterator it = cache.begin();
    HashTable<const char *, RobotsTxt *>::Iterator end = cache.end();
    while (it != end) {
        delete[] it->key;
        delete it->value;
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

bool RobotsCache::isAllowed(const string &url, int *crawlDelay) {
    string origin = extractOrigin(url);
    if (origin.empty())
        return true;

    const char *originKey = origin.cstr();

    {
        lock_guard<mutex> guard(cacheMutex);
        Tuple<const char *, RobotsTxt *> *entry = cache.Find(originKey);
        if (entry) {
            if (!entry->value)
                return true;
            return entry->value->UrlAllowed((const Utf8 *)CRAWLER_USER_AGENT,
                                            (const Utf8 *)url.cstr(), crawlDelay);
        }
    }

    RobotsTxt *robots = fetchRobotsTxt(origin);

    {
        lock_guard<mutex> guard(cacheMutex);
        // ase a heap alloced key so it outlives scope.
        char *heapKey = dupCString(originKey);
        Tuple<const char *, RobotsTxt *> *entry = cache.Find(heapKey, nullptr);

        if (entry->key != heapKey) {
            // another thread already inserted this origin while we were trying to fetch
            delete[] heapKey;
            if (robots) {
                delete robots;
                robots = entry->value;
            }
        } else {
            entry->value = robots;
        }
    }

    if (!robots)
        return true;

    return robots->UrlAllowed((const Utf8 *)CRAWLER_USER_AGENT, (const Utf8 *)url.cstr(),
                              crawlDelay);
}
