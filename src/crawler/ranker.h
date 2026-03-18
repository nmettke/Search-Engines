#pragma once
#include "../utils/string.hpp"

const static double suffixWeight = 1.0;       //.com, .gov, etc.
const static double lengthWeight = 1.0;       // base url length
const static double baseDistanceWeight = 1.0; // distance from the base url (amount of /s there are)
const static double seedDistanceWeight = 1.0; // How far are we from a seed link
const static double failureWeight = 1.0;      // Have we tried to visit this site before and failed?
const static double brokenSourceWeight =
    1.0;                                  // What's the quality of the site that came before this?
const static double languageWeight = 1.0; // What's the language of the site / the site it came
                                          // from?

enum Suffix { // I just took these off wikipedia
    COM = 1,
    EDU = 2,
    GOV = 3,
    ORG = 4,
    NET = 5,
    MIL = 6,
    INT = 7,
    OTHER = 8
};

Suffix stringToSuffix(const string &tld) {
    if (tld == "com")
        return COM;
    if (tld == "edu")
        return EDU;
    if (tld == "gov")
        return GOV;
    if (tld == "org")
        return ORG;
    if (tld == "net")
        return NET;
    if (tld == "mil")
        return MIL;
    if (tld == "int")
        return INT;

    // Fallback default
    return OTHER;
}

class CrawlerRanker {
  public:
    CrawlerRanker(string url) {
        // This here will assume all urls to be correctly formatted
        parseURL(url);
        seedDistance = 0;
    }

    CrawlerRanker(string url, const CrawlerRanker &parent) {
        parseURL(url);
        broken = parent.broken;
        english = parent.english;
        // Failed can never come from parent, everything else is passed down
    }

    void markBroken() { broken = true; }

    void markFailed() { failed = true; }

    void markLanguage() { english = false; }


    //This is me being an idiot

    // Suffix getSuffix() { return suffix; }
    // size_t getBaseLength() { return baseLength; }
    // size_t getSeedDistance() { return seedDistance; }
    // size_t getBaseDistance() { return baseDistance; }
    // bool getFailed() { return failed; }
    // bool getBroken() { return broken; }
    // bool getEnglish() { return english; }

    // double getWeight(){
    //     return (suffixWeight * suffix) + (lengthWeight * baseLength) + (seedDistanceWeight * seedDistance) + (baseDistanceWeight * baseDistance) + (failureWeight * failed) + (brokenSourceWeight * broken) + (languageWeight * english);
    // }

    double getWeight() const {
        return (suffixWeight * suffix) + (lengthWeight * baseLength) + (seedDistanceWeight * seedDistance) + (baseDistanceWeight * baseDistance) + (failureWeight * failed) + (brokenSourceWeight * broken) + (languageWeight * english);
    }

  private:
    // Weights. Assume hardcoded for now. We can adjust later
    Suffix suffix;
    size_t baseLength;
    size_t seedDistance;
    size_t baseDistance;
    bool failed = false; // By default, assume it didn't fail
    bool broken = false; // By default, broken should be set to false unless the parent has been
                         // marked as broken
    bool english = true; // By default, assume it's always english

    void parseURL(string url) {
        size_t schemePos = url.find("://");
        string rest;

        if (schemePos == string::npos) {
            rest = url;
        } else {
            // Substring for remainder of url.
            for (size_t i = schemePos + 3; i < url.size(); ++i) {
                rest.pushBack(url[i]);
            }
        }

        // Split into host and path
        //  host = up to first '/', path = from that '/' to end
        string host;
        string path;
        size_t slashPos = rest.find("/");
        if (slashPos == string::npos) {
            host = rest;
            // path stays empty
        } else {
            for (size_t i = 0; i < slashPos; ++i) {
                host.pushBack(rest[i]);
            }
            // path: [slashPos, end)
            for (size_t i = slashPos; i < rest.size(); ++i) {
                path.pushBack(rest[i]);
            }
        }

        // Find suffix
        suffix = OTHER;
        if (host.size() > 0) {
            // Find last dot
            size_t lastDot = string::npos;
            for (size_t i = host.size(); i > 0; --i) {
                if (host[i - 1] == '.') {
                    lastDot = i - 1;
                    break;
                }
            }

            if (lastDot != string::npos && lastDot + 1 < host.size()) {
                // tld = host substring after lastDot
                string tld;
                for (size_t i = lastDot + 1; i < host.size(); ++i) {
                    tld.pushBack(host[i]);
                }
                suffix = stringToSuffix(tld);
            }
        }

        // Find baseLength
        baseLength = 0;
        if (host.size() > 0) {
            // Find last dot (between domain and TLD)
            size_t lastDot = string::npos;
            for (size_t i = host.size(); i > 0; --i) {
                if (host[i - 1] == '.') {
                    lastDot = i - 1;
                    break;
                }
            }

            if (lastDot == string::npos) {
                // No dot, entire host is basename
                baseLength = host.size();
            } else {
                // Find previous dot. Isolate base specifically
                size_t secondLastDot = string::npos;
                if (lastDot > 0) {
                    for (size_t i = lastDot; i > 0; --i) {
                        if (host[i - 1] == '.') {
                            secondLastDot = i - 1;
                            break;
                        }
                    }
                }

                string baseDomain;
                if (secondLastDot == string::npos) {
                    // "google.com" baseDomain = host[0..lastDot)
                    for (size_t i = 0; i < lastDot; ++i) {
                        baseDomain.pushBack(host[i]);
                    }
                } else {
                    // "www.google.com" baseDomain = host[secondLastDot+1 .. lastDot)
                    for (size_t i = secondLastDot + 1; i < lastDot; ++i) {
                        baseDomain.pushBack(host[i]);
                    }
                }
                baseLength = baseDomain.size();
            }
        }

        // Find baseDistance
        baseDistance = 0;
        if (path.size() > 0) {
            size_t effectiveLen = path.size();
            if (effectiveLen > 1 && path[effectiveLen - 1] == '/') {
                --effectiveLen;
            }

            for (size_t i = 0; i < effectiveLen; ++i) {
                if (path[i] == '/') {
                    ++baseDistance;
                }
            }
        }
    }
};

bool operator<(const CrawlerRanker &lhs, const CrawlerRanker &rhs) {
    return lhs.getWeight() < rhs.getWeight();
}

bool operator<=(const CrawlerRanker &lhs, const CrawlerRanker &rhs) {
    return lhs.getWeight() <= rhs.getWeight();
}

bool operator>(const CrawlerRanker &lhs, const CrawlerRanker &rhs) {
    return lhs.getWeight() > rhs.getWeight();
}

bool operator>=(const CrawlerRanker &lhs, const CrawlerRanker &rhs) {
    return lhs.getWeight() >= rhs.getWeight();
}