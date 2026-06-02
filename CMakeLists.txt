cmake_minimum_required(VERSION 3.10)
project(SignLanguagePipelineProject)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Threads REQUIRED)
find_package(OpenCV REQUIRED COMPONENTS core imgcodecs)

add_executable(SignLanguagePipeline
    src/main.cpp
    concurrency/src/ThreadPool.cpp
    pipeline/src/CameraCapture.cpp
    pipeline/src/Pipeline.cpp
    pipeline/src/RgaProcessor.cpp
)

target_include_directories(SignLanguagePipeline PRIVATE
    ${PROJECT_SOURCE_DIR}
    /usr/include/rga
    ${OpenCV_INCLUDE_DIRS}
)

target_link_libraries(SignLanguagePipeline PRIVATE
    Threads::Threads
    rga
    ${OpenCV_LIBS}
)
