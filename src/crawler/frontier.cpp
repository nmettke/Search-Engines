#include "frontier.h"

Frontier::Frontier(const std::string seed_list_str) {
    std::ifstream seedList(seed_list_str);
    if (!seedList.is_open()) {
        throw std::runtime_error("seedList could not be opened");
    }
    std::string line;
    while (std::getline(seedList, line)) {
        pq.emplace(line);
    }
}

void Frontier::push(const string &url) {
    lock_guard guard(m);
    pq.emplace(url);
}

std::optional<string> Frontier::pop() {
    lock_guard guard(m);
    if (Frontier::empty()) {
        return std::nullopt;
    } else {
        string val = pq.top();
        pq.pop();
        return val;
    }
}

bool Frontier::contains(const string &url) const {}

std::size_t Frontier::size() const {
    lock_guard guard(m);
    return pq.size();
}

bool Frontier::empty() const { return Frontier::size() == 0; }
