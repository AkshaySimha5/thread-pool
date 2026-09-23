cmake_minimum_required(VERSION 4.0)

project(thread_pool LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(rooster
  src/main.cpp
  src/scheduler.cpp
  src/thread_pool.cpp
  src/worker.cpp
)

target_include_directories(rooster
  PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(rooster PRIVATE
    -pedantic-errors
    -Wall
    -Wextra
    -Wconversion
    -Wsign-conversion
  )

elseif (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
  target_compile_options(rooster PRIVATE
    /permissive-
    /W4
  )

endif()

