// File stub for crawler orchestrator file
#include "Crawler.h"
#include "url_dedup.h"

int main() {
    vector<pthread_t> threads(ThreadCount);

    Frontier f("src/crawler/seedList.txt");
    UrlBloomFilter bloom(1000000, 0.0001);
    while (!f.empty()) {
        std::optional<FrontierItem> item = f.pop();
        if (item) {
            string page = readURL(item->link);
            HtmlParser parsed(page.cstr(), page.size());
            for (const Link &link : parsed.links) {
                if (link.URL.find("http") != link.URL.npos) {
                    string canonical;
                    if (shouldEnqueueUrl(link.URL, bloom, canonical)) {
                        f.push(canonical);
                    }
                }
            }
            std::cout << "Crawled " << item->link << std::endl;
        }
    }
}