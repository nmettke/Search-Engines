#include "DelayedQueue.h"

void DelayedQueue::push(const FrontierItem &item, int64_t readyAtMs) {
    lock_guard<mutex> guard(m);
    heap.push(Entry(item, readyAtMs));
}

vector<FrontierItem> DelayedQueue::drainReady(int64_t nowMs) {
    vector<FrontierItem> ready;
    lock_guard<mutex> guard(m);
    while (!heap.empty() && heap.top().readyAtMs <= nowMs) {
        ready.pushBack(heap.top().item);
        heap.pop();
    }
    return ready;
}

size_t DelayedQueue::size() const {
    lock_guard<mutex> guard(m);
    return heap.size();
}

bool DelayedQueue::empty() const {
    lock_guard<mutex> guard(m);
    return heap.empty();
}
