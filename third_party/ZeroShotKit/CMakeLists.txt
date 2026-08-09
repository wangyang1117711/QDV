cmake_minimum_required(VERSION 3.16)
project(ZeroShotKit LANGUAGES CXX)

# ============================================================================
# ZeroShotKit - 零样本功能模块化库
# 提供 Core（纯推理）与 UI（Qt Widgets）两层静态库
# ============================================================================

# 选项
option(ZEROSHOTKIT_BUILD_UI "Build ZeroShotKit UI components" ON)
option(ZEROSHOTKIT_BUILD_TESTS "Build ZeroShotKit tests" OFF)
option(ZEROSHOTKIT_ENABLE_ORT "Enable ONNX Runtime backend" ON)

# C++17 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 自动 MOC/UIC/RCC
set(CMAKE_AUTOMOC ON)

# 查找依赖
find_package(Qt6 REQUIRED COMPONENTS Core Concurrent)
find_package(OpenCV REQUIRED)

# ONNX Runtime
# 主项目已在根 CMakeLists.txt 中配置了 ONNXRUNTIME_INCLUDE_DIRS / ONNXRUNTIME_LIBS
# 这里复用主项目的变量；若独立构建则尝试自行查找
if(ZEROSHOTKIT_ENABLE_ORT)
    if(NOT DEFINED ONNXRUNTIME_INCLUDE_DIRS AND NOT DEFINED ONNXRUNTIME_LIBS)
        # 独立查找模式：假设 ONNX Runtime 安装在默认路径
        find_path(ONNXRUNTIME_INCLUDE_DIRS onnxruntime_cxx_api.h
            PATHS
                "$ENV{ONNXRUNTIME_ROOT}/include"
                "C:/onnxruntime/include"
                "D:/onnxruntime/include"
            PATH_SUFFIXES onnxruntime
            DOC "ONNX Runtime include directory"
        )
        find_library(ONNXRUNTIME_LIBS onnxruntime
            PATHS
                "$ENV{ONNXRUNTIME_ROOT}/lib"
                "C:/onnxruntime/lib"
                "D:/onnxruntime/lib"
            DOC "ONNX Runtime library"
        )
    endif()
    if(ONNXRUNTIME_INCLUDE_DIRS AND ONNXRUNTIME_LIBS)
        set(ZSU_HAS_ORT TRUE)
        message(STATUS "ZeroShotKit: ONNX Runtime found at ${ONNXRUNTIME_LIBS}")
    else()
        message(WARNING "ZeroShotKit: ONNX Runtime not found, building without ORT support")
        set(ZSU_HAS_ORT FALSE)
    endif()
else()
    set(ZSU_HAS_ORT FALSE)
endif()

# Core 层
add_subdirectory(src)

# UI 层（条件编译）
if(ZEROSHOTKIT_BUILD_UI)
    find_package(Qt6 REQUIRED COMPONENTS Widgets)
    add_subdirectory(ui)
endif()

# 示例（条件编译）
if(ZEROSHOTKIT_BUILD_TESTS)
    # 预留：测试目录
endif()
