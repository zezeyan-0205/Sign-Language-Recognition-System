cmake_minimum_required(VERSION 3.10)
project(SignLanguagePipelineProject)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Threads REQUIRED)
find_package(OpenCV REQUIRED COMPONENTS core highgui imgcodecs imgproc)
find_package(Qt5 COMPONENTS Core Widgets QUIET)

find_path(RKNN_INCLUDE_DIR rknn_api.h
    PATHS /usr/include /usr/include/rknn /usr/local/include
)
find_library(RKNNRT_LIBRARY rknnrt
    PATHS /usr/lib /usr/lib/aarch64-linux-gnu /usr/local/lib
)

add_executable(SignLanguagePipeline
    src/main.cpp
    concurrency/src/ThreadPool.cpp
    pipeline/src/CameraCapture.cpp
    pipeline/src/KeypointFeatureUtils.cpp
    pipeline/src/KeypointSequenceBuffer.cpp
    pipeline/src/MediaPipeKeypointExtractor.cpp
    pipeline/src/Pipeline.cpp
    pipeline/src/RgaProcessor.cpp
    pipeline/src/TcnRecognizer.cpp
)

target_include_directories(SignLanguagePipeline PRIVATE
    ${PROJECT_SOURCE_DIR}
    /usr/include/rga
    ${OpenCV_INCLUDE_DIRS}
)

if(RKNN_INCLUDE_DIR AND RKNNRT_LIBRARY)
    target_compile_definitions(SignLanguagePipeline PRIVATE HAVE_RKNN_RUNTIME)
    target_include_directories(SignLanguagePipeline PRIVATE ${RKNN_INCLUDE_DIR})
    target_link_libraries(SignLanguagePipeline PRIVATE ${RKNNRT_LIBRARY})
    message(STATUS "RKNN runtime enabled: ${RKNNRT_LIBRARY}")
else()
    message(WARNING "RKNN runtime not found; TCN recognizer will compile as disabled.")
endif()

target_link_libraries(SignLanguagePipeline PRIVATE
    Threads::Threads
    rga
    ${OpenCV_LIBS}
)

option(BUILD_QT_APP "Build the Qt frontend that drives the Python RKNN worker." ON)
if(BUILD_QT_APP)
    if(Qt5_FOUND)
        add_subdirectory(qt_app)
    else()
        message(STATUS "Qt5 Core/Widgets not found; SignLanguageQt frontend will be skipped.")
    endif()
endif()
