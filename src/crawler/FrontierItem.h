#pragma once

#include "../utils/string.hpp"

enum class Suffix { COM, EDU, GOV, ORG, NET, MIL, INT, OTHER };

class FrontierItem {
  public:
    explicit FrontierItem(const string &url);
    FrontierItem(const string &url, const FrontierItem &parent);
    static FrontierItem withSeedDistance(const string &url, size_t seedDistance);
    FrontierItem withLink(const string &url) const;

    string serializeToLine() const;
    static FrontierItem deserializeFromLine(const string &line);

    void markFailed();

    double getScore() const;

    Suffix getSuffix() const { return suffix; }
    size_t getPathDepth() const { return pathDepth; }
    size_t getSeedDistance() const { return seedDistance; }

    string link;

  private:
    FrontierItem(const string &link, Suffix suffix, size_t baseLength, size_t seedDistance,
                 size_t pathDepth, bool failed);

    Suffix suffix = Suffix::OTHER;
    size_t baseLength = 0;
    size_t seedDistance = 0;
    size_t pathDepth = 0;

    bool failed = false;

    void parseURL(const string &url);
};

struct FrontierItemCompare {
    bool operator()(const FrontierItem &a, const FrontierItem &b) const;
};