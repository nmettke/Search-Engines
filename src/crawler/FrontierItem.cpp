#include "FrontierItem.h"
#include "FrontierItemHelper.h"

constexpr double suffixWeight = 1.0;
constexpr double baseLengthWeight = 0.15;
constexpr double pathDepthWeight = 0.75;
constexpr double seedDistanceWeight = 1.0;
constexpr double failureWeight = 5.0;
constexpr double brokenSourceWeight = 3.0;
constexpr double nonEnglishWeight = 2.0;

FrontierItem::FrontierItem(const string &url) : seedDistance(0) { parseURL(url); }

FrontierItem::FrontierItem(const string &url, const FrontierItem &parent)
    : seedDistance(parent.seedDistance + 1), broken(parent.broken), english(parent.english) {
    parseURL(url);
}

void FrontierItem::markBroken() { broken = true; }

void FrontierItem::markFailed() { failed = true; }

void FrontierItem::markNonEnglish() { english = false; }

double FrontierItem::getScore() const {
    double score = 0.0;

    score += suffixWeight * suffixScore(suffix);

    score -= baseLengthWeight * static_cast<double>(baseLength);
    score -= pathDepthWeight * static_cast<double>(pathDepth);
    score -= seedDistanceWeight * static_cast<double>(seedDistance);

    if (failed) {
        score -= failureWeight;
    }
    if (broken) {
        score -= brokenSourceWeight;
    }
    if (!english) {
        score -= nonEnglishWeight;
    }

    return score;
}

void FrontierItem::parseURL(const string &url) {
    string rest;
    string host;
    string path;

    extractRest(url, rest);
    splitHostAndPath(rest, host, path);
    parseHost(host, suffix, baseLength);
    pathDepth = computePathDepth(path);
}

bool FrontierItemCompare::operator()(const FrontierItem &a, const FrontierItem &b) const {
    return a.getScore() < b.getScore();
}