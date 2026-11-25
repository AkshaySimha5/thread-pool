#include <iostream>
#include <stdexcept>
#include <syncstream>
#include <thread>
#include <vector>

#include "thread_pool/thread_pool.hpp"

int main() {
    const unsigned hw = std::thread::hardware_concurrency();
    thread_pool::ThreadPool pool(hw != 0 ? hw : 4);

    // Fire-and-forget style (future ignored)
    for (int i = 0; i < 5; ++i) {
        pool.submit([i] {
            std::osyncstream(std::cout) << "Task " << i << " on thread "
                                        << std::this_thread::get_id() << "\n";
        });
    }

    // Tasks with return values + arguments
    std::vector<std::future<int>> squares;
    for (int i = 1; i <= 5; ++i) {
        squares.push_back(pool.submit([](int x) { return x * x; }, i));
    }
    for (auto& f : squares) std::osyncstream(std::cout) << f.get() << ' ';
    std::osyncstream(std::cout) << '\n';

    // Exception propagation through the future
    auto bad = pool.submit([]() -> int { throw std::runtime_error("boom"); });
    try {
        bad.get();
    } catch (const std::exception& e) {
        std::cout << "caught from task: " << e.what() << '\n';
    }
}