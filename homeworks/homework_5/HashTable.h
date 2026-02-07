// Simple hash table template.

// Nicole Hamilton  nham@umich.edu

#pragma once

#include <cassert>
#include <iostream>
#include <iomanip>
#include <cstdint>


const size_t INITIAL_BUCKET_COUNT = 2048;


template <typename Key, typename Value>
class Tuple {
public:
   Key key;
   Value value;

   Tuple(const Key &k, const Value v): key(k), value(v) {}
};


template <typename Key, typename Value>
class Bucket {
public:
   Bucket *next;
   uint64_t hashValue;
   Tuple< Key, Value > tuple;

   Bucket(const Key &k, uint64_t h, const Value v)
   : tuple(k, v), next(nullptr), hashValue(h) {}
};


template <typename Key, typename Value>
class HashTable {

private:

   Bucket< Key, Value > **buckets;
   size_t numberOfBuckets;

   uint64_t ( *hash )( const Key );
   bool ( *compareEqual )( const Key, const Key );
   size_t uniqueKeys;

   friend class Iterator;
   friend class HashBlob;

   void clear() {
      for (size_t i = 0; i < numberOfBuckets; i++) {
         Bucket<Key, Value>* current = buckets[i];
         while (current != nullptr) {
            Bucket<Key, Value>* toDelete = current;
            current = current->next;
            delete toDelete;
         }
         buckets[i] = nullptr;
      }

      uniqueKeys = 0;
      numberOfBuckets = 0;
      delete[] buckets;
      buckets = nullptr;
   }

public:

   Tuple< Key, Value >* Find(const Key k, const Value initialValue) {
      // Search for the key k and return a pointer to the
      // ( key, value ) entry.  If the key is not already
      // in the hash, add it with the initial value.
      uint64_t hashValue = hash(k);
      size_t bucketIndex = hashValue % numberOfBuckets;

      Bucket< Key, Value >* current = buckets[bucketIndex];
      while (current != nullptr) {
         if (compareEqual(current->tuple.key, k)) {
            return &(current->tuple);
         }
         current = current->next;
      }

      Optimize();
      bucketIndex = hashValue % numberOfBuckets;

      Bucket< Key, Value >* newBucket = new Bucket< Key, Value >(
         k, hashValue, initialValue
      );
      newBucket->next = buckets[bucketIndex];
      buckets[bucketIndex] = newBucket;
      uniqueKeys++;

      return &(newBucket->tuple);
   }

   Tuple< Key, Value >* Find(const Key k) const {
      // Search for the key k and return a pointer to the
      // ( key, value ) enty.  If the key is not already
      // in the hash, return nullptr.

      uint64_t hashValue = hash(k);
      size_t bucketIndex = hashValue % numberOfBuckets;

      Bucket< Key, Value >* current = buckets[bucketIndex];
      while (current != nullptr) {
         if (compareEqual(current->tuple.key, k)) {
            return &(current->tuple);
         }
         current = current->next;
      }
      return nullptr;
   }

   void Optimize(double loading = 1.5) {
      // Grow or shrink the table as appropriate once we know the loading. A
      // goodrule of thumb is that the table size should be at least 1.5x the
      // number of unique keys.
      size_t desiredSize = static_cast<size_t>(uniqueKeys * loading);
      if (desiredSize <= numberOfBuckets) {
         return;
      }

      size_t newSize = (numberOfBuckets == 0) ? 1 : numberOfBuckets;
      while (newSize < desiredSize) {
         newSize *= 2;
      }

      Bucket< Key, Value >** newBuckets = new Bucket< Key, Value >*[newSize];
      for (size_t i = 0; i < newSize; i++) {
         newBuckets[i] = nullptr;
      }

      for (size_t i = 0; i < numberOfBuckets; i++) {
         Bucket< Key, Value >* current = buckets[i];
         while (current != nullptr) {
            Bucket< Key, Value >* nextBucket = current->next;

            size_t newIndex = current->hashValue % newSize;
            current->next = newBuckets[newIndex];
            newBuckets[newIndex] = current;

            current = nextBucket;
         }
      }

      delete[] buckets;
      buckets = newBuckets;
      numberOfBuckets = newSize;
   }


