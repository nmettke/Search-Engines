#pragma once

// HashBlob, a serialization of a HashTable into one contiguous
// block of memory, possibly memory-mapped to a HashFile.

// Nicole Hamilton  nham@umich.edu

#include <cassert>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <malloc.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "Common.h"
#include "utils/hash/HashTable.h"

using Hash = HashTable<const char *, size_t>;
using Pair = Tuple<const char *, size_t>;
using HashBucket = Bucket<const char *, size_t>;

static const size_t Unknown = 0;

size_t RoundUp(size_t length, size_t boundary) {
    // Round up to the next multiple of the boundary, which
    // must be a power of 2.

    static const size_t oneless = boundary - 1, mask = ~(oneless);
    return (length + oneless) & mask;
}

struct SerialTuple {
    // This is a serialization of a HashTable< char *, size_t >::Bucket.
    // One is packed up against the next in a HashBlob.

    // Since this struct includes size_t and uint64_t members, we'll
    // require that it be sizeof( size_t ) aligned to avoid unaligned
    // accesses.

    // Alternatives might be to use #pragma pack() and pay the perf
    // penalty or use a Utf8-style variable-length encoding that
    // does not need to be aligned.

  public:
    // SerialTupleLength = 0 is a sentinel indicating
    // this is the last SerialTuple chained in this list.
    // (Actual length is not given but not needed.)

    size_t Length, Value;
    uint64_t HashValue;

    // The Key will be a C-string of whatever length.
    char Key[Unknown];

    // Calculate the bytes required to encode a HashBucket as a
    // SerialTuple.

    static size_t BytesRequired(const HashBucket *b) {
        // Size of fixed fields + key string + null terminator
        size_t bytes =
            sizeof(Length) + sizeof(Value) + sizeof(HashValue) + strlen(b->tuple.key) + 1;

        return RoundUp(bytes, sizeof(size_t));
    }

    // Write the HashBucket out as a SerialTuple in the buffer,
    // returning a pointer to one past the last character written.

    static char *Write(char *buffer, char *bufferEnd, const HashBucket *b) {
        size_t bytes = BytesRequired(b);

        assert(buffer + bytes <= bufferEnd);

        SerialTuple *st = reinterpret_cast<SerialTuple *>(buffer);

        st->Length = bytes;
        st->Value = b->tuple.value;
        st->HashValue = b->hashValue;

        strcpy(st->Key, b->tuple.key);

        return buffer + bytes;
    }
};

class HashBlob {
    // This will be a hash specifically designed to hold an
    // entire hash table as a single contiguous blob of bytes.
    // Pointers are disallowed so that the blob can be
    // relocated to anywhere in memory

    // The basic structure should consist of some header
    // information including the number of buckets and other
    // details followed by a concatenated list of all the
    // individual lists of tuples within each bucket.

  public:
    // Define a MagicNumber and Version so you can validate
    // a HashBlob really is one of your HashBlobs.

    size_t MagicNumber, Version, BlobSize, NumberOfBuckets, Buckets[Unknown];

    // The SerialTuples will follow immediately after.

    const SerialTuple *Find(const char *key) const {
        // Search for the key k and return a pointer to the
        // ( key, value ) entry.  If the key is not found,
        // return nullptr.

        // YOUR CODE HERE
        uint64_t hashValue = hashString(key);
        size_t bucketIndex = hashValue % NumberOfBuckets;

        size_t bucketOffset = Buckets[bucketIndex];

        if (bucketOffset == 0) {
            return nullptr; // No entries in this bucket
        }

        const char *blobStart = reinterpret_cast<const char *>(this);
        const char *currentPtr = blobStart + bucketOffset;

        while (true) {
            const SerialTuple *currentTuple = reinterpret_cast<const SerialTuple *>(currentPtr);

            if (currentTuple->Length == 0) {
                break;
            }

            if (currentTuple->HashValue == hashValue && CompareEqual(currentTuple->Key, key)) {
                return currentTuple;
            }

            currentPtr += currentTuple->Length;
        }

        return nullptr;
    }

