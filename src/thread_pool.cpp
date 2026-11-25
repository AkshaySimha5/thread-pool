#include "thread_pool/thread_pool.hpp"

#include <stdexcept>

namespace thread_pool {

ThreadPool::ThreadPool(std::size_t thread_count) {
    if (thread_count == 0) {
        throw std::invalid_argument("ThreadPool requires at least one thread");
    }

    workers_.reserve(thread_count);
    try {
        for (std::size_t i = 0; i < thread_count; ++i) {
            workers_.emplace_back([this] { worker_loop(); });
        }
    } catch (...) {
        // Thread creation failed part-way. The destructor will NOT run for a
        // partially constructed object, and destroying a joinable std::thread
        // calls std::terminate - so stop and join the ones we did start.
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_ = true;
        }
        condition_.notify_all();
        for (auto& w : workers_) w.join();
        throw;
    }
}

ThreadPool::~ThreadPool() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
    }
    condition_.notify_all();

    for (auto& worker : workers_) {
        worker.join();
    }
}

void ThreadPool::enqueue(detail::Task task) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stop_) {
            throw std::runtime_error("submit on stopped ThreadPool");
        }
        tasks_.push(std::move(task));
    }
    condition_.notify_one();
}

void ThreadPool::worker_loop() {
    while (true) {
        detail::Task task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            condition_.wait(lock, [this] { return stop_ || !tasks_.empty(); });

            if (stop_ && tasks_.empty()) {
                return;  // drained: exit thread
            }

            task = std::move(tasks_.front());
            tasks_.pop();
        }

        task();  // run outside the lock; packaged_task captures any exception
    }
}

}  // namespace thread_pool