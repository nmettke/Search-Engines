#include "frontier.h"

Frontier::Frontier(const string &seed_list_str) {
    std::ifstream seedList(seed_list_str.c_str());
    if (!seedList.is_open()) {
        throw std::runtime_error("seedList could not be opened");
    }

    closed = false;
    pending = 0;

    std::string line;
    while (std::getline(seedList, line)) {
        pq.emplace(string(line.c_str()));
        ++pending;
    }

    if (pending == 0) {
        closed = true;
    }
}

Frontier::Frontier(vector<FrontierItem> items) {
    for (size_t i = 0; i < items.size(); ++i) {
        pq.push(items[i]);
    }
}

vector<FrontierItem> Frontier::snapshot() const {
    lock_guard guard(m);
    vector<FrontierItem> result;
    size_t n = pq.size();
    PriorityQueue<FrontierItem, FrontierItemCompare> pq_copy = pq;
    for (size_t i = 0; i < n; ++i) {
        result.pushBack(pq_copy.top());
        pq_copy.pop();
    }
    return result;
}

void Frontier::push(const string &url) {
    lock_guard guard(m);
    if (closed) {
        return;
    }
    pq.emplace(url);
    ++pending;
    cv.notify_one();
}

void Frontier::pushMany(const vector<string> &urls) {
    lock_guard guard(m);
    if (closed || urls.size() == 0) {
        return;
    }

    for (const string &url : urls) {
        pq.emplace(url);
        ++pending;
    }

    cv.notify_all();
}

void Frontier::pushDeferred(const vector<FrontierItem> &items) {
    lock_guard guard(m);
    if (items.size() == 0 || closed) {
        return;
    }
    // we dont change pending here. deferred items were popped earlier but their task was never
    // marked done, so putting them back on the heap is a just requueue
    for (size_t i = 0; i < items.size(); ++i) {
        pq.push(items[i]);
    }
    cv.notify_all();
}

std::optional<FrontierItem> Frontier::pop() {
    m.lock();
    while (pq.empty() && !closed) {
        cv.wait(m);
    }

    if (pq.empty()) {
        m.unlock();
        return std::nullopt;
    } else {
        FrontierItem val = pq.top();
        pq.pop();
        m.unlock();
        return val;
    }
}

void Frontier::taskDone() {
    lock_guard guard(m);
    if (pending == 0) {
        return;
    }

    --pending;
    if (pending == 0) {
        closed = true;
        cv.notify_all();
    }
}

void Frontier::shutdown() {
    lock_guard guard(m);
    closed = true;
    cv.notify_all();
}

bool Frontier::contains(const string &url) const { return true; }

size_t Frontier::size() const {
    lock_guard guard(m);
    return pq.size();
}

bool Frontier::empty() const {
    lock_guard guard(m);
    return pq.empty();
}