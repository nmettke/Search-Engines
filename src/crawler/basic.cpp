#include "../parser/HtmlParser.h"
#include "../utils/SSL/LinuxSSL_Crawler.hpp"
#include "../utils/string.hpp"
#include "../utils/vector.hpp"
#include "url_dedup.h"
#include <fstream>
#include <iostream>
#include <vector>

std::vector<std::string> links;

const bool debug = false;

int main() {
    // Debug Config
    std::ifstream seedList("seedList.txt");
    if (!seedList.is_open()) {
        std::cerr << "Failed to open file\n";
        return 1;
    }
    std::string line;
    UrlBloomFilter bloom(1000000, 0.0001);
    while (std::getline(seedList, line)) {
        std::string canonical;
        if (shouldEnqueueUrl(line, bloom, canonical)) {
            links.emplace_back(canonical);
        }
    }
    for (const std::string &link : links) {
        std::cout << link << '\n';
    }

    const uint32_t count = 50;

    for (size_t i = 0; i < count && i < links.size(); i++) {
        if (debug) {
            std::cout << "Searching " << links[i] << std::endl;
        }
        std::string buffer = readURL(links[i]);
        HtmlParser parsed(buffer.c_str(), buffer.size());

        if (debug) {
            std::cout << "Searched " << links[i] << std::endl;
        }

        for (const Link &link : parsed.links) {
            if (link.URL.find("http") != link.URL.npos) {
                std::string canonical;
                if (shouldEnqueueUrl(link.URL, bloom, canonical)) {
                    links.push_back(canonical);
                }
                if (debug) {
                    std::cout << "Found " << link.URL << std::endl;
                }
            }
        }
    }

    for (const std::string &link : links) {
        std::cout << link << std::endl;
    }
}