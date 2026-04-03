// File stub for crawler orchestrator file
#include "Crawler.h"
#include <unordered_set>

struct StringHash {
    std::size_t operator()(const string &s) const noexcept {
        // 64-bit FNV-1a hash over custom string bytes
        std::size_t hash = 1469598103934665603ull;
        for (std::size_t i = 0; i < s.size(); ++i) {
            hash ^= static_cast<unsigned char>(s[i]);
            hash *= 1099511628211ull;
        }
        return hash;
    }
};

int main() {

    Frontier f("src/crawler/seedList.txt");
    std::unordered_set<string, StringHash> searching;
    while (!f.empty()) {
        std::optional<FrontierItem> item = f.pop();
        if (item) {
            string page = readURL(item->link);
            HtmlParser parsed(page.cstr(), page.size());
            for (const Link &link : parsed.links) {
                if (link.URL.find("http") != std::string::npos &&
                    searching.find(link.URL) == searching.end()) {
                    f.push(link.URL);
                    searching.insert(link.URL);
                }
            }
            std::cout << "Crawled " << item->link << std::endl;
        }
    }
}