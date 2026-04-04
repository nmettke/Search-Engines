#include "ipc/unix_socket.h"
#include "ipc/wire_document.h"
#include "lib/Common.h"
#include "utils/threads/condition_variable.hpp"
#include "utils/threads/mutex.hpp"

#include <pthread.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

constexpr const char *kDefaultMasterSocket = "/tmp/search_engine_index.sock";
constexpr std::size_t kDefaultBatchSize = 32;
constexpr int kReconnectDelayMs = 250;

struct WorkerState {
    explicit WorkerState(std::string socket_path) : socket_path(std::move(socket_path)) {}

    std::string socket_path;
    std::deque<WireDocument> queue;
    ::mutex m;
    ::condition_variable cv; // shutdown or batch queued
    bool shutdown = false;
    std::size_t docs_sent = 0;
    std::size_t batches_sent = 0;
};

struct DispatchThreadArgs {
    WorkerState *state = nullptr;
    std::size_t batch_size = 0;
};

bool flushBatchToWorker(WorkerState &state, int &worker_fd, std::vector<WireDocument> &batch) {
    if (batch.empty()) {
        return true;
    }

    bool connected = false;
    if (worker_fd >= 0) {
        connected = true;
    } else {
        worker_fd = ipcUnixConnect(state.socket_path.c_str());
        if (worker_fd >= 0) {
            std::cout << "Master connected to worker socket " << state.socket_path << "\n";
            connected = true;
        } else {
            std::cerr << "Master failed to connect to worker socket " << state.socket_path
                      << "; retrying.\n";
            connected = false;
        }
    }

    if (!connected) {
        return false;
    }

    std::vector<std::uint8_t> payload;
    if (!wireEncodeDocumentBatch(batch, payload)) {
        std::cerr << "Failed to encode worker batch for " << state.socket_path << "\n";
        return false;
    }

    if (!wireSendFramedMessage(worker_fd, payload)) {
        std::cerr << "Failed to send batch to worker " << state.socket_path << "; reconnecting.\n";
        if (worker_fd >= 0) {
            ::close(worker_fd);
            worker_fd = -1;
        }
        return false;
    }

    state.docs_sent += batch.size();
    state.batches_sent += 1;
    batch.clear();
    return true;
}

void takeBatch(WorkerState &state, std::vector<WireDocument> &batch, std::size_t batch_size) {
    // takes a batch from queue, assumes state lock is acquired
    const std::size_t take = std::min(batch_size, state.queue.size());
    for (std::size_t i = 0; i < take; ++i) {
        batch.push_back(std::move(state.queue.front()));
        state.queue.pop_front();
    }
}

void *dispatchThreadMain(void *raw_args) {
    DispatchThreadArgs *args = static_cast<DispatchThreadArgs *>(raw_args);
    WorkerState &state = *args->state;
    const std::size_t batch_size = args->batch_size;
    int worker_fd = -1;
    std::vector<WireDocument> batch;
    batch.reserve(batch_size);

    while (true) {
        state.m.lock();
        while (state.queue.empty() && !state.shutdown) {
            state.cv.wait(state.m);
        }
        if (state.queue.empty() && state.shutdown) {
            state.m.unlock();
            break;
        }
        takeBatch(state, batch, batch_size);
        state.m.unlock();

        while (!flushBatchToWorker(state, worker_fd, batch)) {
            // if flushing batch to worker fails, we put batch back onto queue
            state.m.lock();

            for (auto it = batch.rbegin(); it != batch.rend(); ++it) {
                state.queue.push_front(std::move(*it));
            }
            batch.clear();

            usleep(kReconnectDelayMs * 1000);

            if (state.queue.empty() && state.shutdown) {
                state.m.unlock();
                break;
            }
            takeBatch(state, batch, batch_size);
            state.m.unlock();
        }
    }

    if (worker_fd >= 0) {
        ::close(worker_fd);
        worker_fd = -1;
    }

    return nullptr;
}

