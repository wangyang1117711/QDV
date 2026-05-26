add_executable(OpenCVTest main.cpp)

target_link_libraries(OpenCVTest PRIVATE
    Qt6::Core Qt6::Widgets
    ${OpenCV_LIBS}
)

target_include_directories(OpenCVTest PRIVATE
    ${OpenCV_INCLUDE_DIRS}
)
