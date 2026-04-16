#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <dirent.h>
#include <functional>
#include <iostream>
#include <sys/stat.h>
#include <sys/types.h>

#include "./index/src/lib/disk_chunk_reader.h"

#include "./utils/hash/HashTable.h"
#include "./utils/string.hpp"
#include "./utils/vector.hpp"

struct Location {
    size_t chunk_id;
    size_t doc_id;
};

static bool urlKeyEqual(string a, string b) { return a == b; }

static uint64_t urlKeyHash(string key) { return static_cast<uint64_t>(std::hash<string>{}(key)); }

static bool docIDKeyEqual(size_t a, size_t b) { return a == b; }

static uint64_t docIDKeyHash(size_t key) { return static_cast<uint64_t>(key); }

string parse_url(const string &line) {
    size_t tab_pos = line.find('\t');
    if (tab_pos != string::npos) {
        return line.substr(0, tab_pos);
    }
    return line;
}

string strip_newline(const string &s) {
    if (!s.empty() && s.back() == '\n') {
        return s.substr(0, s.size() - 1);
    }
    return s;
}

static size_t count_tabs(const string &s) {
    size_t tabs = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\t') {
            ++tabs;
        }
    }
    return tabs;
}

static string to_single_line(const string &s) {
    string out = s;
    for (size_t i = 0; i < out.size(); ++i) {
        if (out[i] == '\n' || out[i] == '\r') {
            out[i] = ' ';
        }
    }
    return out;
}

