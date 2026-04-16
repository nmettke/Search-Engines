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

    // Collect anchor file paths
    vector<string> anchor_files;
    DIR *dir;
    struct dirent *ent;
    if ((dir = opendir(anchor_text_dir.c_str())) != nullptr) {
        while ((ent = readdir(dir)) != nullptr) {
            string file_name = ent->d_name;
            if (file_name.find(".idx") != string::npos) {
                anchor_files.pushBack(anchor_text_dir + file_name);
            }
        }
        closedir(dir);
    }
    std::cout << "Found " << anchor_files.size() << " anchor files.\n";

    // Collect body index chunk file names
    vector<string> chunk_files;
    if ((dir = opendir(index_dir.c_str())) != nullptr) {
        while ((ent = readdir(dir)) != nullptr) {
            string file_name = ent->d_name;
            if (file_name.find("chunk_") == 0 && file_name.find(".idx") != string::npos) {
                chunk_files.pushBack(file_name);
            }
        }
        closedir(dir);
    }

    std::cout << "Building Synchronized Anchor Indices (" << chunk_files.size()
              << " chunks)..." << std::endl;
    Tokenizer tokenizer;

    // Process one body_index chunk at a time to avoid loading all anchor text into memory
    for (size_t ci = 0; ci < chunk_files.size(); ++ci) {
        string file_name = chunk_files[ci];
        std::cout << "\n[Chunk " << (ci + 1) << "/" << chunk_files.size() << "] Processing: "
                  << file_name << std::endl;
        string base_name = file_name.substr(0, file_name.length() - 4);

        DiskChunkReader reader;
        if (!reader.open(index_dir + file_name)) {
            std::cerr << "Failed to open chunk file: " << index_dir + file_name << std::endl;
            std::cerr << "Skipping " << file_name << " due to missing chunk.\n";
            continue;
        }

        auto header = reader.header();
        size_t num_docs = header.num_documents;

        // Step 1: Collect URLs in this chunk into a set (hash table)
        HashTable<string, string> chunk_anchors(anchorKeyEqual, anchorKeyHash);
        for (size_t doc_id = 0; doc_id < num_docs; ++doc_id) {
            auto doc_info = reader.getDocument(doc_id);
            chunk_anchors.Find(doc_info->url, "");
        }
        std::cout << "  " << num_docs << " docs, scanning anchor files..." << std::endl;

        // Step 2: Scan all anchor files, but only keep text for URLs in this chunk
        for (size_t ai = 0; ai < anchor_files.size(); ++ai) {
            FILE *blob_file = fopen(anchor_files[ai].c_str(), "r");
            if (!blob_file)
                continue;

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

                    // Only accumulate if this URL belongs to the current chunk
                    auto entry = chunk_anchors.Find(url);
                    if (entry) {
                        entry->value += " " + text;
                    }
                }
            }
            fclose(blob_file);
        }

        // Step 3: Build anchor index for this chunk
        InMemoryIndex anchor_index;
        uint32_t doc_count = 0;

        for (size_t doc_id = 0; doc_id < num_docs; ++doc_id) {
            auto doc_info = reader.getDocument(doc_id);
            string url = doc_info->url;
            vector<string> words;

            auto entry = chunk_anchors.Find(url);
            if (entry && !entry->value.empty()) {
                string clean_text = strip_html_tags(entry->value);
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
        std::cout << "  Flushing " << doc_count << " docs to " << out_path << "...\n";
        flushIndexChunk(anchor_index, out_path);

        std::cout << "  Done.\n";
        // chunk_anchors goes out of scope here — memory is freed before next chunk
    }

    std::cout << "Anchor Indexing Complete!\n";
    return 0;
}