#include "FrontierItemHelper.h"

#include <cstring>

Suffix stringToSuffix(const string &tld) {
    if (tld == "com")
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
    if (tld == "io")
        return Suffix::IO;
    if (tld == "dev")
        return Suffix::DEV;
    if (tld == "app")
        return Suffix::APP;
    // uk, de, fr, jp, ru, br, cn, au, ca, in, kr
    if (tld.size() == 2)
        return Suffix::CCTLD;
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
        return 2.5;
    case Suffix::MIL:
        return 2.5;
    case Suffix::INT:
        return 2.5;
    case Suffix::IO:
        return 2.0;
    case Suffix::DEV:
        return 2.0;
    case Suffix::APP:
        return 2.0;
    case Suffix::CCTLD:
        return 2.0;
    case Suffix::OTHER:
        return 1.0;
    }
    return 1.0;
}

double baseLengthScore(size_t len) {
    if (len < 3)
        return -1.0; // g.co, a.com
    if (len < 5)
        return -0.3; // short
    if (len <= 15)
        return 0.5; // google, stackoverflow, wikipedia
    if (len <= 25)
        return 0.0; // neutral
    return -0.5;    // auto generated, likely junk
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
    suffixOut = stringToSuffix(tld);

    if (secondLastDot == string::npos) {
        baseLengthOut = lastDot;
    } else {
        baseLengthOut = lastDot - secondLastDot - 1;
    }
}

size_t computePathDepth(const string &path) {
    if (path.size() == 0) {
        return 0;
    }

    size_t effectiveLen = path.size();
    // root path "/" and no path "" are equivalent
    if (effectiveLen >= 1 && path[effectiveLen - 1] == '/') {
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

size_t countQueryParams(const string &path) {
    size_t qPos = path.find('?');
    if (qPos == string::npos) {
        return 0;
    }
    size_t count = 1;
    for (size_t i = qPos + 1; i < path.size(); ++i) {
        if (path[i] == '&') {
            ++count;
        }
    }
    return count;
}

bool isLowValuePath(const string &path) {
    static const char *const patterns[] = {
        "login",    "logout",      "signup", "register", "signin",      "signout",
        "rss",      "feed",        "/api/",  "print",    "cgi-bin",     "wp-admin",
        "wp-login", "wp-includes", "cart",   "checkout", "unsubscribe", nullptr};

    for (size_t i = 0; patterns[i] != nullptr; ++i) {
        if (path.find(patterns[i]) != string::npos) {
            return true;
        }
    }
    return false;
}
