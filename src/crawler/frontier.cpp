#include "frontier.h"

Frontier::Frontier(const std::string seed_list_str) {
    std::ifstream seedList(seed_list_str);
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

Frontier::Frontier(std::vector<FrontierItem> items) {
    for (auto &item : items) {
        pq.push(std::move(item));
    }
}

std::vector<FrontierItem> Frontier::snapshot() const {
    lock_guard guard(m);
    auto pq_copy = pq;
    std::vector<FrontierItem> result;
    while (!pq_copy.empty()) {
        result.push_back(pq_copy.top());
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

bool Frontier::contains(const string &url) const { return true; }

std::size_t Frontier::size() const {
    lock_guard guard(m);
    return pq.size();
}

bool Frontier::empty() const {
    lock_guard guard(m);
    return pq.empty();
}