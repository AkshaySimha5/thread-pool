#pragma once

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include "thread_pool/detail/task.hpp"

namespace thread_pool {

/**
 * @brief A fixed-size thread pool for executing tasks asynchronously.
 *
 * Guarantees:
 * - Starts N worker threads on construction (N > 0, else invalid_argument).
 * - submit() is thread-safe and returns a std::future for the result.
 * - Exceptions thrown by a task are captured and rethrown from future::get();
 *   they never escape into (and kill) a worker thread.
 * - Shutdown DRAINS: the destructor lets workers finish every task already
 *   queued, then joins them. Queued tasks are never discarded.
 * - submit() after shutdown has begun throws std::runtime_error.
 *
 * Non-goals (for now): task dependencies, work stealing, priorities.
 */
class ThreadPool {
public:
    explicit ThreadPool(std::size_t thread_count);
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    // Not movable either: worker threads hold `this`, so moving the pool
    // would leave them pointing at the moved-from object.
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

    /**
     * @brief Submit a callable (plus arguments) for execution.
     *
     * Arguments are decay-copied/moved into the task (like std::thread and
     * std::async), so the task never holds dangling references. Use
     * std::ref explicitly if you really want reference semantics.
     *
     * @return future for the callable's result (std::future<void> for void).
     */
    template <typename F, typename... Args>
        requires std::invocable<std::decay_t<F>, std::decay_t<Args>...>
    auto submit(F&& f, Args&&... args)
        -> std::future<std::invoke_result_t<std::decay_t<F>, std::decay_t<Args>...>> {
        using R = std::invoke_result_t<std::decay_t<F>, std::decay_t<Args>...>;

        // packaged_task = callable + promise. Running it stores the return
        // value OR the thrown exception into the shared state behind the future.
        std::packaged_task<R()> pt(
            [fn = std::forward<F>(f),
             ... captured = std::forward<Args>(args)]() mutable -> R {
                return std::invoke(std::move(fn), std::move(captured)...);
            });

        std::future<R> result = pt.get_future();
        enqueue(detail::Task(std::move(pt)));  // packaged_task is move-only: fine for Task
        return result;
    }

private:
    void enqueue(detail::Task task);
    void worker_loop();

    std::vector<std::thread> workers_;
    std::queue<detail::Task> tasks_;

    std::mutex mutex_;
    std::condition_variable condition_;
    bool stop_ = false;  // guarded by mutex_
};

}  // namespace thread_pool