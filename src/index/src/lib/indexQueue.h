// A deque for crawler to write HTML parser to and for index worker to read from
// + wrappers for threading and fault tolerance

#include "utils/STL_rewrite/deque.hpp"
#include <optional>

// #include "./html_parser.h"
#include "parser/HtmlParser.h"
#include "utils/threads/condition_variable.hpp"
#include "utils/threads/lock_guard.hpp"
#include "utils/threads/mutex.hpp"
#include "utils/vector.hpp"

class IndexQueue {
  public:
    struct Stats {
        std::size_t itemCount = 0;
        std::size_t approxBytes = 0;
    };

    explicit IndexQueue(std::size_t maxQueuedItems = 1024);
    IndexQueue(::vector<HtmlParser> items, std::size_t maxQueuedItems = 1024);
    ~IndexQueue() = default;

    void push(HtmlParser &parsed);

    std::optional<HtmlParser> pop();
    Stats stats() const;
    void shutdown();

  private:
    ::deque<HtmlParser> queue;
    mutable ::mutex m;
    ::condition_variable cv;
    bool closed;
    std::size_t pending;
    std::size_t approxQueuedBytes;
    std::size_t maxQueuedItems;
};
