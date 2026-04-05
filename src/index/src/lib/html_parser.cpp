#include "html_parser.h"
#include <cstdlib>
#include <cstring>

static void writeLine(FILE *f, const std::string &s) { fprintf(f, "%s\n", s.c_str()); }

static std::string readLine(FILE *f) {
    char buf[8192];
    if (!fgets(buf, sizeof(buf), f))
        return "";
    size_t len = strlen(buf);
    if (len > 0 && buf[len - 1] == '\n')
        buf[len - 1] = '\0';
    return std::string(buf);
}

void HtmlParser::serializeToStream(FILE *f) const {
    writeLine(f, base);
    fprintf(f, "%zu\n", words.size());
    for (const auto &w : words)
        writeLine(f, w);
    fprintf(f, "%zu\n", titleWords.size());
    for (const auto &w : titleWords)
        writeLine(f, w);
    fprintf(f, "%zu\n", links.size());
    for (const auto &link : links) {
        fprintf(f, "%s\t%zu", link.URL.c_str(), link.anchorText.size());
        for (const auto &a : link.anchorText)
            fprintf(f, "\t%s", a.c_str());
        fprintf(f, "\n");
    }
}

HtmlParser HtmlParser::deserializeFromStream(FILE *f) {
    HtmlParser hp;
    hp.base = readLine(f);

    size_t wordCount = atol(readLine(f).c_str());
    for (size_t i = 0; i < wordCount; ++i)
        hp.words.push_back(readLine(f));

    size_t titleCount = atol(readLine(f).c_str());
    for (size_t i = 0; i < titleCount; ++i)
        hp.titleWords.push_back(readLine(f));

    size_t linkCount = atol(readLine(f).c_str());
    for (size_t i = 0; i < linkCount; ++i) {
        char buf[8192];
        if (!fgets(buf, sizeof(buf), f))
            break;
        size_t len = strlen(buf);
        if (len > 0 && buf[len - 1] == '\n')
            buf[len - 1] = '\0';

        // Parse: URL\tanchor_count\tanchor1\tanchor2...
        char *p = buf;
        char *tab = strchr(p, '\t');
        if (!tab)
            continue;
        std::string url(p, tab);
        p = tab + 1;

        tab = strchr(p, '\t');
        size_t anchorCount = atol(p);

        Link link(url);
        if (tab) {
            p = tab + 1;
            for (size_t j = 0; j < anchorCount; ++j) {
                tab = strchr(p, '\t');
                if (tab) {
                    link.anchorText.push_back(std::string(p, tab));
                    p = tab + 1;
                } else {
                    link.anchorText.push_back(std::string(p));
                    break;
                }
            }
        }
        hp.links.push_back(link);
    }
    return hp;
}
