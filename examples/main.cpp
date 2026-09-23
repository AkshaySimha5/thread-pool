#include "thread_pool/thread_pool.hpp"
#include <iostream>
#include <syncstream>

int main(){
    thread_pool::ThreadPool pool(10);
    for(int i=0; i<15; ++i) {
        pool.submit([i] {
            std::osyncstream out(std::cout); // synchronized wrapper for std::cout, std::cout is thread-safe but not atomic
            out << "Task " << i << " on thread " << std::this_thread::get_id() << "\n";
            // osyncstream is a C++20 feature, convenience wrapper to synchronize threads writing to the same stream
        });
    }
}