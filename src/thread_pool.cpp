#include "thread_pool/thread_pool.hpp"
#include <functional>
#include <stdexcept>

namespace thread_pool {

ThreadPool::ThreadPool(std::size_t thread_count) 
    : thread_count_(thread_count), stop_(false) {
    for (std::size_t i=0;i < thread_count_; ++i) {
        workers_.emplace_back([this] {
            worker_loop();
        });
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

void ThreadPool::submit(std::function<void()> task) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if(stop_){
            throw std::runtime_error("submit on stopped ThreadPool");
        }
        tasks_.push(std::move(task));
    }

    condition_.notify_one();
}

void ThreadPool::worker_loop() {
    while (true){
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            condition_.wait(lock, [this] {
                return stop_ || !tasks_.empty();
            });

            if (stop_ && tasks_.empty()) {
                return; // exit thread
            }

            task = std::move(tasks_.front());
            tasks_.pop();
        }
        
        task(); // execute outside lock
    }
}
} // namespace thread_pool
