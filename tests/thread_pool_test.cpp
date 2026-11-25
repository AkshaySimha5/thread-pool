// Dependency-free test runner (swap for GoogleTest/Catch2 later via FetchContent).
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "thread_pool/thread_pool.hpp"

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << "  "    \
                      << #cond << "\n";                                    \
            std::exit(1);                                                  \
        }                                                                  \
    } while (0)

using thread_pool::ThreadPool;

static int add(int a, int b) { return a + b; }

void returns_value() {
    ThreadPool pool(2);
    CHECK(pool.submit([] { return 42; }).get() == 42);
    CHECK(pool.submit(add, 2, 3).get() == 5);               // free function + args
    CHECK(pool.submit([](std::string s) { return s + "!"; }, std::string("hi")).get() == "hi!");
}

void void_task() {
    ThreadPool pool(2);
    std::atomic<int> n{0};
    pool.submit([&n] { ++n; }).get();
    CHECK(n == 1);
}

void exception_propagates() {
    ThreadPool pool(2);
    auto f = pool.submit([]() -> int { throw std::runtime_error("boom"); });
    bool caught = false;
    try { f.get(); } catch (const std::runtime_error& e) { caught = std::string(e.what()) == "boom"; }
    CHECK(caught);
    // Worker must have survived the throwing task.
    CHECK(pool.submit([] { return 1; }).get() == 1);
}

void move_only_args_and_callable() {
    ThreadPool pool(2);
    auto p = std::make_unique<int>(7);
    CHECK(pool.submit([](std::unique_ptr<int> q) { return *q; }, std::move(p)).get() == 7);
    auto cap = std::make_unique<int>(9);
    CHECK(pool.submit([c = std::move(cap)] { return *c; }).get() == 9);  // move-only callable
}

void many_tasks_all_run() {
    std::atomic<int> n{0};
    {
        ThreadPool pool(4);
        for (int i = 0; i < 10000; ++i) pool.submit([&n] { ++n; });
        // destructor must DRAIN the queue before returning
    }
    CHECK(n == 10000);
}

void concurrent_submitters() {
    ThreadPool pool(4);
    std::atomic<int> n{0};
    std::vector<std::thread> producers;
    for (int t = 0; t < 4; ++t)
        producers.emplace_back([&] { for (int i = 0; i < 1000; ++i) pool.submit([&n] { ++n; }); });
    for (auto& p : producers) p.join();
    // wait for completion via a final task per worker is racy; poll instead
    for (int spins = 0; n < 4000 && spins < 5000; ++spins)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    CHECK(n == 4000);
}

void zero_threads_rejected() {
    bool threw = false;
    try { ThreadPool pool(0); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
}

int main() {
    returns_value();
    void_task();
    exception_propagates();
    move_only_args_and_callable();
    many_tasks_all_run();
    concurrent_submitters();
    zero_threads_rejected();
    std::cout << "all tests passed\n";
}