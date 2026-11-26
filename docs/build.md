# Build

Three toolchains are used deliberately, so that any warning-level or
behavioral difference (thread-id formatting, `pthread` detection path,
etc.) surfaces early rather than at benchmark time. Each toolchain gets
its own build directory — CMake caches the generator + compiler choice
in `CMakeCache.txt`, and reusing one directory across toolchains errors
out rather than switching.

Run all commands from the project root (where `CMakeLists.txt` lives).

## Requirements

- CMake >= 3.20
- A C++20 compiler with `<stop_token>` / `std::jthread` support: MSVC
  (VS Build Tools), or mingw64 g++ / clang++ with `bin/` on `PATH`
- Ninja on `PATH` for the GCC/Clang builds (`-G Ninja`)
- `git` on `PATH` and network access, only if you build the benchmarks
  (CMake downloads Google Benchmark)

## MSVC (Visual Studio Build Tools)

```powershell
cmake -S . -B build
cmake --build build --config Release -j
.\build\Release\rooster.exe
```

- No `-G` needed — CMake defaults to the installed Visual Studio
  generator on Windows.
- Multi-config generator: binaries land under `build\Debug\` or
  `build\Release\` depending on `--config` (defaults to `Debug` if
  omitted).

## GCC (mingw64 g++)

```powershell
cmake -S . -B build-gcc -G Ninja -DCMAKE_CXX_COMPILER=g++
cmake --build build-gcc -j
.\build-gcc\rooster.exe
```

## Clang (mingw64 clang++)

```powershell
cmake -S . -B build-clang -G Ninja -DCMAKE_CXX_COMPILER=clang++
cmake --build build-clang -j
.\build-clang\rooster.exe
```

## Tests

Tests are built by default (`-DTHREAD_POOL_BUILD_TESTS=OFF` to skip).

```powershell
# Ninja builds (GCC / Clang)
ctest --test-dir build-gcc --output-on-failure
# or run the binary directly
.\build-gcc\thread_pool_test.exe

# MSVC (multi-config: name the configuration)
ctest --test-dir build -C Release --output-on-failure
```

### Sanitizers (Linux / WSL)

Use a separate build directory per sanitizer. Not available with MSVC,
and unreliable with mingw — use WSL or a Linux machine.

```bash
cmake -S . -B build-tsan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DTHREAD_POOL_SANITIZE=thread
cmake --build build-tsan
ctest --test-dir build-tsan --output-on-failure
```

`-DTHREAD_POOL_SANITIZE=` accepts `thread`, `address` or `undefined`.
`ctest` loads `tsan.supp` automatically; it suppresses one known false
positive in libstdc++'s `exception_ptr` handling (see the comments in
that file). If you run the test binary directly under TSan, set
`TSAN_OPTIONS=suppressions=tsan.supp` yourself.

## Benchmarks

Benchmarks are off by default. Enabling them downloads Google Benchmark
(v1.9.1) at configure time. **Always build them in Release** — Debug
numbers are meaningless.

### Build

```powershell
# GCC
cmake -S . -B build-gcc-rel -G Ninja -DCMAKE_CXX_COMPILER=g++ -DCMAKE_BUILD_TYPE=Release -DTHREAD_POOL_BUILD_BENCHMARKS=ON
cmake --build build-gcc-rel -j

# Clang
cmake -S . -B build-clang-rel -G Ninja -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Release -DTHREAD_POOL_BUILD_BENCHMARKS=ON
cmake --build build-clang-rel -j

# MSVC (config is chosen at build time, not configure time)
cmake -S . -B build -DTHREAD_POOL_BUILD_BENCHMARKS=ON
cmake --build build --config Release -j
```

### Run

```powershell
# Ninja builds
.\build-gcc-rel\benchmarks\micro_bench.exe --benchmark_repetitions=5 --benchmark_report_aggregates_only=true

# MSVC
.\build\benchmarks\Release\micro_bench.exe --benchmark_repetitions=5 --benchmark_report_aggregates_only=true
```

Useful flags:

| Flag | Purpose |
|---|---|
| `--benchmark_repetitions=N` | Repeat each benchmark N times; with `--benchmark_report_aggregates_only=true` prints mean / median / stddev / cv only |
| `--benchmark_filter=REGEX` | Run a subset, e.g. `--benchmark_filter=RoundTrip` |
| `--benchmark_min_time=0.5s` | Run each benchmark longer for steadier numbers |
| `--benchmark_out=results.json --benchmark_out_format=json` | Save machine-readable results |

### What is measured

All benchmarks submit **empty tasks**, so they measure the pool's own
overhead (queueing, wake-up, type erasure, future plumbing), not useful
work.

| Benchmark | Measures |
|---|---|
| `BM_Throughput_Counter` | Tasks/sec, completion tracked with an atomic counter |
| `BM_Throughput_Futures` | Tasks/sec, every future collected and waited on |
| `BM_RoundTrip_Idle` | Submit one task to an idle (sleeping) pool and wait: wake-up latency |
| `BM_ThreadPerTask` | Cost of creating and joining one OS thread; the reason pools exist |

The throughput and round-trip benchmarks run against two pools:
`thread_pool::ThreadPool` (`std::jthread` + `condition_variable_any`)
and `bench::CvPool` (`std::thread` + `condition_variable`, the
pre-`jthread` design), so differences between them are attributable to
that one change.

All benchmarks use real (wall-clock) time: the work happens on worker
threads, so the benchmark thread's CPU time would be meaningless.

### Getting trustworthy numbers

- Release build, quiet machine, plugged in, high-performance power plan,
  other applications closed.
- Read the `cv` row (coefficient of variation) in the aggregates. **Above
  about 5%, treat that row as unreliable** and rerun or increase
  `--benchmark_repetitions` / `--benchmark_min_time`.
- With one submitting thread, use at most `hardware_concurrency() - 1`
  workers for a fair row; the largest worker count in the default sweep
  oversubscribes the CPU.
- Check your thread model with `g++ -v` ("Thread model: posix" or
  "win32"). mingw-w64's posix model uses winpthreads, whose mutexes and
  condition variables can behave very differently from MSVC's native
  ones, so compare toolchains before drawing conclusions.
- Never compare absolute numbers across machines or toolchains; compare
  ratios within one run.

## Known cross-toolchain differences (not bugs)

- **`Threads` detection path**: GCC's mingw64 runtime exposes
  `pthread_create` without an extra link flag; Clang on the same
  mingw64 sysroot needs `-lpthread`, which CMake finds and links
  automatically. Both report `Found Threads: TRUE` — different path,
  same correct outcome.
- **`std::thread::id` printed values**: MSVC's STL prints the raw
  Win32 thread ID (large, OS-assigned numbers); mingw64's libstdc++
  prints its own internal handle-derived value (small, sequential
  numbers). The standard only guarantees that `thread::id` is
  comparable, hashable and orderable, and says nothing about its printed
  form, so don't compare raw ID values across compilers. Use them only
  to distinguish threads within a single run.