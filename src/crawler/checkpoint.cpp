#include "checkpoint.h"
#include "index/src/lib/indexQueue.h"
#include "utils/threads/lock_guard.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <unistd.h>

Checkpoint::Checkpoint(const CheckpointConfig &config) : config_(config) {}

string Checkpoint::filePath() const { return config_.directory + "/checkpoint.dat"; }

string Checkpoint::tmpPath() const { return config_.directory + "/checkpoint.dat.tmp"; }

bool Checkpoint::save(const Frontier &frontier, const UrlBloomFilter &bloom, size_t urlsCrawled,
                      const IndexQueue *indexQueue) {
    lock_guard guard(saveMutex_);
    vector<FrontierItem> items = frontier.snapshot();

    FILE *f = fopen(tmpPath().c_str(), "wb");
    if (!f) {
        std::cerr << "Checkpoint: failed to open temp file\n";
        return false;
    }

    fprintf(f, "[HEADER]\n");
    fprintf(f, "version=2\n");
    fprintf(f, "urls_crawled=%zu\n", urlsCrawled);
    fprintf(f, "frontier_count=%zu\n", items.size());

    fprintf(f, "[FRONTIER]\n");
    for (const auto &item : items) {
        string line = item.serializeToLine();
        fprintf(f, "%s\n", line.c_str());
    }

    fprintf(f, "[BLOOM]\n");
    bloom.serializeToStream(f);

    if (indexQueue) {
        vector<HtmlParser> queueItems = indexQueue->snapshot();
        fprintf(f, "[INDEX_QUEUE]\n");
        fprintf(f, "%zu\n", queueItems.size());
        for (size_t i = 0; i < queueItems.size(); ++i) {
            queueItems[i].serializeToStream(f);
        }
    }

    fflush(f);
    fsync(fileno(f));
    fclose(f);

    if (rename(tmpPath().c_str(), filePath().c_str()) != 0) {
        std::cerr << "Checkpoint: rename failed\n";
        return false;
    }

    std::cerr << "Checkpoint saved at " << urlsCrawled << " URLs (" << items.size()
              << " frontier items)\n";
    return true;
}

bool Checkpoint::load(vector<FrontierItem> &items, UrlBloomFilter &bloom, size_t &urlsCrawled,
                      IndexQueue *indexQueue) {
    FILE *f = fopen(filePath().c_str(), "rb");
    if (!f)
        return false;

    char lineBuf[8192];
    size_t frontierCount = 0;
    urlsCrawled = 0;

    fgets(lineBuf, sizeof(lineBuf), f);

    while (fgets(lineBuf, sizeof(lineBuf), f)) {
        size_t len = strlen(lineBuf);
        if (len > 0 && lineBuf[len - 1] == '\n')
            lineBuf[len - 1] = '\0';

        if (strcmp(lineBuf, "[FRONTIER]") == 0)
            break;

        if (strncmp(lineBuf, "urls_crawled=", 13) == 0)
            urlsCrawled = atol(lineBuf + 13);
        else if (strncmp(lineBuf, "frontier_count=", 15) == 0)
            frontierCount = atol(lineBuf + 15);
    }

    items.reserve(frontierCount);
    for (size_t i = 0; i < frontierCount; ++i) {
        if (!fgets(lineBuf, sizeof(lineBuf), f))
            break;
        size_t len = strlen(lineBuf);
        if (len > 0 && lineBuf[len - 1] == '\n')
            lineBuf[len - 1] = '\0';
        items.pushBack(FrontierItem::deserializeFromLine(string(lineBuf)));
    }

    fgets(lineBuf, sizeof(lineBuf), f);
    bloom = UrlBloomFilter::deserializeFromStream(f);

    // Try to read index queue section (may not exist in older checkpoints)
    if (indexQueue && fgets(lineBuf, sizeof(lineBuf), f)) {
        size_t len = strlen(lineBuf);
        if (len > 0 && lineBuf[len - 1] == '\n')
            lineBuf[len - 1] = '\0';
        if (strcmp(lineBuf, "[INDEX_QUEUE]") == 0) {
            fgets(lineBuf, sizeof(lineBuf), f);
            size_t queueCount = atol(lineBuf);
            for (size_t i = 0; i < queueCount; ++i) {
                HtmlParser hp = HtmlParser::deserializeFromStream(f);
                indexQueue->push(hp);
            }
        }
    }

    fclose(f);
    return true;
}
