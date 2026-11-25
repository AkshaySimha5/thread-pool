cmake_minimum_required(VERSION 3.20)

project(thread_pool LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(THREAD_POOL_BUILD_TESTS "Build tests" ON)
set(THREAD_POOL_SANITIZE "" CACHE STRING "Sanitizer: thread, address, or undefined (empty = none)")

find_package(Threads REQUIRED)

# The pool is a library; the example and tests link against it.
add_library(thread_pool STATIC src/thread_pool.cpp)
target_include_directories(thread_pool PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(thread_pool PUBLIC Threads::Threads)

add_executable(rooster examples/main.cpp)
target_link_libraries(rooster PRIVATE thread_pool)

if (THREAD_POOL_BUILD_TESTS)
  enable_testing()
  add_executable(thread_pool_test tests/thread_pool_test.cpp)
  target_link_libraries(thread_pool_test PRIVATE thread_pool)
  add_test(NAME thread_pool_test COMMAND thread_pool_test)
endif()

set(_targets thread_pool rooster)
if (THREAD_POOL_BUILD_TESTS)
  list(APPEND _targets thread_pool_test)
endif()

foreach(t IN LISTS _targets)
  if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${t} PRIVATE -pedantic-errors -Wall -Wextra -Wconversion -Wsign-conversion)
    if (THREAD_POOL_SANITIZE)
      target_compile_options(${t} PUBLIC -fsanitize=${THREAD_POOL_SANITIZE} -g)
      target_link_options(${t} PUBLIC -fsanitize=${THREAD_POOL_SANITIZE})
    endif()
  elseif (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(${t} PRIVATE /permissive- /W4)
  endif()
endforeach()