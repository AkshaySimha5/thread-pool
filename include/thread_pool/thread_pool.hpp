#pragma once

#include <cstddef>
#include <functional>
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <utility>

namespace thread_pool{
/**
 * @brief A fixed-size thread pool for executing tasks asynchronously.
 *
 * High-level guarantees (initial version):
 * - Starts N worker threads on construction.
 * - Allows submitting tasks for execution.
 * - Ensures all worker threads are stopped on destruction.
 *
 * Non-goals (for now):
 * - No futures / return values
 * - No task dependencies
 * - No work stealing
 *
 * This is intentionally minimal.
 */
class ThreadPool{
public:
    /**
     * @brief Construct a thread pool with a fixed number of worker threads.
     *
     * @param thread_count Number of worker threads to start.
     *
     * Requirements / assumptions:
     * - thread_count > 0
     * - Threads start immediately and wait for work.
     */
    explicit ThreadPool(std::size_t thread_count);

    /**
     * @brief Destructor.
     *
     * Guarantees:
     * - No worker thread is running after destruction.
     * - All internal resources are released.
     *
     * Open question (to be decided later):
     * - Do we finish queued tasks or discard them?
     */
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    // copy operations deleted: forbidding duplicating threads, sharing queues, undefined ownerships

    /**
     * @brief Submit a task for execution.
     *
     * The task:
     * - Must be callable with no arguments.
     * - Must not throw (for now).
     *
     * Behavior:
     * - The task is queued and eventually executed by a worker thread.
     * - This call should be thread-safe.
     *
     * Open question:
     * - What happens if submit() is called during shutdown?
     */
    void submit(std::function<void()> task);

private:
    // Internal helper run by each worker thread.
    void worker_loop();

private:
    std::size_t thread_count_;

    // Worker threads (std::thread for now; will migrate to std::jthread later)
    // Example: std::vector<std::jthread>

    std::vector<std::thread> workers_;

    // Task queue (exact data structure decided later)
    // Example: std::queue<std::function<void()>>
    
    std::queue<std::function<void()>> tasks_;

    // Synchronization primitives will live here
    // Example: mutex, condition_variable, atomic flags

    std::mutex mutex_;
    std::condition_variable condition_;
    bool stop_;
};
} // namespace thread_pool