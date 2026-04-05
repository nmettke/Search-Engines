#include "indexQueue.h"

IndexQueue::IndexQueue(){};

IndexQueue::IndexQueue(vector<HtmlParser> items) {
    for (size_t i = 0; i < items.size(); ++i) {
        queue.push_front(items[i]);
    }
}

void IndexQueue::push(HtmlParser &parsed) {
    m.lock();
    if (closed) {
        return;
    }
    queue.emplace_front(parsed);
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
        HtmlParser val = queue.back();
        queue.pop_back();
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