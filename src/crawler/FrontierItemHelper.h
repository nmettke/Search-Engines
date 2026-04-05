#pragma once

#include "../utils/string.hpp"
#include "FrontierItem.h"

Suffix stringToSuffix(const string &tld);

double suffixScore(Suffix suffix);

double baseLengthScore(size_t len);

void extractRest(const string &url, string &rest);

void splitHostAndPath(const string &rest, string &host, string &path);

void parseHost(const string &host, Suffix &suffixOut, size_t &baseLengthOut);

size_t computePathDepth(const string &path);

size_t countQueryParams(const string &path);

bool isLowValuePath(const string &path);
