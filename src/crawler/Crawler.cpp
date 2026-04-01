// File stub for crawler orchestrator file
#include "Crawler.h"

int main() {
    Frontier f("src/crawler/seedList.txt");

    while (!f.empty()) {
        std::optional<FrontierItem> item = f.pop();
        if (item) {
            string page = readURL(item->link);
            HtmlParser parsed(page.cstr(), page.size());
            for (const Link &link : parsed.links) {
                if (link.URL.find("http") != std::string::npos) {
                    f.push(link.URL);
                }
            }
            std::cout << "Crawled " << item->link << std::endl;
        }
    }
}