#include "FrontierItem.h"
#include "FrontierItemHelper.h"
#include <cstdio>
#include <cstdlib>

constexpr double suffixWeight = 1.0;
constexpr double baseLengthWeight = 0.1;
constexpr double pathDepthWeight = 0.2;
constexpr double seedDistanceWeight = 3.0;
constexpr double failureWeight = 10.0;
constexpr double nonEnglishWeight = 1.5;

FrontierItem::FrontierItem(const string &url) : link(url), seedDistance(0) { parseURL(url); }

FrontierItem::FrontierItem(const string &url, const FrontierItem &parent)
    : link(url), seedDistance(parent.seedDistance + 1), broken(parent.broken), english(parent.english) {
    parseURL(url);
}

FrontierItem::FrontierItem(const string &link, Suffix suffix, size_t baseLength,
                           size_t seedDistance, size_t pathDepth, bool failed, bool broken,
                           bool english)
    : link(link), suffix(suffix), baseLength(baseLength), seedDistance(seedDistance),
      pathDepth(pathDepth), failed(failed), broken(broken), english(english) {}

string FrontierItem::serializeToLine() const {
    char buf[32];
    // Strip \r and \n from link to prevent line breaks in checkpoint file
    string cleanLink;
    for (size_t i = 0; i < link.size(); ++i) {
        if (link[i] != '\r' && link[i] != '\n')
            cleanLink.pushBack(link[i]);
    }
    string res = cleanLink;
    res += '|';
    snprintf(buf, sizeof(buf), "%d", static_cast<int>(suffix));
    res += buf;
    res += '|';
    snprintf(buf, sizeof(buf), "%zu", baseLength);
    res += buf;
    res += '|';
    snprintf(buf, sizeof(buf), "%zu", seedDistance);
    res += buf;
    res += '|';
    snprintf(buf, sizeof(buf), "%zu", pathDepth);
    res += buf;
    res += '|';
    res += failed ? '1' : '0';
    res += '|';
    res += broken ? '1' : '0';
    res += '|';
    res += english ? '1' : '0';
    return res;
}

static size_t findPipe(const string &line, size_t from) {
    for (size_t i = from; i < line.size(); ++i) {
        if (line[i] == '|')
            return i;
    }
    return line.size();
}

static string field(const string &line, size_t start, size_t end) {
    return string(line.c_str() + start, line.c_str() + end);
}

FrontierItem FrontierItem::deserializeFromLine(const string &line) {
    size_t start = 0;
    size_t pipe = findPipe(line, start);
    string link = field(line, start, pipe);

    start = pipe + 1;
    pipe = findPipe(line, start);
    Suffix suffix = static_cast<Suffix>(atol(field(line, start, pipe).c_str()));

    start = pipe + 1;
    pipe = findPipe(line, start);
    size_t baseLength = atol(field(line, start, pipe).c_str());

    start = pipe + 1;
    pipe = findPipe(line, start);
    size_t seedDistance = atol(field(line, start, pipe).c_str());

    start = pipe + 1;
    pipe = findPipe(line, start);
    size_t pathDepth = atol(field(line, start, pipe).c_str());

    start = pipe + 1;
    pipe = findPipe(line, start);
    bool failed = atol(field(line, start, pipe).c_str()) != 0;

    start = pipe + 1;
    pipe = findPipe(line, start);
    bool broken = atol(field(line, start, pipe).c_str()) != 0;

    start = pipe + 1;
    bool english = atol(field(line, start, line.size()).c_str()) != 0;

    return FrontierItem(link, suffix, baseLength, seedDistance, pathDepth, failed, broken, english);
}

void FrontierItem::markBroken() { broken = true; }

void FrontierItem::markFailed() { failed = true; }

void FrontierItem::markNonEnglish() { english = false; }

double FrontierItem::getScore() const {
    double score = 0.0;

    // Base scoring
    score += suffixWeight * suffixScore(suffix);

    score -= baseLengthWeight * static_cast<double>(baseLength);
    score -= pathDepthWeight * static_cast<double>(pathDepth);
    score -= seedDistanceWeight * static_cast<double>(seedDistance);

    if (failed) {
        score -= failureWeight;
    }
    if (!english) {
        score -= nonEnglishWeight;
    }

    // Apply new heuristics
    string rest;
    string host;
    string path;

    extractRest(link, rest);
    splitHostAndPath(rest, host, path);

    // Path pattern scoring
    score += scorePathPatterns(path);

    // Query string complexity
    score += scoreQueryStringComplexity(link);

    // Subdomain signals
    score += scoreSubdomainSignals(host);

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