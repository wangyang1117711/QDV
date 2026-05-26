cmake_minimum_required(VERSION 3.16)
project(QDetectVision VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Gui Network Sql Charts)

# [OpenCV集成] OpenCV 配置 - MinGW 源码编译版本
# OpenCV MinGW 编译产物路径: D:\opencv\build_mingw
set(OPENCV_ROOT "D:/opencv" CACHE PATH "OpenCV root install directory")

set(OpenCV_DIR "${OPENCV_ROOT}/build_mingw")
find_package(OpenCV QUIET)
if(OpenCV_FOUND)
    message(STATUS "OpenCV version: ${OpenCV_VERSION}")
    message(STATUS "OpenCV include: ${OpenCV_INCLUDE_DIRS}")
    message(STATUS "OpenCV libs: ${OpenCV_LIBS}")
    message(STATUS "OpenCV lib dir: ${OpenCV_LIB_DIR}")
    set(OpenCV_BIN_DIR "D:/opencv/build_mingw/bin")
    add_definitions(-DHAS_OPENCV)
else()
    message(STATUS "OpenCV not found via CMake config. Using manual setup for MinGW.")
    message(STATUS "To rebuild OpenCV, run:")
    message(STATUS "  cmake -DOpenCV_DIR=D:/opencv/build_mingw ...")
    set(OpenCV_INCLUDE_DIRS "D:/opencv/mingw_install/include")
    set(OpenCV_LIB_DIR "D:/opencv/mingw_install/x64/mingw/lib")
    set(OpenCV_BIN_DIR "D:/opencv/mingw_install/x64/mingw/bin")
    set(OpenCV_LIBS "opencv_world4130")
    message(STATUS "Using manual OpenCV config: ${OpenCV_INCLUDE_DIRS}")
    add_definitions(-DHAS_OPENCV)
endif()

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${OpenCV_INCLUDE_DIRS}
)

link_directories(
    ${OpenCV_LIB_DIR}
)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

add_subdirectory(src/Core)
add_subdirectory(src/AI)
add_subdirectory(src/UI)
add_subdirectory(src/TrainingInference)
add_subdirectory(src/Vision)
add_subdirectory(src/Communication)
add_subdirectory(src/Database)
add_subdirectory(src/Plugins)
add_subdirectory(apps/SmartVision)
if(OpenCV_FOUND)
    add_subdirectory(apps/OpenCVTest)
else()
    message(STATUS "OpenCVTest skipped: OpenCV MinGW libs not available")
endif()