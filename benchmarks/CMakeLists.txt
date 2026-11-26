include(FetchContent)

# Keep the benchmark library lean: no tests, no install rules.
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
  benchmark
  GIT_REPOSITORY https://github.com/google/benchmark.git
  GIT_TAG        v1.9.1
)
FetchContent_MakeAvailable(benchmark)

add_executable(micro_bench micro_bench.cpp)
target_link_libraries(micro_bench PRIVATE thread_pool benchmark::benchmark)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(micro_bench PRIVATE -Wall -Wextra -Wconversion -Wsign-conversion)
elseif (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
  target_compile_options(micro_bench PRIVATE /permissive- /W4)
endif()