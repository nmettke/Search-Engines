#include "FrontierItemHelper.h"

Suffix stringToSuffix(const string &tld) {
    if (tld == "com" || tld == "com.au")
        return Suffix::COM;
    if (tld == "edu")
        return Suffix::EDU;
    if (tld == "gov")
        return Suffix::GOV;
    if (tld == "org")
        return Suffix::ORG;
    if (tld == "net")
        return Suffix::NET;
    if (tld == "mil")
        return Suffix::MIL;
    if (tld == "int")
        return Suffix::INT;
    // Regional TLDs map to ORG (institutional/community preference)
    if (tld == "co.uk" || tld == "ca" || tld == "uk" || tld == "au")
        return Suffix::ORG;
    return Suffix::OTHER;
}

double suffixScore(Suffix suffix) {
    switch (suffix) {
    case Suffix::EDU:
        return 4.0;
    case Suffix::GOV:
        return 4.0;
    case Suffix::ORG:
        return 3.0;
    case Suffix::COM:
        return 3.0;
    case Suffix::NET:
        return 2.0;
    case Suffix::MIL:
        return 2.0;
    case Suffix::INT:
        return 2.0;
    case Suffix::OTHER:
        return 0.0;
    }
    return 0.0;
}

void extractRest(const string &url, string &rest) {
    size_t schemePos = url.find("://");

    if (schemePos == string::npos) {
        rest = url;
        return;
    }

    for (size_t i = schemePos + 3; i < url.size(); ++i) {
        rest.pushBack(url[i]);
    }
}

void splitHostAndPath(const string &rest, string &host, string &path) {
    size_t slashPos = rest.find("/");

    if (slashPos == string::npos) {
        host = rest;
        return;
    }

    for (size_t i = 0; i < slashPos; ++i) {
        host.pushBack(rest[i]);
    }

    for (size_t i = slashPos; i < rest.size(); ++i) {
        path.pushBack(rest[i]);
    }
}

void parseHost(const string &host, Suffix &suffixOut, size_t &baseLengthOut) {
    suffixOut = Suffix::OTHER;
    baseLengthOut = 0;

    if (host.size() == 0) {
        return;
    }

    size_t lastDot = string::npos;
    size_t secondLastDot = string::npos;

    for (size_t i = host.size(); i > 0; --i) {
        if (host[i - 1] == '.') {
            if (lastDot == string::npos) {
                lastDot = i - 1;
            } else {
                secondLastDot = i - 1;
                break;
            }
        }
    }

    if (lastDot == string::npos) {
        baseLengthOut = host.size();
        return;
    }

    string tld;
    for (size_t i = lastDot + 1; i < host.size(); ++i) {
        tld.pushBack(host[i]);
    }

    // Handle two-part TLDs: check if this is a second-level TLD that needs the previous part
    if ((tld == "uk" || tld == "au" || tld == "ca") && secondLastDot != string::npos) {
        // Extract the two-part TLD (e.g., "co.uk" from "example.co.uk")
        string tld2;
        for (size_t i = secondLastDot + 1; i < host.size(); ++i) {
            tld2.pushBack(host[i]);
        }
        suffixOut = stringToSuffix(tld2);
        if (secondLastDot > 0) {
            size_t thirdLastDot = string::npos;
            for (size_t i = secondLastDot; i > 0; --i) {
                if (host[i - 1] == '.') {
                    thirdLastDot = i - 1;
                    break;
                }
            }
            baseLengthOut =
                thirdLastDot == string::npos ? secondLastDot : secondLastDot - thirdLastDot - 1;
        } else {
            baseLengthOut = secondLastDot;
        }
    } else {
        suffixOut = stringToSuffix(tld);
        if (secondLastDot == string::npos) {
            baseLengthOut = lastDot;
        } else {
            baseLengthOut = lastDot - secondLastDot - 1;
        }
    }
}

size_t computePathDepth(const string &path) {
    if (path.size() == 0) {
        return 0;
    }

    size_t effectiveLen = path.size();
    if (effectiveLen > 1 && path[effectiveLen - 1] == '/') {
        --effectiveLen;
    }

    size_t depth = 0;
    for (size_t i = 0; i < effectiveLen; ++i) {
        if (path[i] == '/') {
            ++depth;
        }
    }

    return depth;
}

double scorePathPatterns(const string &path) {
    double score = 0.0;

    if (path.size() == 0) {
        return score;
    }

    string lowerPath;
    for (size_t i = 0; i < path.size(); ++i) {
        char c = path[i];
        lowerPath.pushBack((c >= 'A' && c <= 'Z') ? (c - 'A' + 'a') : c);
    }

    // Boost home pages
    if (path == "/" || path == "/index.html") {
        score += 2.0;
        return score;
    }

    // Penalize meta pages
    if (lowerPath.find("/search") != string::npos || lowerPath.find("/filter") != string::npos ||
        lowerPath.find("/tag/") != string::npos || lowerPath.find("/category/") != string::npos ||
        lowerPath.find("/archive/") != string::npos || lowerPath.find("/date/") != string::npos) {
        score -= 1.5;
    }

    // Penalize admin/auth pages
    if (lowerPath.find("/admin") != string::npos || lowerPath.find("/login") != string::npos ||
        lowerPath.find("/account") != string::npos || lowerPath.find("/settings") != string::npos) {
        score -= 2.0;
    }

    // Penalize ads/tracking
    if (lowerPath.find("/ads/") != string::npos || lowerPath.find("/tracking/") != string::npos ||
        lowerPath.find("/analytics/") != string::npos) {
        score -= 3.0;
    }

    // Boost content pages
    if (lowerPath.find("/blog") != string::npos || lowerPath.find("/article") != string::npos ||
        lowerPath.find("/news") != string::npos || lowerPath.find("/tutorial") != string::npos ||
        lowerPath.find("/guide") != string::npos || lowerPath.find("/docs") != string::npos ||
        lowerPath.find("/post") != string::npos) {
        score += 1.0;
    }

    return score;
}

double scoreQueryStringComplexity(const string &url) {
    // Find query string start
    size_t queryPos = url.find("?");
    if (queryPos == string::npos) {
        return 0.0; // No query string
    }

    string queryString;
    for (size_t i = queryPos + 1; i < url.size(); ++i) {
        queryString.pushBack(url[i]);
    }

    // Count params (number of & characters + 1)
    size_t paramCount = 1;
    for (size_t i = 0; i < queryString.size(); ++i) {
        if (queryString[i] == '&') {
            ++paramCount;
        }
    }

    // Penalize 3+ params or long query string
    if (paramCount >= 3 || queryString.size() > 100) {
        return -0.5;
    }

    return 0.0;
}

double scoreSubdomainSignals(const string &host) {
    string lowerHost;
    for (size_t i = 0; i < host.size(); ++i) {
        char c = host[i];
        lowerHost.pushBack((c >= 'A' && c <= 'Z') ? (c - 'A' + 'a') : c);
    }

    // Penalize tracking/ad/mail subdomains
    if (lowerHost.find("ads.") == 0 || lowerHost.find("tracking.") == 0 ||
        lowerHost.find("mail.") == 0 || lowerHost.find("smtp.") == 0 ||
        lowerHost.find("ftp.") == 0) {
        return -2.0;
    }

    // Neutral for www, en, blog, news subdomains
    return 0.0;
}