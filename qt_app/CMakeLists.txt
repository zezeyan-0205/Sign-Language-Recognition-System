set(CMAKE_AUTOMOC ON)

add_executable(SignLanguageQt
    main.cpp
    DoctorWindow.cpp
    DoctorWindow.hpp
    MainWindow.cpp
    MainWindow.hpp
)

target_compile_definitions(SignLanguageQt PRIVATE
    PROJECT_ROOT="${PROJECT_SOURCE_DIR}"
)

target_link_libraries(SignLanguageQt PRIVATE
    Qt5::Core
    Qt5::Widgets
    Threads::Threads
    opencv_videoio
    ${OpenCV_LIBS}
)
