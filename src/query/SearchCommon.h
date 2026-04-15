#pragma once

// exposes broker_server types

#include <arpa/inet.h>
#include <cstdio>
#include <cstring>
#include <sys/socket.h>
#include <unistd.h>

#include "../utils/string.hpp"
#include "../utils/vector.hpp"

struct GlobalResult {
    string url;
    string title;
    string snippet;
    double score;

    bool operator>(const GlobalResult &other) const { return score > other.score; }
    bool operator<(const GlobalResult &other) const { return score < other.score; }
};

class GlobalTopKHeap {
  private:
    ::vector<GlobalResult> heap_;
    size_t k_;

    void heapifyUp(int index) {
        while (index > 0) {
            int parent = (index - 1) / 2;
            if (heap_[index].score < heap_[parent].score) {
                GlobalResult temp = heap_[index];
                heap_[index] = heap_[parent];
                heap_[parent] = temp;
                index = parent;
            } else {
                break;
            }
        }
    }

    void heapifyDown(int index) {
        int size = heap_.size();
        while (true) {
            int left = 2 * index + 1;
            int right = 2 * index + 2;
            int smallest = index;
            if (left < size && heap_[left].score < heap_[smallest].score)
                smallest = left;
            if (right < size && heap_[right].score < heap_[smallest].score)
                smallest = right;
            if (smallest != index) {
                GlobalResult temp = heap_[index];
                heap_[index] = heap_[smallest];
                heap_[smallest] = temp;
                index = smallest;
            } else {
                break;
            }
        }
    }

  public:
    GlobalTopKHeap(size_t k) : k_(k) { heap_.reserve(k + 1); }

    void push(const GlobalResult &item) {
        if (heap_.size() < k_) {
            heap_.pushBack(item);
            heapifyUp(heap_.size() - 1);
        } else if (item.score > heap_[0].score) {
            heap_[0] = item;
            heapifyDown(0);
        }
    }

    ::vector<GlobalResult> extractSorted() {
        ::vector<GlobalResult> sorted;
        while (!heap_.empty()) {
            sorted.pushBack(heap_[0]);
            heap_[0] = heap_.back();
            heap_.popBack();
            if (!heap_.empty())
                heapifyDown(0);
        }
        size_t n = sorted.size();
        for (size_t i = 0; i < n / 2; ++i) {
            GlobalResult temp = sorted[i];
            sorted[i] = sorted[n - 1 - i];
            sorted[n - 1 - i] = temp;
        }
        return sorted;
    }
};

struct WorkerArgs {
    string ip;
    int port;
    string query;
    size_t k;
    ::vector<GlobalResult> local_results;
};

// Function declarations — defined in broker_server.cpp
string to_string(double score);
string to_string(size_t n);
string url_decode(const string &src);
void *fetch_from_worker(void *args);
