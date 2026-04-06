#include "indexQueue.h"

IndexQueue::IndexQueue() : closed(false), pending(0) {}

IndexQueue::IndexQueue(vector<HtmlParser> items) : closed(false), pending(items.size()) {
    for (size_t i = 0; i < items.size(); ++i) {
        queue.push_front(std::move(items[i]));
    }
}

void IndexQueue::push(HtmlParser &parsed) {
    m.lock();
    if (closed) {
        m.unlock();
        return;
    }
    queue.emplace_front(parsed);
    ++pending;
    cv.notify_one();
    m.unlock();
}

std::optional<HtmlParser> IndexQueue::pop() {
    m.lock();
    while (queue.empty() && !closed) {
        cv.wait(m);
    }

    if (queue.empty()) {
        m.unlock();
        return std::nullopt;
    } else {
        HtmlParser val = std::move(queue.back());
        queue.pop_back();
        if (pending > 0) {
            --pending;
        }
        m.unlock();
        return val;
    }
}

void IndexQueue::shutdown() {
    m.lock();
    closed = true;
    cv.notify_all();
    m.unlock();
}
