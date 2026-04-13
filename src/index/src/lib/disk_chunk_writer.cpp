// src/lib/disk_chunk_writer.cpp
#include "disk_chunk_writer.h"
#include "binary_writer.h"
#include "seek_table.h"
#include "vbyte.h"
#include <fcntl.h>
#include <system_error>
#include <unistd.h>

DiskChunkWriter::DiskChunkWriter(const ::string &filename) {
    fd_ = open(filename.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd_ < 0) {
        throw std::system_error(errno, std::generic_category(),
                                "Failed to open chunk file for writing");
    }
}

DiskChunkWriter::~DiskChunkWriter() {
    if (fd_ >= 0) {
        close(fd_);
    }
}

void DiskChunkWriter::writeHeader(const FileHeader &header) {
    BinaryWriter writer(fd_);
    writer.writePOD(header);
}

uint64_t DiskChunkWriter::writePostingList(const ::vector<uint32_t> &locations) {
    BinaryWriter writer(fd_);
    uint64_t current_offset = static_cast<uint64_t>(writer.currentOffset());

    ::vector<uint8_t> compressed_data = VariableByteEncoder::encodeDeltaList(locations);

    PostingListHeader pl_header;
    pl_header.num_postings = locations.size();
    pl_header.data_size = compressed_data.size();

    ::vector<uint8_t> seek_table_bytes;
    if (SeekTable::shouldBuild(locations.size())) {
        pl_header.has_seek_table = 1;
        SeekTable table = SeekTable::build(compressed_data, locations.size());
        seek_table_bytes = table.serialize();
    } else {
        pl_header.has_seek_table = 0;
    }

    writer.writePOD(pl_header);

    if (pl_header.has_seek_table) {
        writer.writeBuffer(seek_table_bytes.data(), seek_table_bytes.size());
    }

    writer.writeBuffer(compressed_data.data(), compressed_data.size());

    return current_offset;
}

uint64_t DiskChunkWriter::writeDocumentTable(const ::vector<DocumentRecord> &documents) {
    BinaryWriter writer(fd_);
    uint64_t doctable_start = static_cast<uint64_t>(writer.currentOffset());

    uint32_t num_docs = static_cast<uint32_t>(documents.size());
    writer.writePOD(num_docs);

    off_t offsets_array_start = writer.currentOffset();
    ::vector<uint64_t> doc_offsets(num_docs, 0);
    writer.writeBuffer(doc_offsets.data(), num_docs);

    for (size_t i = 0; i < num_docs; ++i) {
        const auto &doc = documents[i];

        doc_offsets[i] = static_cast<uint64_t>(writer.currentOffset()) - doctable_start;

        writer.writeString16(doc.url);
        writer.writeString16(doc.features.raw_tld);

        DocumentRecordDisk disk_record{};
        disk_record.start_location = doc.start_location;
        disk_record.end_location = doc.end_location;
        disk_record.word_count = doc.word_count;
        disk_record.title_word_count = doc.title_word_count;
        disk_record.seed_distance = doc.seed_distance;
        disk_record.features.flags = doc.features.flags;
        disk_record.features.base_domain_length = doc.features.base_domain_length;
        disk_record.features.url_length = doc.features.url_length;
        disk_record.features.path_length = doc.features.path_length;
        disk_record.features.path_depth = doc.features.path_depth;
        disk_record.features.query_param_count = doc.features.query_param_count;
        disk_record.features.numeric_path_char_count = doc.features.numeric_path_char_count;
        disk_record.features.domain_hyphen_count = doc.features.domain_hyphen_count;
        disk_record.features.outgoing_link_count = doc.features.outgoing_link_count;
        disk_record.features.outgoing_anchor_word_count = doc.features.outgoing_anchor_word_count;

        writer.writePOD(disk_record);
    }

    off_t end_of_doctable = writer.currentOffset();
    writer.seekSet(offsets_array_start);
    writer.writeBuffer(doc_offsets.data(), num_docs);

    writer.seekSet(end_of_doctable);

    return doctable_start;
}

uint64_t DiskChunkWriter::writeDictionary(const ::vector<::vector<DictionaryEntry>> &buckets) {
    BinaryWriter writer(fd_);
    off_t dict_start_offset = writer.currentOffset();

    // write num_buckets
    size_t num_buckets = buckets.size();
    writer.writePOD(num_buckets);

    // write placeholder Buckets[] array
    off_t bucket_array_start = writer.currentOffset();
    ::vector<size_t> bucket_offsets(num_buckets, 0);
    writer.writeBuffer(bucket_offsets.data(), num_buckets);

    // write the chains and record their start offsets
    for (size_t i = 0; i < num_buckets; ++i) {
        if (buckets[i].empty())
            continue;

        off_t current_chain_start = writer.currentOffset();
        bucket_offsets[i] = current_chain_start - dict_start_offset;

        for (const auto &entry : buckets[i]) {
            BucketDisk b_disk = entry.disk_info;
            b_disk.string_length = static_cast<uint16_t>(entry.term.size());

            writer.writePOD(b_disk);
            writer.writeBuffer(entry.term.data(), b_disk.string_length);
        }

        BucketDisk sentinel;
        sentinel.occupied = 0;
        writer.writePOD(sentinel);
    }

    // seek back and overwrite the placeholder Buckets[] array with real offsets
    off_t end_of_dict = writer.currentOffset();

    writer.seekSet(bucket_array_start);
    writer.writeBuffer(bucket_offsets.data(), num_buckets);

    writer.seekSet(end_of_dict); // return to the end of the file

    return static_cast<uint64_t>(dict_start_offset);
}

void DiskChunkWriter::finish(const FileHeader &final_header) {
    BinaryWriter writer(fd_);
    writer.seekSet(0);
    writer.writePOD(final_header);
}
