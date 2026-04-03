#include <fstream>
#include <iostream>

#include "../parser/HtmlParser.h"
#include "../utils/SSL/LinuxSSL_Crawler.hpp"
#include "../utils/string.hpp"
#include "../utils/vector.hpp"
#include "url_dedup.h"
#include <fstream>
#include <iostream>
#include <vector>

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
    UrlBloomFilter bloom(1000000, 0.0001);
    while (std::getline(seedList, line)) {
        std::string canonical;
        if (shouldEnqueueUrl(line, bloom, canonical)) {
            links.emplace_back(canonical);
        }
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

    for (const string &link : links) {
        std::cout << link << std::endl;
    }
}