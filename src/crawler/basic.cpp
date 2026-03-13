#include "../parser/HtmlParser.h"
#include "../utils/SSL/LinuxSSL_Crawler.hpp"
#include "../utils/string.hpp"
#include "../utils/vector.hpp"
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
    while (std::getline(seedList, line)) {
        links.emplace_back(line);
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
                links.push_back(link.URL);
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