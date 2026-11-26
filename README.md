# thread-pool

A C++20 thread pool built from scratch, as a way to learn the language
features, concurrency model and memory model that real pools are built on,
and then to benchmark it honestly against established libraries.

> Status: work in progress. The core pool, tests and a first benchmark
> suite are done; work stealing and comparisons with other libraries are
> planned (see [Roadmap](#roadmap)).

## Features

- Fixed-size pool of `std::jthread` workers
- `submit(f, args...)` returns a `std::future<R>`; arguments are
  decay-copied, so tasks never hold dangling references
- Exceptions thrown by a task are captured and rethrown from
  `future::get()`; a throwing task never kills a worker
- Move-only, type-erased `Task` wrapper (needed because `packaged_task`
  is move-only, and `std::function` is not until C++23)
- Cooperative shutdown with `std::stop_token` and
  `std::condition_variable_any`
- Explicit, idempotent `shutdown()` with **drain** semantics: all accepted
  tasks run before workers exit; `submit()` after shutdown throws
- Tested under ThreadSanitizer, AddressSanitizer and UBSan (Linux)

## Usage

```cpp
#include <iostream>
#include <stdexcept>
#include "thread_pool/thread_pool.hpp"

int main() {
    thread_pool::ThreadPool pool(4);

    auto sum = pool.submit([](int a, int b) { return a + b; }, 20, 22);
    std::cout << sum.get() << '\n';  // 42

    auto bad = pool.submit([]() -> int { throw std::runtime_error("boom"); });
    try {
        bad.get();
    } catch (const std::exception& e) {
        std::cout << "caught: " << e.what() << '\n';
    }
}   // destructor drains queued tasks, then joins the workers
```

## Project layout

```
include/thread_pool/
    thread_pool.hpp        public API
    detail/task.hpp        move-only type-erased task
src/thread_pool.cpp
examples/main.cpp          small demo (target: rooster)
tests/                     dependency-free test runner (ctest)
benchmarks/                Google Benchmark suite + baseline pool
docs/build.md              build, test, sanitizer and benchmark commands
tsan.supp                  one documented TSan false-positive suppression
```

## Build and test

Requires CMake >= 3.20 and a C++20 compiler. Full per-toolchain commands
(MSVC, GCC, Clang, sanitizers, benchmarks) are in
[docs/build.md](docs/build.md). The short version:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Verified so far: GCC 13 on Linux (including TSan/ASan/UBSan) and
MinGW-w64 GCC 14.2 on Windows. MSVC and Clang builds of the current code
have not been re-verified yet.

## Design notes

- **Shutdown drains.** Workers exit only when stop is requested *and* the
  queue is empty, so every accepted task runs. Cancelling instead of
  draining is a possible later policy.
- **Order inside `shutdown()` matters.** The `stopping_` flag is set under
  the mutex *before* stop is requested, so no task can be enqueued after a
  worker has decided to exit.
- **`workers_` is the last member.** Members are destroyed in reverse
  order, so the jthreads (which request stop and join) are torn down
  before the queue, mutex and condition variable they use. This also makes
  a constructor that fails partway clean up correctly.
- **`condition_variable_any`** is used because it is the only condition
  variable with a `stop_token`-aware `wait()`, which removes the
  lost-wakeup race of a hand-rolled stop flag.
- **Cost per task** is currently two heap allocations (the `packaged_task`
  shared state and the `Task` model) plus one virtual call.

## Benchmarks

The suite measures the pool's own overhead using **empty tasks**, so it is
a worst case for scheduling cost, not a model of real workloads. Each
benchmark also runs against `CvPool`, an otherwise identical baseline built
on `std::thread` + `std::condition_variable`, so effects of the
`jthread`/`stop_token` design can be separated out. How to run: see
[docs/build.md](docs/build.md#benchmarks).

### First results

- Machine: AMD Ryzen 9 9900X (12 cores / 24 threads, Zen 5)
  - Base clock: 4.4 GHz; maximum boost: up to 5.6 GHz
  - Cache: 12 MiB L2 (1 MiB x12), 64 MiB L3
- Toolchain: MinGW-w64 GCC 14.2.0 on Windows, Release build
- Method: 5 repetitions, mean values. A warning sign marks rows whose
  coefficient of variation exceeds 5%; treat those as unreliable.
- A single thread submits all tasks.

**Throughput, empty tasks, completion tracked by counter (tasks/sec)**

| Workers | `ThreadPool` | `CvPool` |
|---|---|---|
| 1 | 2.54M ⚠ | 2.82M |
| 2 | 2.71M | 2.36M ⚠ |
| 4 | 497k ⚠ | 822k |
| 8 | 98.7k ⚠ | 148k ⚠ |
| 16 | 170k | 94.5k |
| 24 | 142k ⚠ | 93.8k |

**Throughput, every future collected and waited on (tasks/sec)**

| Workers | `ThreadPool` | `CvPool` |
|---|---|---|
| 1 | 1.71M | 1.46M |
| 2 | 1.48M ⚠ | 2.01M ⚠ |
| 4 | 377k ⚠ | 891k ⚠ |
| 8 | 96.2k ⚠ | 211k ⚠ |
| 16 | 91.3k | 96.6k ⚠ |
| 24 | 92.1k | 92.0k |

**Latency and baseline**

| Benchmark | `ThreadPool` | `CvPool` |
|---|---|---|
| Idle round trip, 1 worker | 10.9 µs | 10.7 µs |
| Idle round trip, 4 workers | 11.2 µs | 10.5 µs |
| One OS thread created and joined per task | 48.6 µs | |

### What the numbers show

- **Peak throughput is at 1 to 2 workers** (about 0.37 µs per task). Adding
  workers makes this workload slower: at 8 workers throughput is roughly
  25 times lower, at about 10 µs per task.
- **At 16 to 24 workers the cost per task (about 10.8 µs) is close to the
  idle round-trip time (about 10.9 µs).** A plausible explanation, not yet
  tested, is that empty tasks drain the queue instantly, workers go back to
  sleep, and nearly every `submit()` then pays a full wake-up.
- **A pool is much cheaper than a thread per task:** about 11 µs for an
  idle round trip and 0.4 µs per task under sustained load, against 48.6 µs
  to create and join a thread.
- **`condition_variable_any` versus `condition_variable` is inconclusive.**
  The faster pool changes with worker count and many rows are noisy.

### Caveats

- One machine and one toolchain. MinGW-w64 may use winpthreads, whose
  synchronization primitives can differ a lot from MSVC's native ones;
  MSVC results are still to come.
- One submitting thread, so the producer itself may be the bottleneck;
  these results cannot separate producer cost from lock contention.
- 24 workers plus the submitting thread means 25 application threads
  competing for 24 logical CPUs. The actual degree of oversubscription
  depends on how many workers are runnable versus sleeping.
- Empty tasks only. Task granularity (how long a task must run before extra
  workers help) is not measured yet.

## Roadmap

- [x] Fixed pool with mutex + condition variable
- [x] `std::future` results, move-only `Task`, exception propagation
- [x] `std::jthread` / `std::stop_token` shutdown, explicit `shutdown()`
- [x] Tests with sanitizer runs
- [x] Micro-benchmark harness with baseline pool
- [ ] Task-granularity benchmark
- [ ] Wake-up cost experiments: notify only when a worker sleeps, spin
      before sleeping, batched submit
- [ ] Future-less `post()` and small-buffer storage in `Task`
- [ ] Per-worker deques with work stealing (Chase-Lev), benchmarked on
      recursive fork-join workloads
- [ ] Latency percentiles (p50 / p99 / p999)
- [ ] Comparison against oneTBB, Taskflow, BS::thread_pool and Boost.Asio
- [ ] C++20 coroutine integration