#include "Crawler.h"
#include "checkpoint.h"
#include "url_dedup.h"
#include <csignal>
#include <iostream>

static volatile bool shouldStop = false;

static void signalHandler(int) { shouldStop = true; }

int main() {
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    CheckpointConfig cpConfig;
    cpConfig.directory = "src/crawler";
    cpConfig.interval = 500;
    Checkpoint checkpoint(cpConfig);

    std::vector<FrontierItem> recoveredItems;
    UrlBloomFilter bloom(1000000, 0.0001);
    std::size_t urlsCrawled = 0;

    Frontier *f;
    if (checkpoint.load(recoveredItems, bloom, urlsCrawled)) {
        f = new Frontier(std::move(recoveredItems));
        std::cerr << "Recovered from checkpoint at " << urlsCrawled << " URLs\n";
    } else {
        f = new Frontier("src/crawler/seedList.txt");
        std::cerr << "Starting fresh from seed list\n";
    }

    while (!f->empty() && !shouldStop) {
        std::optional<FrontierItem> item = f->pop();
        if (!item)
            continue;

        string page = readURL(item->link);
        HtmlParser parsed(page.cstr(), page.size());

        for (const Link &link : parsed.links) {
            string canonical;
            if (shouldEnqueueUrl(link.URL, bloom, canonical)) {
                f->push(canonical);
            }
        }

        ++urlsCrawled;
        std::cout << "Crawled [" << urlsCrawled << "] " << item->link << std::endl;

        if (checkpoint.shouldCheckpoint(urlsCrawled)) {
            checkpoint.save(*f, bloom, urlsCrawled);
        }
    }

    checkpoint.save(*f, bloom, urlsCrawled);

    if (shouldStop)
        std::cerr << "Graceful shutdown after SIGINT\n";

    delete f;
    return 0;
}
