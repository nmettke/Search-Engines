#include "indexQueue.h"

IndexQueue::IndexQueue(std::size_t maxQueuedItemsArg)
    : closed(false), pending(0), maxQueuedItems(maxQueuedItemsArg) {}

IndexQueue::IndexQueue(vector<HtmlParser> items, std::size_t maxQueuedItemsArg)
    : closed(false), pending(items.size()), maxQueuedItems(maxQueuedItemsArg) {
    for (size_t i = 0; i < items.size(); ++i) {
        queue.push_front(std::move(items[i]));
    }
}

void IndexQueue::push(HtmlParser &parsed) {
    m.lock();
    while (!closed && queue.size() >= maxQueuedItems) {
        cv.wait(m);
    }
    if (closed) {
        m.unlock();
        return;
    }
    queue.emplace_front(parsed);
    ++pending;
    cv.notify_all();
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
        cv.notify_all();
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
