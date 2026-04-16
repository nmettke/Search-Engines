#include <cstdint>
#include <cstdio>
#include <dirent.h>
#include <functional>
#include <iostream>

#include "./index/src/lib/chunk_flusher.h"
#include "./index/src/lib/disk_chunk_reader.h"
#include "./index/src/lib/in_memory_index.h"
#include "./index/src/lib/indexQueue.h"
#include "./index/src/lib/tokenizer.h"

#include "./utils/hash/HashTable.h"
#include "./utils/string.hpp"
#include "./utils/vector.hpp"
#include "parser/HtmlParser.h"

static bool anchorKeyEqual(string a, string b) { return a == b; }

static uint64_t anchorKeyHash(string key) {
    return static_cast<uint64_t>(std::hash<string>{}(key));
}

static vector<string> tokenize(const string &text) {
    vector<string> tokens;
    size_t start = 0;
    while (start < text.size()) {
        while (start < text.size() && !isalnum(static_cast<unsigned char>(text[start])))
            ++start;
        if (start >= text.size())
            break;
        size_t end = start + 1;
        while (end < text.size() && isalnum(static_cast<unsigned char>(text[end])))
            ++end;
        string tok = text.substr(start, end - start);
        tokens.pushBack(tok);
        start = end;
    }
    return tokens;
}

string strip_html_tags(const string &input) {
    string result = "";
    bool in_tag = false;
    for (char c : input) {
        if (c == '<')
            in_tag = true;
        else if (c == '>') {
            in_tag = false;
            result += ' ';
        } else if (!in_tag)
            result += c;
    }
    return result;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        std::cerr << "Usage: ./anchor_index_builder <directory_path>\n";
        return 1;
    }

    string dir_path = argv[1];

    // Ensure directory path doesn't end with a slash
    if (!dir_path.empty() && dir_path.back() == '/')
        dir_path.pop_back();

    string anchor_text_dir = dir_path + "/anchor_index/";
    string index_dir = dir_path + "/body_index/";
    string anchor_index_output_dir = dir_path + "/parsed_anchor_index/";

    HashTable<string, string> global_anchors(anchorKeyEqual, anchorKeyHash);

    std::cout << "Loading anchor texts from " << anchor_text_dir << "...\n";
    DIR *dir;
    struct dirent *ent;
    if ((dir = opendir(anchor_text_dir.c_str())) != nullptr) {
        while ((ent = readdir(dir)) != nullptr) {
            string file_name = ent->d_name;
            if (file_name.find(".idx") != string::npos) {
                std::cout << "Processing file: " << file_name << std::endl;
                string full_path = anchor_text_dir + file_name;
                FILE *blob_file = fopen(full_path.c_str(), "r");

                if (blob_file) {
                    char line[65536];
                    bool in_words = false;

                    while (fgets(line, sizeof(line), blob_file)) {
                        string s(line);
                        if (!s.empty() && s.back() == '\n')
                            s.pop_back();
                        if (!s.empty() && s.back() == '\r')
                            s.pop_back();

                        if (s == "[Words]") {
                            in_words = true;
                            continue;
                        }
                        if (!in_words)
                            continue;

                        size_t tab1 = s.find('\t');
                        size_t tab2 = s.find('\t', tab1 + 1);
                        size_t tab3 = s.find('\t', tab2 + 1);

                        if (tab1 != string::npos && tab2 != string::npos && tab3 != string::npos) {
                            string url = s.substr(0, tab1);
                            string text = s.substr(tab3 + 1);

                            global_anchors.Find(url, "")->value += " " + text;
                        }
                    }
                    fclose(blob_file);
                }
            }
        }
        closedir(dir);
    }
    std::cout << "Loaded " << global_anchors.size() << " unique URLs from HashBlobs.\n";

    std::cout << "Building Synchronized Anchor Indices..." << std::endl;
    Tokenizer tokenizer;

    if ((dir = opendir(index_dir.c_str())) != nullptr) {
        while ((ent = readdir(dir)) != nullptr) {
            string file_name = ent->d_name;

            if (file_name.find("chunk_") == 0 && file_name.find(".idx") != string::npos) {
                std::cout << "Checking file: " << file_name << std::endl;
                string base_name = file_name.substr(0, file_name.length() - 4);

                string full_path = index_dir + file_name;

                InMemoryIndex anchor_index;
                uint32_t doc_count = 0;

                DiskChunkReader reader;
                if (!reader.open(index_dir + file_name)) {
                    std::cerr << "Failed to open chunk file: " << index_dir + file_name
                              << std::endl;
                    std::cerr << "Skipping " << file_name << " due to missing chunk.\n";
                    continue;
                }

                auto header = reader.header();
                size_t num_docs = header.num_documents;

                for (size_t doc_id = 0; doc_id < num_docs; ++doc_id) {
                    auto doc_info = reader.getDocument(doc_id);
                    string url = doc_info->url;
                    vector<string> words;

                    auto entry = global_anchors.Find(url, "");
                    if (!entry->value.empty()) {
                        string raw_text = entry->value;
                        string clean_text = strip_html_tags(raw_text);
                        words = tokenize(clean_text);
                    }

                    if (words.size() == 0) {
                        words = {"PageWithNoAnchorText"};
                    }

                    HtmlParser doc;
                    doc.words = words;
                    doc.sourceUrl = url;

                    auto tokenized = tokenizer.processDocument(doc);
                    for (const auto &tok : tokenized.tokens) {
                        anchor_index.addToken(tok);
                    }

                    anchor_index.finishDocument(tokenized.doc_end);
                    doc_count++;
                }

                // Flush this perfectly synchronized chunk to disk
                string out_path = anchor_index_output_dir + base_name + ".idx";
                std::cout << "Flushing anchor index for " << base_name << " with " << doc_count
                          << " docs to " << out_path << "...\n";
                flushIndexChunk(anchor_index, out_path);

                std::cout << "Built " << out_path << " with " << doc_count
                          << " synchronized docs.\n";
            }
        }
        closedir(dir);
    }

    std::cout << "Anchor Indexing Complete!\n";
    return 0;
}