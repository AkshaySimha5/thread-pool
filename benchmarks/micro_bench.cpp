// Micro-benchmarks: scheduling overhead of the pool itself, using EMPTY tasks
// so the measured cost is queueing + wake-up + type erasure + future plumbing,
// not useful work.
//
// IMPORTANT: every benchmark uses UseRealTime(). Work happens on worker
// threads, so the default (CPU time of the benchmark thread) would be
// meaningless. Build with -DCMAKE_BUILD_TYPE=Release (or --config Release).
#include <benchmark/benchmark.h>

#include <atomic>
#include <cstdint>
#include <future>
#include <thread>
#include <vector>

#include "baseline_pool.hpp"
#include "thread_pool/thread_pool.hpp"

namespace {

constexpr int kBatch = 10'000;  // tasks submitted per benchmark iteration

// 1, 2, 4, ... up to hardware_concurrency (always includes hw itself).
void ThreadArgs(benchmark::internal::Benchmark* b) {
    const unsigned hw = std::max(1u, std::thread::hardware_concurrency());
    for (unsigned t = 1; t < hw; t *= 2) b->Arg(t);
    b->Arg(hw);
}

// Throughput, completion tracked with a shared counter (no per-task future
// collected). Note submit() still builds a packaged_task and returns a future
// that we discard - that cost is inherent to the current API (a future-less
// post() would be a worthwhile later addition, and this benchmark will show
// how much it would save).
template <typename Pool>
void BM_Throughput_Counter(benchmark::State& state) {
    Pool pool(static_cast<std::size_t>(state.range(0)));
    std::atomic<int> remaining{0};
    for (auto _ : state) {
        remaining.store(kBatch, std::memory_order_relaxed);
        for (int i = 0; i < kBatch; ++i) {
            pool.submit([&remaining] {
                if (remaining.fetch_sub(1, std::memory_order_acq_rel) == 1) remaining.notify_one();
            });
        }
        for (int v; (v = remaining.load(std::memory_order_acquire)) != 0;) remaining.wait(v);
    }
    state.SetItemsProcessed(state.iterations() * kBatch);
}

// Throughput, collecting and waiting on every future (what a typical user does).
template <typename Pool>
void BM_Throughput_Futures(benchmark::State& state) {
    Pool pool(static_cast<std::size_t>(state.range(0)));
    std::vector<std::future<void>> futs;
    futs.reserve(kBatch);
    for (auto _ : state) {
        futs.clear();
        for (int i = 0; i < kBatch; ++i) futs.push_back(pool.submit([] {}));
        for (auto& f : futs) f.get();
    }
    state.SetItemsProcessed(state.iterations() * kBatch);
}

// Round trip on an otherwise IDLE pool: submit one empty task, wait for it.
// Dominated by waking a sleeping worker (futex wake + scheduler latency), so
// it is the number most sensitive to the condition-variable implementation.
template <typename Pool>
void BM_RoundTrip_Idle(benchmark::State& state) {
    Pool pool(static_cast<std::size_t>(state.range(0)));
    for (auto _ : state) pool.submit([] {}).get();
}

// Why pools exist: cost of creating and joining an OS thread per task.
void BM_ThreadPerTask(benchmark::State& state) {
    for (auto _ : state) {
        std::thread t([] {});
        t.join();
    }
}

}  // namespace

using thread_pool::ThreadPool;
using bench::CvPool;

BENCHMARK_TEMPLATE(BM_Throughput_Counter, ThreadPool)->Apply(ThreadArgs)->UseRealTime();
BENCHMARK_TEMPLATE(BM_Throughput_Counter, CvPool)->Apply(ThreadArgs)->UseRealTime();
BENCHMARK_TEMPLATE(BM_Throughput_Futures, ThreadPool)->Apply(ThreadArgs)->UseRealTime();
BENCHMARK_TEMPLATE(BM_Throughput_Futures, CvPool)->Apply(ThreadArgs)->UseRealTime();
BENCHMARK_TEMPLATE(BM_RoundTrip_Idle, ThreadPool)->Arg(1)->Arg(4)->UseRealTime();
BENCHMARK_TEMPLATE(BM_RoundTrip_Idle, CvPool)->Arg(1)->Arg(4)->UseRealTime();
BENCHMARK(BM_ThreadPerTask)->UseRealTime();

BENCHMARK_MAIN();