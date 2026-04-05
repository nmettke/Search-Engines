// A deque for crawler to write HTML parser to and for index worker to read from
// + wrappers for threading and fault tolerance

// TODO: Change std::deque to self written one

#include <deque>
#include <optional>

#include "./html_parser.h"
#include "utils/threads/condition_variable.hpp"
#include "utils/threads/lock_guard.hpp"
#include "utils/threads/mutex.hpp"

class IndexQueue {
  public:
    IndexQueue();
    ~IndexQueue();

    void push(const HtmlParser &parsed);

    std::optional<HtmlParser> pop();

  private:
    std::deque<HtmlParser> queue;
    mutable mutex m;
    condition_variable cv;
    bool closed;
    std::size_t pending;
};