int ingestLoop(const char *listen_path, std::deque<WorkerState> &workers) {
    // takes in the documents from crawler
    if (workers.empty()) {
        std::cerr << "Failed: No worker thread created\n";
        return 1;
    }
    int listen_fd = ipcUnixListen(listen_path);
    if (listen_fd < 0) {
        std::cerr << "Failed to bind master socket: " << listen_path << "\n";
        return 1;
    }

    std::cout << "Index master listening on " << listen_path << "\n";
    std::cout << "Configured " << workers.size() << " worker shard(s).\n";

    int client_fd = ::accept(listen_fd, nullptr, nullptr);

    if (listen_fd >= 0) {
        ::close(listen_fd);
        listen_fd = -1;
    }

    if (client_fd < 0) {
        std::cerr << "Connection to socket failed: " << client_fd << "\n";
        return 1;
    }

    std::vector<std::uint8_t> payload;
    std::vector<WireDocument> docs;
    std::size_t ingested = 0;

    while (wireRecvFramedMessage(client_fd, payload)) {
        if (!wireDecodePayloadDocuments(payload.data(), payload.size(), docs)) {
            std::cerr << "Master received malformed payload; closing input stream.\n";
            break;
        }

        for (auto &doc : docs) {
            // Put the document onto specific worker queue
            const std::size_t shard = hashString(doc.base.c_str()) % workers.size();
            WorkerState &worker = workers[shard];
            worker.m.lock();
            worker.queue.push_back(std::move(doc));
            worker.m.unlock();
            worker.cv.notify_one();
            ++ingested;
        }
    }

    if (client_fd >= 0) {
        ::close(client_fd);
        client_fd = -1;
    }
    std::cout << "Master ingested " << ingested << " document(s).\n";
    return 0;
}

int main(int argc, char **argv) {
    const char *listen_path;
    const std::size_t batch_size = kDefaultBatchSize; // to be changed
    std::deque<WorkerState> workers;

    for (int i = 2; i < argc; ++i) {
        if (argv[i] && argv[i][0] != '\0')
            workers.emplace_back(argv[i]);
    }

    if (workers.empty()) {
        std::cerr << "Usage: " << argv[0] << " master_socket worker_socket [worker_socket ...]\n";
        return 1;
    }

    if (argc >= 2) {
        listen_path = argv[1];
    } else {
        listen_path = kDefaultMasterSocket;
    }

    std::vector<pthread_t> dispatch_threads(workers.size());
    std::vector<DispatchThreadArgs> dispatch_args(workers.size());
    std::size_t threads_started = 0;

    for (std::size_t i = 0; i < workers.size(); ++i) {
        dispatch_args[i].state = &workers[i];
        dispatch_args[i].batch_size = batch_size;
        const int create_status =
            pthread_create(&dispatch_threads[i], nullptr, dispatchThreadMain, &dispatch_args[i]);
        if (create_status != 0) {
            std::cerr << "Failed to create dispatch thread for worker " << workers[i].socket_path
                      << "\n";
            for (auto &worker : workers) {
                // shutdown all workers
                worker.m.lock();
                worker.shutdown = true;
                worker.m.unlock();
                worker.cv.notify_one();
            }
            for (std::size_t j = 0; j < threads_started; ++j) {
                pthread_join(dispatch_threads[j], nullptr);
            }
            return 1;
        }
        ++threads_started;
    }

    const int status = ingestLoop(listen_path, workers);

    // Finished taking in docs (loop returned)
    for (auto &worker : workers) {
        // shutdown all workers
        worker.m.lock();
        worker.shutdown = true;
        worker.m.unlock();
        worker.cv.notify_one();
    }

    for (std::size_t i = 0; i < threads_started; ++i) {
        pthread_join(dispatch_threads[i], nullptr);
    }

    for (std::size_t i = 0; i < workers.size(); ++i) {
        std::cout << "Worker[" << i << "] " << workers[i].socket_path << ": "
                  << workers[i].docs_sent << " docs across " << workers[i].batches_sent
                  << " batch(es)\n";
    }

    return status;
}
