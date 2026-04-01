#include <fstream>
#include <iostream>

#include "../parser/HtmlParser.h"
#include "../utils/SSL/LinuxSSL_Crawler.hpp"
#include "../utils/string.hpp"
#include "../utils/vector.hpp"

vector<string> links;

const bool debug = false;

int main() {
    std::ifstream seedList("src/crawler/seedList.txt");
    if (!seedList.is_open()) {
        std::cerr << "Failed to open file\n";
        return 1;
    }

    // std::string allowed here only for getline
    std::string line;

    while (std::getline(seedList, line)) {
        links.emplaceBack(string(line.c_str()));
    }

    for (const string &link : links) {
        std::cout << link << '\n';
    }

    const uint32_t count = 50;

    for (size_t i = 0; i < count && i < links.size(); i++) {
        if (debug) {
            std::cout << "Searching " << links[i] << std::endl;
        }

        // readURL already returns your string
        string page = readURL(links[i]); //Need to fix conversion between string and std::string
        //string page = "";

        HtmlParser parsed(page.cstr(), page.size());

        if (debug) {
            std::cout << "Searched " << links[i] << std::endl;
        }

        for (const Link &link : parsed.links) {
            // parser still uses std::string
            if (link.URL.find("http") != std::string::npos) {
                links.pushBack(string(link.URL.c_str()));

                if (debug) {
                    std::cout << "Found " << link.URL << std::endl;
                }
            }
        }
    }

    for (const string &link : links) {
        std::cout << link << std::endl;
    }
}