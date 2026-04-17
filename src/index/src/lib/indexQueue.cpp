#include "indexQueue.h"

namespace {

std::size_t stringBytes(const ::string &value) {
    return value.capacity() + 1;
}

std::size_t stringVectorBytes(const ::vector<::string> &values) {
    std::size_t total = values.capacity() * sizeof(::string);
    for (const ::string &value : values) {
        total += stringBytes(value);
    }
    return total;
}

std::size_t linkBytes(const Link &link) {
    return sizeof(Link) + stringBytes(link.URL) + stringVectorBytes(link.anchorText);
}

std::size_t htmlParserBytes(const HtmlParser &parsed) {
    std::size_t total = sizeof(HtmlParser);
    total += stringVectorBytes(parsed.words);
    total += stringVectorBytes(parsed.titleWords);
    total += parsed.links.capacity() * sizeof(Link);
    for (const Link &link : parsed.links) {
        total += linkBytes(link);
    }
    total += stringBytes(parsed.base);
    total += stringBytes(parsed.sourceUrl);
    return total;
}

} // namespace

IndexQueue::IndexQueue(std::size_t maxQueuedItemsArg)
    : closed(false), pending(0), approxQueuedBytes(0), maxQueuedItems(maxQueuedItemsArg) {}

IndexQueue::IndexQueue(vector<HtmlParser> items, std::size_t maxQueuedItemsArg)
    : closed(false), pending(items.size()), approxQueuedBytes(0), maxQueuedItems(maxQueuedItemsArg) {
    for (size_t i = 0; i < items.size(); ++i) {
        approxQueuedBytes += htmlParserBytes(items[i]);
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
    approxQueuedBytes += htmlParserBytes(parsed);
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
        const std::size_t itemBytes = htmlParserBytes(queue.back());
        HtmlParser val = std::move(queue.back());
        queue.pop_back();
        if (pending > 0) {
            --pending;
        }
        if (approxQueuedBytes >= itemBytes) {
            approxQueuedBytes -= itemBytes;
        } else {
            approxQueuedBytes = 0;
        }
        cv.notify_all();
        m.unlock();
        return val;
    }
}

IndexQueue::Stats IndexQueue::stats() const {
    lock_guard<mutex> guard(m);
    Stats stats;
    stats.itemCount = pending;
    stats.approxBytes = approxQueuedBytes;
    return stats;
}

void IndexQueue::shutdown() {
    m.lock();
    closed = true;
    cv.notify_all();
    m.unlock();
}
