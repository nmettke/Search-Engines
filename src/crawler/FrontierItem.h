#pragma once

#include "../utils/string.hpp"

enum class Suffix { COM, EDU, GOV, ORG, NET, MIL, INT, OTHER };

class FrontierItem {
  public:
    explicit FrontierItem(const string &url);
    FrontierItem(const string &url, const FrontierItem &parent);

    void markBroken();
    void markFailed();
    void markNonEnglish();

    double getScore() const;

  private:
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