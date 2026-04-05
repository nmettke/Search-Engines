#pragma once

#include "../utils/string.hpp"

enum class Suffix { COM, EDU, GOV, ORG, NET, MIL, INT, OTHER };

class FrontierItem {
  public:
    explicit FrontierItem(const string &url);
    FrontierItem(const string &url, const FrontierItem &parent);

    string serializeToLine() const;
    static FrontierItem deserializeFromLine(const string &line);

    void markBroken();
    void markFailed();
    void markNonEnglish();

    double getScore() const;

    string link;

  private:
    FrontierItem(const string &link, Suffix suffix, size_t baseLength, size_t seedDistance,
                 size_t pathDepth, bool failed, bool broken, bool english);

    Suffix suffix = Suffix::OTHER;
    size_t baseLength = 0;
    size_t seedDistance = 0;
    size_t pathDepth = 0;

    bool failed = false;
    bool broken = false;
    bool english = true;

    void parseURL(const string &url);
};

struct FrontierItemCompare {
    bool operator()(const FrontierItem &a, const FrontierItem &b) const;
};