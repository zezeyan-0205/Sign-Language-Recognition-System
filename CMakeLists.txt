cmake_minimum_required(VERSION 3.10)
project(SignLanguagePipelineProject)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable(SignLanguagePipeline
    src/main.cpp
    concurrency/src/ThreadPool.cpp
    pipeline/src/Pipeline.cpp
)

target_include_directories(SignLanguagePipeline PRIVATE
    ${PROJECT_SOURCE_DIR}
)

find_package(Threads REQUIRED)
target_link_libraries(SignLanguagePipeline PRIVATE Threads::Threads)
