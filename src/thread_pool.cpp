#include "thread_pool/thread_pool.hpp"

#include <stdexcept>

namespace thread_pool {

ThreadPool::ThreadPool(std::size_t thread_count) {
    if (thread_count == 0) {
        throw std::invalid_argument("ThreadPool requires at least one thread");
    }
    // If emplace_back throws part-way, workers_ (declared last) is destroyed
    // first: each started jthread gets a stop request, wakes via the
    // stop_token-aware wait, drains nothing (queue is empty) and joins.
    workers_.reserve(thread_count);
    
    for (std::size_t i = 0; i < thread_count; ++i) {
            // jthread passes its own stop_token as the first argument when the
            // callable accepts one.
            workers_.emplace_back([this](std::stop_token st) { worker_loop(st); });
    }
}

ThreadPool::~ThreadPool() { shutdown(); }

void ThreadPool::shutdown() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;  // from here on, enqueue() rejects new tasks
    }
    // Order matters: stopping_ is set BEFORE stop is requested, so once a
    // worker can observe stop, no new task can be enqueued behind its back.
    for (auto& w : workers_) w.request_stop();  // wakes sleepers via the token
    for (auto& w : workers_) {
        if (w.joinable()) w.join();
    }
}

void ThreadPool::enqueue(detail::Task task) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) {
            throw std::runtime_error("submit on stopped ThreadPool");
        }
        tasks_.push(std::move(task));
    }
    condition_.notify_one();
}

void ThreadPool::worker_loop(std::stop_token st) {
    while (true) {
        detail::Task task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            // Returns the final value of the predicate: false means
            // "stop requested AND queue empty" -> fully drained, exit.
            // If stop is requested while tasks remain, the predicate is true,
            // so keep draining.
            if (!condition_.wait(lock, st, [this] { return !tasks_.empty(); })) {
                return;
            }
            task = std::move(tasks_.front());
            tasks_.pop();
        }

        task();  // run outside the lock; packaged_task captures any exception
    }
}

}  // namespace thread_pool