   // Your constructor may add defaults for arguments to the
   // constructor.  The compareEqual function should return
   // true if the keys are equal.  The hash function should
   // return a 64-bit value.
   HashTable(
      bool ( *compareEqual )(const Key, const Key),
      uint64_t ( *hash )(const Key),
      size_t numberOfBuckets = INITIAL_BUCKET_COUNT
   ) :   buckets(nullptr),
         numberOfBuckets(numberOfBuckets),
         hash(hash),
         compareEqual(compareEqual),
         uniqueKeys(0)
   {
      if (this->numberOfBuckets == 0) {
         this->numberOfBuckets = INITIAL_BUCKET_COUNT;
      }

      buckets = new Bucket<Key, Value>*[this->numberOfBuckets];
      for (size_t i = 0; i < this->numberOfBuckets; i++) {
         buckets[i] = nullptr;
      }
   }

   // Disable copy constructor and assignment operator
   HashTable(const HashTable&) = delete;
   HashTable& operator=(const HashTable&) = delete;

   HashTable(HashTable&& other) noexcept 
      : buckets(other.buckets), numberOfBuckets(other.numberOfBuckets), 
        hash(other.hash), compareEqual(other.compareEqual),
        uniqueKeys(other.uniqueKeys) 
   {
      other.buckets = nullptr;
      other.numberOfBuckets = 0;
      other.uniqueKeys = 0;
   }

   HashTable& operator=(HashTable&& other) noexcept {
      if (this != &other) {
         clear();
         buckets = other.buckets;
         numberOfBuckets = other.numberOfBuckets;
         hash = other.hash;
         compareEqual = other.compareEqual;
         uniqueKeys = other.uniqueKeys;

         other.buckets = nullptr;
         other.numberOfBuckets = 0;
         other.uniqueKeys = 0;
      }
      return *this;
   }

   ~HashTable( ) {
      clear();
   }


   class Iterator {
   
   private:

      friend class HashTable;

      HashTable* table;
      size_t bucketIndex;
      Bucket<Key, Value>* currentBucket;

      Iterator(HashTable* table, size_t bucket, Bucket<Key, Value>* b)
      :  table(table), bucketIndex(bucket), currentBucket(b) { }

   public:

      Iterator() : Iterator(nullptr, 0, nullptr) {}

      ~Iterator() = default;

      Tuple< Key, Value >& operator*() {
         return currentBucket->tuple;
      }

      Tuple< Key, Value > *operator->() const {
         return &(currentBucket->tuple);
      }

      // Prefix ++
      Iterator &operator++() {
         if (currentBucket != nullptr && currentBucket->next != nullptr) {
            currentBucket = currentBucket->next;
            return *this;
         }

         do {
            ++bucketIndex;
            if (bucketIndex >= table->numberOfBuckets) {
               currentBucket = nullptr;
               return *this;
            }

            currentBucket = table->buckets[bucketIndex];
         } while (currentBucket == nullptr);
         return *this;
      }

      // Postfix ++
      Iterator operator++(int) {
         Iterator temp = *this;
         ++(*this);
         return temp;
      }

      bool operator==(const Iterator &rhs) const {
         return currentBucket == rhs.currentBucket;
      }

      bool operator!=(const Iterator &rhs) const {
         return !(*this == rhs);
      }
   };

   Iterator begin() {
      for (size_t i = 0; i < numberOfBuckets; i++) {
         if (buckets[i] != nullptr) {
            return Iterator(this, i, buckets[i]);
         }
      }
      return end();
   }

   Iterator end() {
      return Iterator(this, numberOfBuckets, nullptr);
   }
};
