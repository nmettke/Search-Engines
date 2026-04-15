// query/create_dummy_indices.cpp
// This file is a utility to create dummy index files for testing the worker node.

#include "../index/src/lib/chunk_flusher.h"
#include "../index/src/lib/in_memory_index.h"
#include "../index/src/lib/tokenizer.h"

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <vector>

#include "../utils/string.hpp"
#include "../utils/vector.hpp"

namespace {

Link make_link(const ::string &url, std::initializer_list<::string> anchor_words) {
    Link link(url);
    for (const auto &word : anchor_words) {
        link.anchorText.pushBack(word);
    }
    return link;
}

HtmlParser make_doc(std::initializer_list<::string> words,
                    std::initializer_list<::string> title_words, std::initializer_list<Link> links,
                    const ::string &url, uint8_t seed_distance) {
    HtmlParser doc(words, title_words, links, url);
    doc.seedDistance = seed_distance;
    return doc;
}

::string join_words(const ::vector<::string> &words, size_t limit = static_cast<size_t>(-1)) {
    ::string joined;
    size_t count = words.size();
    if (limit < count) {
        count = limit;
    }

    for (size_t i = 0; i < count; ++i) {
        if (!joined.empty()) {
            joined += " ";
        }
        joined += words[i];
    }
    return joined;
}

void write_meta_file(const ::string &path, const ::vector<HtmlParser> &docs) {
    FILE *fp = fopen(path.c_str(), "w");
    if (!fp) {
        std::cerr << "Failed to open meta file for writing: " << path << "\n";
        return;
    }

    for (const auto &doc : docs) {
        ::string title = join_words(doc.titleWords);
        if (title.empty()) {
            title = doc.documentUrl();
        }

        ::string snippet = join_words(doc.words, 18);
        fprintf(fp, "%s\t%s\t%s\n", doc.documentUrl().c_str(), title.c_str(), snippet.c_str());
    }

    fclose(fp);
}

void create_dummy_index_and_meta(const ::string &index_path, const ::string &meta_path,
                                 const ::vector<HtmlParser> &docs) {
    Tokenizer tokenizer;
    InMemoryIndex index;
    for (const auto &doc : docs) {
        auto tokenized = tokenizer.processDocument(doc);
        for (const auto &tok : tokenized.tokens) {
            index.addToken(tok);
        }
        index.finishDocument(tokenized.doc_end);
    }

    try {
        flushIndexChunk(index, index_path);
        write_meta_file(meta_path, docs);
        std::cout << "Successfully wrote chunk to: " << index_path << "\n";
    } catch (const std::exception &e) {
        std::cerr << "Failed to write chunk: " << e.what() << "\n";
    }
}

void prepare_worker_dirs(const ::string &root) {
    ::string command = "mkdir -p " + root + "/body_index " + root + "/meta";
    system(command.c_str());
}

} // namespace

int main() {
    ::vector<HtmlParser> docs1_1 = {
        make_doc({"Trusted", "cat", "care", "guide", "with", "feeding", "tips", "cat", "health",
                  "advice", "and", "play", "ideas"},
                 {"Complete", "Cat", "Care", "Guide"},
                 {make_link("https://www.vet.example.org/cat-health", {"cat", "health", "guide"}),
                  make_link("https://www.shelter.example.org/adopt", {"adopt", "a", "cat"}),
                  make_link("https://www.example.org/cat-food", {"cat", "food"})},
                 "https://www.example.org/cats/care-guide", 0),
        make_doc({"Adopt", "a", "friendly", "cat", "today", "with", "adoption", "tips",
                  "checklists", "and", "cat", "supplies"},
                 {"Cat", "Adoption", "Tips"},
                 {make_link("https://www.example.com/pets/checklist", {"adoption", "checklist"})},
                 "https://www.example.com/pets/cats/adoption", 1),
    };

    ::vector<HtmlParser> docs1_2 = {
        make_doc({"My", "cat", "and", "my", "dog", "are", "friends", "with", "daily", "cat",
                  "routines", "and", "walk", "plans"},
                 {"Cat", "and", "Dog", "Routine"},
                 {make_link("https://www.example.net/routine", {"daily", "routine"})},
                 "https://www.example.net/blog/family/pets/cat-dog-routine?ref=home", 2),
        make_doc({"No", "cats", "here", "just", "birds", "and", "fish"},
                 {"Bird", "Watching", "Notes"}, {}, "https://www.example.com/birds", 1),
    };

    prepare_worker_dirs("chunk1");
    create_dummy_index_and_meta("chunk1/body_index/chunk_0001.idx", "chunk1/meta/chunk_0001.meta",
                                docs1_1);
    create_dummy_index_and_meta("chunk1/body_index/chunk_0002.idx", "chunk1/meta/chunk_0002.meta",
                                docs1_2);

    ::vector<HtmlParser> docs2_1 = {
        make_doc({"Cheap", "cat", "deals", "cat", "sale", "cat", "offer"},
                 {"Cheap", "Cat", "Deals"},
                 {make_link("http://ads.spam.example.com/click", {"click"})},
                 "http://spam-example.com/store/cat/2024/09/deals?session=abc&ref=ad&src=push", 5),
        make_doc({"I", "love", "my", "cat", "and", "my", "dog", "with", "simple", "cat", "games",
                  "at", "home"},
                 {"Cat", "Games", "at", "Home"},
                 {make_link("https://www.example.edu/pets", {"pet", "games"}),
                  make_link("https://www.example.edu/cats/play", {"cat", "play"})},
                 "https://www.example.edu/home/pets/cat-games", 1),
    };

    ::vector<HtmlParser> docs2_2 = {
        make_doc({"My", "cat", "hates", "dogs", "after", "a", "cat", "park", "visit"},
                 {"Cat", "Park", "Story"}, {},
                 "https://cats.example.com/community/stories/cat-park-story", 3),
        make_doc({"My", "cat", "loves", "ice", "cream", "but", "the", "cat", "vet", "says",
                  "choose", "safer", "treats"},
                 {"Cat", "Treat", "Ideas"},
                 {make_link("https://www.vet.example.org/treats", {"safe", "cat", "treats"}),
                  make_link("https://www.example.org/cat-health", {"cat", "health"})},
                 "https://www.example.org/cats/treat-ideas", 0),
    };

    prepare_worker_dirs("chunk2");
    create_dummy_index_and_meta("chunk2/body_index/chunk_0001.idx", "chunk2/meta/chunk_0001.meta",
                                docs2_1);
    create_dummy_index_and_meta("chunk2/body_index/chunk_0002.idx", "chunk2/meta/chunk_0002.meta",
                                docs2_2);
    return 0;
}
