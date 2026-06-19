set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

add_executable(SignLanguageQml
    main.cpp
    PreviewImageProvider.cpp
    PreviewImageProvider.hpp
    SignController.cpp
    SignController.hpp
    resources.qrc
)

target_compile_definitions(SignLanguageQml PRIVATE
    PROJECT_ROOT="${PROJECT_SOURCE_DIR}"
)

target_link_libraries(SignLanguageQml PRIVATE
    Qt5::Core
    Threads::Threads
    opencv_videoio
    ${OpenCV_LIBS}
)

if(SIGN_QML_USE_PKGCONFIG)
    target_include_directories(SignLanguageQml PRIVATE ${QT5_QML_QUICK_INCLUDE_DIRS})
    target_compile_options(SignLanguageQml PRIVATE ${QT5_QML_QUICK_CFLAGS_OTHER})
    target_link_libraries(SignLanguageQml PRIVATE ${QT5_QML_QUICK_LDFLAGS})
else()
    target_link_libraries(SignLanguageQml PRIVATE
        Qt5::Qml
        Qt5::Quick
    )
endif()