static bool parse_chunk_id_from_filename(const string &file_name, const char *suffix,
                                         size_t &chunk_id_out) {
    const char *prefix = "chunk_";
    const size_t prefix_len = 6;
    if (file_name.find(prefix) != 0) {
        return false;
    }

    size_t suffix_len = 0;
    while (suffix[suffix_len] != '\0') {
        ++suffix_len;
    }

    if (file_name.size() <= prefix_len + suffix_len) {
        return false;
    }

    if (file_name.substr(file_name.size() - suffix_len) != suffix) {
        return false;
    }

    string id_part = file_name.substr(prefix_len, file_name.size() - prefix_len - suffix_len);
    if (id_part.empty()) {
        return false;
    }

    for (size_t i = 0; i < id_part.size(); ++i) {
        if (id_part[i] < '0' || id_part[i] > '9') {
            return false;
        }
    }

    chunk_id_out = static_cast<size_t>(std::strtoull(id_part.c_str(), nullptr, 10));
    return true;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        std::cerr << "Usage: ./sync_meta <directory_path>\n";
        return 1;
    }

    string dir_path = argv[1];

    // Ensure directory path doesn't end with a slash
    if (!dir_path.empty() && dir_path.back() == '/')
        dir_path.pop_back();

    string index_dir = dir_path + "/body_index/";
    string meta_dir = dir_path + "/meta/";
    string output_meta_dir = dir_path + "/parsed_meta";

    HashTable<string, Location> index_lookup(urlKeyEqual, urlKeyHash);
    HashTable<size_t, size_t> chunk_doc_count(docIDKeyEqual, docIDKeyHash);
    size_t max_chunk_id = 0;
    bool found_chunk = false;

    DIR *dir;
    struct dirent *ent;
    if ((dir = opendir(index_dir.c_str())) != nullptr) {
        while ((ent = readdir(dir)) != nullptr) {
            string file_name = ent->d_name;

            size_t chunk_id = 0;
            if (!parse_chunk_id_from_filename(file_name, ".idx", chunk_id)) {
                continue;
            }

            std::cout << "Processing file: " << file_name << std::endl;
            string full_path = index_dir + file_name;

            DiskChunkReader reader;
            if (!reader.open(full_path)) {
                std::cerr << "Failed to open chunk file: " << full_path << std::endl;
                continue;
            }

            auto header = reader.header();
            size_t num_docs = header.num_documents;

            for (size_t doc_id = 0; doc_id < num_docs; ++doc_id) {
                auto doc_info = reader.getDocument(doc_id);
                string url = doc_info->url;
                index_lookup.Find(url, {chunk_id, doc_id});
            }
            chunk_doc_count.Find(chunk_id, num_docs);
            if (!found_chunk || chunk_id > max_chunk_id) {
                max_chunk_id = chunk_id;
                found_chunk = true;
            }
        }
        closedir(dir);
    } else {
        std::cerr << "Fatal Error: Could not open directory " << dir_path << "\n";
        return 1;
    }

    if (!found_chunk) {
        std::cerr << "Fatal Error: No chunk_*.idx files found in " << index_dir << "\n";
        return 1;
    }

    vector<vector<string>> all_meta(max_chunk_id + 1);
    for (size_t i = 0; i <= max_chunk_id; ++i) {
        size_t doc_count = chunk_doc_count.Find(i, 0)->value;
        all_meta[i] = vector<string>(doc_count);

        for (size_t j = 0; j < doc_count; ++j) {
            all_meta[i][j] = "<UNKNOWN URL>\t<UNKNOWN TITLE>\t<UNKNOWN SNIPPET>";
        }
    }

    if ((dir = opendir(meta_dir.c_str())) != nullptr) {
        while ((ent = readdir(dir)) != nullptr) {
            string file_name = ent->d_name;

            size_t meta_chunk_id = 0;
            if (!parse_chunk_id_from_filename(file_name, ".meta", meta_chunk_id) ||
                meta_chunk_id > max_chunk_id) {
                continue;
            }

            std::cout << "Processing meta file: " << file_name << std::endl;
            string full_path = meta_dir + file_name;

            FILE *fp = fopen(full_path.c_str(), "r");
            if (fp) {
                char *line_buf = nullptr;
                size_t line_buf_cap = 0;
                ssize_t read_len = 0;

                    // Lambda to process a complete meta entry
                auto process_entry = [&](const string &entry) {
                    string single = to_single_line(entry);
                    string url = parse_url(single);
                    if (url.empty()) {
                        return;
                    }
                    auto location_ptr = index_lookup.Find(url);
                    if (location_ptr == nullptr) {
                        if (url.back() == '/') {
                            string url_no_slash = url.substr(0, url.size() - 1);
                            location_ptr = index_lookup.Find(url_no_slash);
                        } else {
                            string url_with_slash = url + "/";
                            location_ptr = index_lookup.Find(url_with_slash);
                        }
                        if (location_ptr == nullptr) {
                            std::cerr << "Warning: URL in meta not found in index: " << url << "\n";
                        }
                    }

                    if (location_ptr != nullptr) {
                        Location loc = location_ptr->value;
                        all_meta[loc.chunk_id][loc.doc_id] = single;
                    }
                };

                string current_entry;

                while ((read_len = getline(&line_buf, &line_buf_cap, fp)) != -1) {
                    (void)read_len;
                    string line(line_buf);
                    line = strip_newline(line);

                    // A complete meta entry has at least 2 tabs (URL\ttitle\tsnippet).
                    // Lines with fewer tabs are continuations of the previous entry
                    // caused by embedded newlines in metadata fields.
                    if (count_tabs(line) >= 2) {
                        // Process the previous complete entry
                        if (!current_entry.empty()) {
                            process_entry(current_entry);
                        }
                        current_entry = line;
                    } else {
                        // Continuation line - append to current entry
                        if (!current_entry.empty()) {
                            current_entry += " " + line;
                        }
                    }
                }
                // Process the last entry
                if (!current_entry.empty()) {
                    process_entry(current_entry);
                }

                if (line_buf != nullptr) {
                    free(line_buf);
                }
                fclose(fp);
            } else {
                std::cerr << "Failed to open meta file\n";
                return 1;
            }
        }
        closedir(dir);
    } else {
        std::cerr << "Fatal Error: Could not open directory " << meta_dir << "\n";
        return 1;
    }

    // Write out the synchronized meta data
    for (size_t i = 0; i < all_meta.size(); ++i) {
        char meta_buffer[64] = {};
        std::snprintf(meta_buffer, sizeof(meta_buffer), "%s/chunk_%zu.meta",
                      output_meta_dir.c_str(), i);
        const string meta_path(meta_buffer);

        FILE *meta_fp = fopen(meta_path.c_str(), "w");
        if (meta_fp) {
            for (size_t j = 0; j < all_meta[i].size(); ++j) {
                fwrite(all_meta[i][j].c_str(), 1, all_meta[i][j].size(), meta_fp);
                fwrite("\n", 1, 1, meta_fp);
            }
            fclose(meta_fp);
        } else {
            std::cerr << "Failed to write synchronized meta file: " << meta_path << "\n";
        }
    }

    std::cout << "Successfully synchronized meta data for " << all_meta.size() << " chunks.\n";
    return 0;
}
