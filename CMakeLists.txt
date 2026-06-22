cmake_minimum_required(VERSION 3.10)
project(SignLanguagePipelineProject)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Threads REQUIRED)
find_package(OpenCV REQUIRED COMPONENTS core highgui imgcodecs imgproc videoio)
find_package(Qt5 COMPONENTS Core Widgets QUIET)
find_package(Qt5 COMPONENTS Qml Quick QUIET)
find_package(PkgConfig QUIET)

if(PkgConfig_FOUND AND NOT (Qt5Qml_FOUND AND Qt5Quick_FOUND))
    pkg_check_modules(QT5_QML_QUICK QUIET Qt5Qml Qt5Quick)
endif()

set(SIGN_MEDIAPIPE_WRAPPER "" CACHE FILEPATH "Path to libsign_mediapipe_extractor.so.")

find_path(RKNN_INCLUDE_DIR rknn_api.h
    PATHS
        /usr/include
        /usr/include/rknn
        /usr/local/include
        /usr/local/include/rknn
        /home/elf/AItools
        /home/elf/AItools/rknn
    PATH_SUFFIXES
        include
        include/rknn
)
find_library(RKNNRT_LIBRARY rknnrt
    PATHS
        /usr/lib
        /usr/lib/aarch64-linux-gnu
        /usr/local/lib
        /usr/local/lib/aarch64-linux-gnu
        /home/elf/AItools
    PATH_SUFFIXES
        lib
        lib64
        usr/lib
        usr/lib/aarch64-linux-gnu
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
    message(STATUS "RKNN_INCLUDE_DIR=${RKNN_INCLUDE_DIR}")
    message(STATUS "RKNNRT_LIBRARY=${RKNNRT_LIBRARY}")
    message(STATUS "If RKNN is installed in a custom path, pass -DRKNN_INCLUDE_DIR=... -DRKNNRT_LIBRARY=...")
endif()

if(SIGN_MEDIAPIPE_WRAPPER)
    target_compile_definitions(SignLanguagePipeline PRIVATE
        HAVE_MEDIAPIPE_WRAPPER
        SIGN_MEDIAPIPE_WRAPPER_PATH="${SIGN_MEDIAPIPE_WRAPPER}"
    )
    target_link_libraries(SignLanguagePipeline PRIVATE dl)
    message(STATUS "MediaPipe wrapper enabled: ${SIGN_MEDIAPIPE_WRAPPER}")
else()
    message(STATUS "MediaPipe wrapper disabled; keypoint extractor will use mock fallback.")
endif()

target_link_libraries(SignLanguagePipeline PRIVATE
    Threads::Threads
    rga
    ${OpenCV_LIBS}
)

option(BUILD_QT_APP "Build the Qt frontend that drives the Python RKNN worker." ON)
if(BUILD_QT_APP)
    if(Qt5Widgets_FOUND)
        add_subdirectory(qt_app)
    else()
        message(STATUS "Qt5 Core/Widgets not found; SignLanguageQt frontend will be skipped.")
    endif()
endif()

option(BUILD_QML_APP "Build the QML frontend that drives the Python RKNN worker." ON)
if(BUILD_QML_APP)
    if(Qt5Qml_FOUND AND Qt5Quick_FOUND)
        add_subdirectory(qt_qml_app)
    elseif(Qt5Core_FOUND AND QT5_QML_QUICK_FOUND)
        set(SIGN_QML_USE_PKGCONFIG ON)
        add_subdirectory(qt_qml_app)
    else()
        message(STATUS "Qt5 Qml/Quick not found; SignLanguageQml frontend will be skipped.")
        message(STATUS "Install qtdeclarative5-dev or provide pkg-config files for Qt5Qml/Qt5Quick.")
    endif()
endif()