    static size_t BytesRequired(const Hash *hashTable) {
        // Calculate how much space it will take to
        // represent a HashTable as a HashBlob.

        // Need space for the header + buckets +
        // all the serialized tuples.

        // YOUR CODE HERE
        size_t totalBytes = 0;
        totalBytes += sizeof(HashBlob);                            // Header size
        totalBytes += hashTable->numberOfBuckets * sizeof(size_t); // Buckets array size

        for (size_t i = 0; i < hashTable->numberOfBuckets; ++i) {
            HashBucket *current = hashTable->buckets[i];

            // only serialize non-empty buckets
            if (current != nullptr) {
                while (current != nullptr) {
                    totalBytes += SerialTuple::BytesRequired(current);
                    current = current->next;
                }
                // Add space for the Sentinel (Length = 0) only for non-empty buckets
                totalBytes += sizeof(size_t);
            }
        }

        return RoundUp(totalBytes, sizeof(size_t));
    }

    // Write a HashBlob into a buffer, returning a
    // pointer to the blob.

    static HashBlob *Write(HashBlob *hb, size_t bytes, const Hash *hashTable) {
        // YOUR CODE HERE
        hb->MagicNumber = 0xAABBCCDD;
        hb->Version = 1;
        hb->BlobSize = bytes;
        hb->NumberOfBuckets = hashTable->numberOfBuckets;

        char *start = reinterpret_cast<char *>(hb);
        char *bufferEnd = start + bytes;

        size_t *bucketOffsets = hb->Buckets;

        char *dataPtr = reinterpret_cast<char *>(bucketOffsets + hb->NumberOfBuckets);

        for (size_t i = 0; i < hashTable->numberOfBuckets; ++i) {
            HashBucket *current = hashTable->buckets[i];

            if (current == nullptr) {
                bucketOffsets[i] = 0;
            } else {
                bucketOffsets[i] = dataPtr - start;

                while (current != nullptr) {
                    dataPtr = SerialTuple::Write(dataPtr, bufferEnd, current);
                    current = current->next;
                }

                assert(dataPtr + sizeof(size_t) <= bufferEnd);

                *reinterpret_cast<size_t *>(dataPtr) = 0;
                dataPtr += sizeof(size_t);
            }
        }

        return hb;
    }

    // Create allocates memory for a HashBlob of required size
    // and then converts the HashTable into a HashBlob.
    // Caller is responsible for discarding when done.

    // (No easy way to override the new operator to create a
    // variable sized object.)

    static HashBlob *Create(const Hash *hashTable) {
        // YOUR CODE HERE
        size_t bytes = BytesRequired(hashTable);

        void *memory = malloc(bytes);

        if (memory == nullptr) {
            return nullptr;
        }

        HashBlob *blob = reinterpret_cast<HashBlob *>(memory);

        return Write(blob, bytes, hashTable);
    }

    // Discard
    static void Discard(HashBlob *blob) {
        // YOUR CODE HERE
        if (blob != nullptr) {
            free(blob);
        }
    }
};

class HashFile {
  private:
    HashBlob *blob;
    size_t mappedSize;

    size_t FileSize(int f) {
        struct stat fileInfo;
        fstat(f, &fileInfo);
        return fileInfo.st_size;
    }

  public:
    const HashBlob *Blob() { return blob; }

    HashFile(const char *filename) {
        // Open the file for reading, map it and check the header.
        // new a HashFile, fill in the filehandle and blob address.

        int fd = open(filename, O_RDONLY);
        assert(fd >= 0);

        mappedSize = FileSize(fd);

        void *mapped = mmap(nullptr, mappedSize, PROT_READ, MAP_PRIVATE, fd, 0);
        assert(mapped != MAP_FAILED);

        blob = reinterpret_cast<HashBlob *>(mapped);

        close(fd);
    }

    HashFile(const char *filename, const Hash *hashtable) {
        // Open the file for write, map it, write
        // the hashtable out as a HashBlob, and note
        // the blob address.

        mappedSize = HashBlob::BytesRequired(hashtable);

        int fd = open(filename, O_RDWR | O_CREAT | O_TRUNC, 0644);
        assert(fd >= 0);

        int result = ftruncate(fd, mappedSize);
        assert(result == 0);

        void *mapped = mmap(nullptr, mappedSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        assert(mapped != MAP_FAILED);

        blob = HashBlob::Write(reinterpret_cast<HashBlob *>(mapped), mappedSize, hashtable);

        close(fd);
    }

    ~HashFile() { munmap(blob, mappedSize); }
};
