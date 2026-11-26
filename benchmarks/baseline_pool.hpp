#pragma once
// Baseline for A/B comparison: the SAME design as thread_pool::ThreadPool
// (single mutex + queue, detail::Task, packaged_task futures) but built on
// std::thread + std::condition_variable + a plain bool stop flag, i.e. the
// pre-jthread design. Any throughput/latency difference between this and
// ThreadPool is therefore attributable to jthread/stop_token/condition_variable_any.
#include <condition_variable>
#include <cstddef>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <type_traits>
#include <vector>

#include "thread_pool/detail/task.hpp"

namespace bench {

class CvPool {
public:
    explicit CvPool(std::size_t n) {
        for (std::size_t i = 0; i < n; ++i) workers_.emplace_back([this] { loop(); });
    }
    ~CvPool() {
        {
            std::lock_guard<std::mutex> lock(m_);
            stop_ = true;
        }
        cv_.notify_all();
        for (auto& w : workers_) w.join();
    }
    CvPool(const CvPool&) = delete;
    CvPool& operator=(const CvPool&) = delete;

    template <typename F>
    auto submit(F&& f) -> std::future<std::invoke_result_t<std::decay_t<F>>> {
        using R = std::invoke_result_t<std::decay_t<F>>;
        std::packaged_task<R()> pt(std::forward<F>(f));
        auto fut = pt.get_future();
        {
            std::lock_guard<std::mutex> lock(m_);
            q_.push(thread_pool::detail::Task(std::move(pt)));
        }
        cv_.notify_one();
        return fut;
    }

private:
    void loop() {
        while (true) {
            thread_pool::detail::Task t;
            {
                std::unique_lock<std::mutex> lock(m_);
                cv_.wait(lock, [this] { return stop_ || !q_.empty(); });
                if (stop_ && q_.empty()) return;
                t = std::move(q_.front());
                q_.pop();
            }
            t();
        }
    }

    std::queue<thread_pool::detail::Task> q_;
    std::mutex m_;
    std::condition_variable cv_;
    bool stop_ = false;
    std::vector<std::thread> workers_;
};

}  // namespace bench