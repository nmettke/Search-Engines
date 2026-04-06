// Dry-run ranking: scores each seed URL as a root FrontierItem (seedDistance 0) and
// writes descending score order. Matches priority used by FrontierItemCompare / PriorityQueue.

#include "src/crawler/FrontierItem.h"
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

static void stripCr(std::string &line) {
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
        line.pop_back();
    }
}

int main(int argc, char **argv) {
    const char *inPath = argc > 1 ? argv[1] : "src/crawler/seedList.txt";
    const char *outPath = argc > 2 ? argv[2] : "src/crawler/seed_ranking.txt";

    std::ifstream in(inPath);
    if (!in) {
        std::cerr << "rank_seedlist: cannot open input " << inPath << '\n';
        return 1;
    }

    std::vector<FrontierItem> items;
    std::string line;
    while (std::getline(in, line)) {
        stripCr(line);
        if (line.empty()) {
            continue;
        }
        items.emplace_back(string(line.c_str()));
    }

    std::sort(items.begin(), items.end(), [](const FrontierItem &a, const FrontierItem &b) {
        const double sa = a.getScore();
        const double sb = b.getScore();
        if (sa != sb) {
            return sa > sb;
        }
        return a.link < b.link;
    });

    std::ofstream out(outPath);
    if (!out) {
        std::cerr << "rank_seedlist: cannot write " << outPath << '\n';
        return 1;
    }

    out << std::fixed << std::setprecision(4);
    out << "# rank\tscore\turl\n";
    for (size_t i = 0; i < items.size(); ++i) {
        out << (i + 1) << '\t' << items[i].getScore() << '\t' << items[i].link.c_str() << '\n';
    }

    std::cerr << "rank_seedlist: wrote " << items.size() << " rows to " << outPath << '\n';
    return 0;
}
