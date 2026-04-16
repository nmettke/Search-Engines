// src/main.cpp
#include "lib/chunk_flusher.h"
#include "lib/disk_chunk_reader.h"
#include "lib/in_memory_index.h"
#include "lib/isr.h"
#include "lib/query_engine.h"
#include "lib/tokenizer.h"

#include "utils/string.hpp"
#include "utils/vector.hpp"
#include <cstdint>
#include <exception>
#include <iostream>
#include <sys/types.h>

// this is a simple integration interface to show how the components fit together.

// int main() {
//     // 1. Mock the crawler output using the HtmlParser interface
//     ::vector<HtmlParser> docs;
//     // Initialization: {words}, {titleWords}, {links}, base_url
//     docs.pushBack(HtmlParser{{"The", "CAT", "sat."}, {}, {}, "doc1"});
//     docs.pushBack(HtmlParser{{"A", "Dog", "Running", "after", "my", "cat!"}, {}, {}, "doc2"});
//     docs.pushBack(HtmlParser{{"My", "cat", "and", "my", "dog", "are", "friends."}, {}, {},
//     "doc3"}); docs.pushBack(HtmlParser{{"No", "cats", "here."}, {}, {}, "doc4"});
//     docs.pushBack(HtmlParser{{"Just", "a", "cat."}, {}, {}, "doc5"});
//     docs.pushBack(HtmlParser{{"I", "love", "my", "cat.", "and", "my", "dog."}, {}, {}, "doc6"});
//     docs.pushBack(HtmlParser{{"My", "cat", "hates", "dogs."}, {}, {}, "doc7"});
//     docs.pushBack(HtmlParser{{"My", "cat", "loves", "ice", "cream."}, {}, {}, "doc8"});

//     // 2. Tokenize and build the In-Memory Index
//     Tokenizer tokenizer;
//     InMemoryIndex index;
//     for (const auto &doc : docs) {
//         auto tokenized = tokenizer.processDocument(doc);
//         for (const auto &tok : tokenized.tokens) {
//             index.addToken(tok);
//         }
//         index.finishDocument(tokenized.doc_end);
//     }

//     // 3. Flush the chunk to disk using OUR flusher interface
//     const ::string path = "chunk_0001.idx";
//     try {
//         flushIndexChunk(index, path);
//         std::cout << "Successfully wrote chunk to: " << path << "\n";
//     } catch (const std::exception &e) {
//         std::cerr << "Failed to write chunk: " << e.what() << "\n";
//         return 1;
//     }

//     DiskChunkReader reader;
//     if (!reader.open(path)) {
//         std::cerr << "Failed to open chunk\n";
//         return 1;
//     }

//     std::cout << "Chunk written and reopened: " << path << "\n";
//     std::cout << "docs=" << reader.header().num_documents
//               << ", terms=" << reader.header().num_unique_terms
//               << ", total_locations=" << reader.header().total_locations << "\n";

//     QueryEngine engine(reader);

//     ::string query1 = "cat AND dog";
//     ::string query2 = "cat OR dog";
//     ::string query3 = "\"my cat\" -dog";
//     ::string query4 = "\"my dog\"";
//     auto results1 = engine.search(query1);
//     auto results2 = engine.search(query2);
//     auto results3 = engine.search(query3);
//     auto results4 = engine.search(query4);

//     std::cout << "\nResults for '" << query1 << "':\n";
//     for (const auto &doc : results1) {
//         std::cout << "- " << doc.url << "\n";
//     }

//     std::cout << "\nResults for '" << query2 << "':\n";
//     for (const auto &doc : results2) {
//         std::cout << "- " << doc.url << "\n";
//     }

//     std::cout << "\nResults for '" << query3 << "':\n";
//     for (const auto &doc : results3) {
//         std::cout << "- " << doc.url << "\n";
//     }

//     std::cout << "\nResults for '" << query4 << "':\n";
//     for (const auto &doc : results4) {
//         std::cout << "- " << doc.url << "\n";
//     }

//     return 0;
// }

string parse_url(const string &line) {
    size_t tab_pos = line.find('\t');
    if (tab_pos != string::npos) {
        return line.substr(0, tab_pos);
    }
    return line;
}

static string normalize_url_key(const string &url) {
    string normalized;
    for (size_t i = 0; i < url.size(); ++i) {
        char ch = url[i];
        if (ch != '\n' && ch != '\r' && ch != '\t' && ch != ' ') {
            normalized.pushBack(ch);
        }
    }
    return normalized;
}

// debug body text index
int main() {
    string body_file_path = "../../data/body_index/chunk_1.idx";
    string meta_file_path = "../../data/parsed_meta/chunk_1.meta";
    DiskChunkReader body_reader;
    if (!body_reader.open(body_file_path)) {
        std::cerr << "Failed to open chunk\n";
        return 1;
    }

    std::cout << "Chunk opened: " << body_file_path << "\n";
    std::cout << "docs=" << body_reader.header().num_documents
              << ", terms=" << body_reader.header().num_unique_terms
              << ", total_locations=" << body_reader.header().total_locations << "\n";

    vector<string> meta_data;
    FILE *meta_fp = fopen(meta_file_path.c_str(), "r");
    if (meta_fp) {
        char *line_buf = nullptr;
        size_t line_buf_cap = 0;
        ssize_t read_len = 0;

        while ((read_len = getline(&line_buf, &line_buf_cap, meta_fp)) != -1) {
            (void)read_len;
            string line(line_buf);
            if (!line.empty() && line.back() == '\n')
                line.pop_back();
            meta_data.pushBack(line);
        }

        if (line_buf != nullptr) {
            free(line_buf);
        }
        fclose(meta_fp);
    } else {
        std::cerr << "Failed to open meta file\n";
        return 1;
    }

    std::cout << "Meta data loaded: " << meta_file_path << "\n";

    int mismatch_count = 0;
    for (size_t i = 0; i < meta_data.size(); ++i) {
        auto body_doc_info = body_reader.getDocument(i);

        string body_url = body_doc_info->url;
        string meta_url = parse_url(meta_data[i]);

        string body_url_key = normalize_url_key(body_url);
        string meta_url_key = normalize_url_key(meta_url);

        if (meta_url_key != body_url_key) {
            mismatch_count++;
            std::cerr << "URL mismatch at doc_id " << i << ": " << meta_url << " vs " << body_url
                      << "\n";
        }

        if (mismatch_count >= 10) {
            std::cerr << "Too many mismatches, aborting further checks.\n";
            break;
        }
    }

    std::cout << "Finished validating URLs between body index and meta data.\n";
    return 0;
}