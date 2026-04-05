// A deque for crawler to write HTML parser to and for index worker to read from
// + wrappers for threading and fault tolerance

// TODO: Change std::deque to self written one

#include <deque>
#include <optional>

#include "./html_parser.h"
#include "utils/threads/condition_variable.hpp"
#include "utils/threads/lock_guard.hpp"
#include "utils/threads/mutex.hpp"
#include "utils/vector.hpp"

class IndexQueue {
  public:
    IndexQueue();
    IndexQueue(vector<HtmlParser> items);
    ~IndexQueue() = default;

    void push(HtmlParser &parsed);

    std::optional<HtmlParser> pop();
    void shutdown();

    vector<HtmlParser> snapshot() const;

  private:
    std::deque<HtmlParser> queue;
    mutable mutex m;
    condition_variable cv;
    bool closed;
    std::size_t pending;
};