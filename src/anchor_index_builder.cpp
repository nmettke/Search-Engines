#include <cstddef>
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

string to_string(size_t n) {
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "%zu", n);
    return string(buffer);
}

size_t partition_anchor_text(const string &index_dir, const string &anchor_text_dir,
                             const string &partition_dir) {
    std::cout << "Partitioning anchor text files, storing in " << partition_dir << std::endl;

    std::cout << "Loading URL to chunk mapping from body index...\n";
    HashTable<string, int> url_to_chunk(anchorKeyEqual, anchorKeyHash);

    DIR *dir;
    struct dirent *ent;
    size_t total_chunks = 0;
    if ((dir = opendir(index_dir.c_str())) != nullptr) {
        while ((ent = readdir(dir)) != nullptr) {
            string file_name = ent->d_name;
            if (file_name.length() >= 10 && file_name.substr(file_name.length() - 4) == ".idx") {
                total_chunks++;

                std::cout << "Processing chunk file: " << file_name << "...\n";

                // get chunk id from file name, assuming format "chunk_{id}.idx"
                string chunk_id_str = file_name.substr(6, file_name.length() - 10);
                int chunk_id = atoi(chunk_id_str.c_str());

                DiskChunkReader reader;
                if (reader.open(index_dir + file_name)) {
                    size_t num_docs = reader.header().num_documents;
                    for (size_t doc_id = 0; doc_id < num_docs; ++doc_id) {
                        auto doc_info = reader.getDocument(doc_id);
                        url_to_chunk.Find(doc_info->url, chunk_id);
                    }
                }
            }
        }
        closedir(dir);
    }

    std::cout << "URL to chunk mapping loaded. " << total_chunks << " chunks found.\n";
    std::cout << "Partitioning anchor text files...\n";

    // Partition anchor text files
    vector<FILE *> partition_files(total_chunks, nullptr);
    for (size_t i = 0; i < total_chunks; ++i) {
        string file_name = partition_dir + "partition_" + to_string(i) + ".txt";
        partition_files[i] = fopen(file_name.c_str(), "w");
    }

    if ((dir = opendir(anchor_text_dir.c_str())) != nullptr) {
        while ((ent = readdir(dir)) != nullptr) {
            string file_name = ent->d_name;
            if (file_name.find(".idx") != string::npos) {
                std::cout << "Partitioning anchor text file: " << file_name << "...\n";
                string blob_path = anchor_text_dir + file_name;

                FILE *blob_file = fopen(blob_path.c_str(), "r");
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

                        int chunk_id;
                        auto entry = url_to_chunk.Find(url);
                        if (entry) {
                            chunk_id = entry->value;
                            fprintf(partition_files[chunk_id], "%s\t%s\n", url.c_str(),
                                    text.c_str());
                        }
                    }
                }
                fclose(blob_file);
            }
        }
        closedir(dir);
    }

    for (int i = 0; i < total_chunks; ++i) {
        fclose(partition_files[i]);
    }

    std::cout << "Partitioning complete. " << total_chunks << " partition files created.\n";
    return total_chunks;
}

void build_anchor_index(size_t chunk_id, const string &index_dir, const string &partition_dir,
                        const string &output_dir) {
    std::cout << "Building anchor index for chunk " << chunk_id << "...\n";

    string chunk_file = index_dir + "chunk_" + to_string(chunk_id) + ".idx";
    string partition_file = partition_dir + "partition_" + to_string(chunk_id) + ".txt";
    string output_file = output_dir + "chunk_" + to_string(chunk_id) + ".idx";

    HashTable<string, string> url_to_anchor(anchorKeyEqual, anchorKeyHash);
    FILE *pf = fopen(partition_file.c_str(), "r");
    if (pf) {
        char line[65536];
        while (fgets(line, sizeof(line), pf)) {
            string s(line);
            if (!s.empty() && s.back() == '\n')
                s.pop_back();
            if (!s.empty() && s.back() == '\r')
                s.pop_back();
            size_t tab = s.find('\t');
            if (tab != string::npos) {
                string url = s.substr(0, tab);
                string text = s.substr(tab + 1);
                auto entry = url_to_anchor.Find(url, "");
                entry->value += text + " ";
            }
        }
        fclose(pf);
    }

    DiskChunkReader reader;
    if (!reader.open(chunk_file)) {
        std::cerr << "Failed to open chunk file: " << chunk_file << std::endl;
        return;
    }

    Tokenizer tokenizer;
    InMemoryIndex anchor_index;
    auto header = reader.header();
    for (uint32_t doc_id = 0; doc_id < header.num_documents; ++doc_id) {
        auto doc_info = reader.getDocument(doc_id);
        string url = doc_info->url;
        vector<string> words;

        auto entry = url_to_anchor.Find(url);
        if (entry && !entry->value.empty()) {
            words = tokenize(entry->value);
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
    }

    flushIndexChunk(anchor_index, output_file);

    std::cout << "Anchor index for chunk " << chunk_id << " built and flushed to " << output_file
              << "\n";
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

    string partition_dir = dir_path + "/anchor_partitions/";

    size_t total_chunks = partition_anchor_text(index_dir, anchor_text_dir, partition_dir);

    for (size_t chunk_id = 0; chunk_id < total_chunks; ++chunk_id) {
        build_anchor_index(chunk_id, index_dir, partition_dir, anchor_index_output_dir);
    }

    std::cout << "Anchor Indexing Complete!\n";
    return 0;
}