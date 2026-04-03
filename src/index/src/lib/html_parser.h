#pragma once

#include <string>
#include <vector>

class Link {
  public:
    std::string URL;
    std::vector<std::string> anchorText;

    Link(std::string URL) : URL(URL) {}
};

class HtmlParser {
  public:
    std::vector<std::string> words, titleWords;
    std::vector<Link> links;
    std::string base;

    HtmlParser() = default;
};