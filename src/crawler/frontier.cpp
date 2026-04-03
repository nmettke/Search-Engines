#include "frontier.h"

Frontier::Frontier(const std::string seed_list_str) {
    std::ifstream seedList(seed_list_str);
    if (!seedList.is_open()) {
        throw std::runtime_error("seedList could not be opened");
    }

    std::string line;
    while (std::getline(seedList, line)) {
        pq.emplace(string(line.c_str()));
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
    pq.emplace(url);
}

std::optional<FrontierItem> Frontier::pop() {
    lock_guard guard(m);
    if (pq.empty()) {
        return std::nullopt;
    } else {
        FrontierItem val = pq.top();
        pq.pop();
        return val;
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