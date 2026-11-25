# Build

Three toolchains are used deliberately, so that any warning-level or
behavioral difference (thread-id formatting, `pthread` detection path,
etc.) surfaces early rather than at benchmark time. Each toolchain gets
its own build directory — CMake caches the generator + compiler choice
in `CMakeCache.txt`, and reusing one directory across toolchains errors
out rather than switching.

Run all commands from the project root (where `CMakeLists.txt` lives).

## MSVC (Visual Studio Build Tools)

```
cmake -S . -B build
cmake --build build --config Release -j
.\build\thread_pool_test.exe
.\build\Release\rooster.exe
```

- No `-G` needed — CMake defaults to the installed Visual Studio
  generator on Windows.
- Multi-config generator: binaries land under `build\Debug\` or
  `build\Release\` depending on `--config` (defaults to `Debug` if
  omitted).

## GCC (mingw64 g++)

```
cmake -S . -B build-gcc -G Ninja -DCMAKE_CXX_COMPILER=g++
cmake --build build-gcc -j
.\build-gcc\thread_pool_test.exe
.\build-gcc\rooster.exe
```

## Clang (mingw64 clang++)

```
cmake -S . -B build-clang -G Ninja -DCMAKE_CXX_COMPILER=clang++
cmake --build build-clang -j
.\build-clang\thread_pool_test.exe
.\build-clang\rooster.exe
```

## Requirements

- CMake >= 4.0
- A C++20 compiler: MSVC (VS Build Tools), or mingw64 g++ / clang++
  with `bin/` on `PATH`
- Ninja on `PATH` for the GCC/Clang builds (`-G Ninja`)

## Known cross-toolchain differences (not bugs)

- **`Threads` detection path**: GCC's mingw64 runtime exposes
  `pthread_create` without an extra link flag; Clang on the same
  mingw64 sysroot needs `-lpthread`, which CMake finds and links
  automatically. Both report `Found Threads: TRUE` — different path,
  same correct outcome.
- **`std::thread::id` printed values**: MSVC's STL prints the raw
  Win32 thread ID (large, OS-assigned numbers); mingw64's libstdc++
  prints its own internal handle-derived value (small, sequential
  numbers). `thread::id` is only guaranteed comparable/hashable/
  orderable by the standard — never mind that the printed form matches
  across implementations. Don't compare raw ID values across compilers
  in benchmark output; only use them to distinguish threads within a
  single run.