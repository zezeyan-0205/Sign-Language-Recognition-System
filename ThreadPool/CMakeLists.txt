cmake_minimum_required(VERSION 3.10)
project(ThreadPoolProject)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable(ThreadPool
    src/main.cpp
    src/ThreadPool.cpp
)

target_include_directories(ThreadPool PRIVATE
    ${PROJECT_SOURCE_DIR}/inc
)

find_package(Threads REQUIRED)
target_link_libraries(ThreadPool PRIVATE Threads::Threads)