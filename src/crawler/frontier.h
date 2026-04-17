#pragma once

#include "FrontierItem.h"
#include "utils/PriorityQueue.hpp"
#include "utils/hash/HashTable.h"
#include "utils/string.hpp"
#include "utils/threads/condition_variable.hpp"
#include "utils/threads/lock_guard.hpp"
#include "utils/threads/mutex.hpp"
#include "utils/vector.hpp"

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>

class Frontier {
  public:
    Frontier(const string &seed_list_str, bool autoCloseWhenDrained = true,
             size_t maxQueuedItems = 0);

    Frontier(vector<FrontierItem> items, bool autoCloseWhenDrained = true,
             size_t maxQueuedItems = 0);

    ~Frontier() = default;

    void push(const string &url);
    void push(const FrontierItem &item);
    void pushMany(const vector<string> &urls);
    void pushMany(const vector<FrontierItem> &items);

    // Push items that were previously popped and then deferred because of crawl delay... these
    // items pending count was never decremented, so this method must not increment it.
    void pushDeferred(const vector<FrontierItem> &items);
    void snoozeCurrent(const FrontierItem &item, std::int64_t readyAtMs);

    std::optional<FrontierItem> pop();

    void taskDone();
    void shutdown();

    bool contains(const string &url) const;

    size_t size() const;

    bool empty() const;

    bool hasInFlightWork() const;

    vector<FrontierItem> snapshot() const;

  private:
    template <typename T> struct BufferedQueue {
        vector<T> storage;
        std::size_t frontIndex = 0;

        bool empty() const { return frontIndex >= storage.size(); }

        std::size_t size() const { return empty() ? 0 : storage.size() - frontIndex; }

        T &front() { return storage[frontIndex]; }

        const T &front() const { return storage[frontIndex]; }

        T &operator[](std::size_t index) { return storage[frontIndex + index]; }

        const T &operator[](std::size_t index) const { return storage[frontIndex + index]; }

        void pushBack(const T &value) { storage.pushBack(value); }

        void pushBack(T &&value) { storage.pushBack(std::move(value)); }

        void pushFront(const T &value) {
            compactBeforeFrontInsert();
            storage.insert(storage.begin() + frontIndex, value);
        }

        void pushFront(T &&value) {
            compactBeforeFrontInsert();
            storage.insert(storage.begin() + frontIndex, std::move(value));
        }

        T popFront() {
            T value = std::move(storage[frontIndex]);
            ++frontIndex;
            maybeCompact();
            return value;
        }

        void eraseAt(std::size_t index) {
            const std::size_t absoluteIndex = frontIndex + index;
            for (std::size_t i = absoluteIndex + 1; i < storage.size(); ++i) {
                storage[i - 1] = std::move(storage[i]);
            }

            storage.popBack();
            maybeCompact();
        }

        template <typename Fn> void forEach(Fn fn) {
            for (std::size_t i = frontIndex; i < storage.size(); ++i) {
                fn(storage[i]);
            }
        }

        template <typename Fn> void forEach(Fn fn) const {
            for (std::size_t i = frontIndex; i < storage.size(); ++i) {
                fn(storage[i]);
            }
        }

      private:
        void compactBeforeFrontInsert() {
            if (frontIndex > 0 &&
                (frontIndex == storage.size() || frontIndex * 2 >= storage.size())) {
                compact();
            }
        }

        void maybeCompact() {
            if (frontIndex == 0) {
                return;
            }

            if (frontIndex >= storage.size()) {
                while (!storage.empty()) {
                    storage.popBack();
                }
                frontIndex = 0;
                return;
            }

            if (frontIndex < 32 || frontIndex * 2 < storage.size()) {
                return;
            }

            compact();
        }

        void compact() {
            const std::size_t activeSize = size();
            for (std::size_t i = 0; i < activeSize; ++i) {
                storage[i] = std::move(storage[frontIndex + i]);
            }

            while (storage.size() > activeSize) {
                storage.popBack();
            }

            frontIndex = 0;
        }
    };

    struct HostQueue {
        BufferedQueue<FrontierItem> items;
        std::int64_t blockedUntilMs = 0;
        std::uint64_t sleepGeneration = 0;
        bool inFlight = false;
        bool inReadyQueue = false;
        bool inSleepingQueue = false;
    };

    struct SleepingHost {
        std::int64_t readyAtMs = 0;
        std::uint64_t generation = 0;
        string hostKey;
    };

    struct PromotionCandidate {
        std::size_t index = 0;
        double score = 0.0;
    };

    struct SleepingHostCompare {
        bool operator()(const SleepingHost &a, const SleepingHost &b) const {
            return a.readyAtMs > b.readyAtMs;
        }
    };

    static std::int64_t nowMillis();
    static string extractHostKey(const string &url);

    // Requires: m is held.
    void pushInternal(const FrontierItem &item, bool countTowardsPending);

    // Requires: m is held.
    void doBackUp(); // backs up reservoir if full

    // Requires: m is held.
    void refillReservoirFromDiskBacked();

    // Requires: m is held.
    bool loadDiskBackedChunkFromDisk();

    void recoverDiskBackedChunkCount();
    string diskChunkPath(std::size_t chunkIndex, std::size_t chunkItemCount) const;
    bool findDiskChunkPath(std::size_t chunkIndex, string &path) const;

    bool writeDiskChunk(const vector<FrontierItem> &items, std::size_t chunkIndex) const;
    bool readDiskChunk(std::size_t chunkIndex, vector<FrontierItem> &items) const;

    // Requires: m is held.
    void enqueueScheduledItem(const FrontierItem &item, std::int64_t nowMs);

    // Requires: m is held.
    void scheduleHost(const string &hostKey, HostQueue &host, std::int64_t nowMs);

    // Requires: m is held.
    void promoteSleepingHosts(std::int64_t nowMs);

    // Requires: m is held.
    void promoteReservoir(std::int64_t nowMs);

    // Requires: m is held.
    void releaseActiveHost(std::int64_t nowMs);
    static string &activeHostKey();

    using HostTable = HashTable<string, HostQueue>;

    mutable HostTable hostQueues;
    BufferedQueue<string> readyHosts;
    PriorityQueue<SleepingHost, SleepingHostCompare> sleepingHosts;
    vector<FrontierItem> reservoir;
    vector<FrontierItem> disk_back_reservoir; // backup reservoir for when full
    std::size_t diskBackedChunksOnDisk = 0;
    string diskBackFilePrefix;
    std::size_t reservoirSweepCursor = 0;
    mutable mutex m;
    condition_variable cv;
    bool closed;
    bool autoCloseWhenDrained;
    std::size_t pending;
    std::size_t queued;
    std::size_t maxQueuedItems;
};
