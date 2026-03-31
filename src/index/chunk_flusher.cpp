// index/chunk_flusher.cpp
#include "Common.h"
#include "disk_chunk_writer.h"
#include "in_memory_index.h"
#include <vector>

void flushIndexChunk(const InMemoryIndex &mem_index, const std::string &filename) {
    DiskChunkWriter writer(filename);

    // write dummy header
    FileHeader header;
    writer.writeHeader(header);

    // prepare dictionary buckets
    const auto &postings = mem_index.postings();
    size_t num_unique_terms = postings.size();

    // size the dictionary to keep load factor low
    size_t num_buckets = std::max<size_t>(1, num_unique_terms * 1.5);
    std::vector<std::vector<DiskChunkWriter::DictionaryEntry>> dict_buckets(num_buckets);

    // write Posting Lists and build Dictionary Entries
    for (const auto &[term, posting_list] : postings) {
        // write the compressed posting list to disk
        uint64_t current_posting_offset = writer.writePostingList(posting_list.locations);

        // prepare the dictionary entry for this term
        DiskChunkWriter::DictionaryEntry entry;
        entry.term = term;
        entry.disk_info.occupied = 1;
        entry.disk_info.posting_offset = current_posting_offset;
        entry.disk_info.doc_frequency = posting_list.doc_frequency;
        entry.disk_info.collection_frequency = posting_list.collection_frequency;

        // hash the term to find its bucket
        uint64_t hash_val = hashString(term.c_str());
        size_t bucket_idx = hash_val % num_buckets;

        dict_buckets[bucket_idx].push_back(entry);
    }

    // write Document Table
    uint64_t doctable_offset = writer.writeDocumentTable(mem_index.documents());

    // write Dictionary
    uint64_t dict_offset = writer.writeDictionary(dict_buckets);

    // overwrite the Header with final metadata
    header.num_documents = mem_index.documents().size();
    header.total_locations = mem_index.totalLocations();
    header.num_unique_terms = num_unique_terms;
    header.dict_offset = dict_offset;
    header.postings_offset = sizeof(FileHeader);
    header.doctable_offset = doctable_offset;

    writer.finish(header);
}