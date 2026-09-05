cmake_minimum_required(VERSION 3.16)
project(QDetectVision VERSION 2.0.809 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# [Task 10] 代码覆盖率开关 - 默认关闭
# 启用方式：cmake -DENABLE_COVERAGE=ON ...
# 启用后 MinGW gcov 会在构建目录生成 .gcno（编译期）与 .gcda（运行期）文件，
# 配合 scripts/coverage.ps1 可生成覆盖率报告。
# 注意：仅在 MinGW/gcc 下生效；MSVC 不支持 --coverage。
option(ENABLE_COVERAGE "Enable code coverage analysis (MinGW gcov)" OFF)
if(ENABLE_COVERAGE)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU")
        message(WARNING "ENABLE_COVERAGE is intended for GNU/MinGW (gcov). Current compiler (${CMAKE_CXX_COMPILER_ID}) may not support --coverage.")
    endif()
    # --coverage 等同于 -fprofile-arcs -ftest-coverage（编译）与 -lgcov（链接）
    add_compile_options(--coverage)
    add_link_options(--coverage)
    message(STATUS "Code coverage enabled (--coverage, gcov)")
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Gui Network Sql Charts Concurrent Test Quick QuickWidgets Qml)

# S4 修复（安全要求）：禁用 QML 调试器
# QML 调试器允许在运行时通过端口（默认 1234）连接到 QML 引擎执行任意 JS 代码，
# 生产环境必须关闭。QT_FEATURE_qml_debug 控制 Qt 自身构建特性（best-effort），
# QT_NO_DEBUG 进一步关闭调试输出，减少信息泄漏面。
# 注意：QT_FEATURE_qml_debug 仅对从源码构建 Qt 生效；预编译 Qt 通过移除
# qmldbg_*.dll 插件目录实现同等效果（见部署文档）。
set(QT_FEATURE_qml_debug OFF CACHE BOOL "" FORCE)
add_compile_definitions(QT_NO_DEBUG)

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

# ONNX Runtime 条件编译
# 默认开启（Task 4：接通 ONNX Runtime 后端）
# 默认关闭：ONNX Runtime GPU 版本依赖 CUDA DLLs，无 CUDA 环境会导致进程启动失败
# 启用方式：cmake -DHAS_ONNX_RUNTIME=ON ...
# 启用前提：安装 CUDA Toolkit 或使用 ONNX Runtime CPU 版本
option(HAS_ONNX_RUNTIME "Enable ONNX Runtime backend (requires CUDA or CPU ORT)" OFF)
if(HAS_ONNX_RUNTIME)
    message(STATUS "ONNX Runtime backend enabled (HAS_ONNX_RUNTIME=ON)")
    add_definitions(-DHAS_ONNX_RUNTIME)
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

# [P1-4b ScriptTool] 嵌入式 CPython 配置 - 默认 D:/Program Files/Python314
# 默认启用，可通过 -DQDV_WITH_PYTHON=OFF 关闭（关闭后 ScriptTool 退化为 stub）
option(QDV_WITH_PYTHON "Enable embedded CPython for ScriptTool" ON)
set(Python3_ROOT_DIR "D:/Program Files/Python314" CACHE PATH "CPython root directory")
if(QDV_WITH_PYTHON)
    set(_PY_INCLUDE "${Python3_ROOT_DIR}/include")
    set(_PY_LIB     "${Python3_ROOT_DIR}/libs/python314.lib")
    set(_PY_DLL     "${Python3_ROOT_DIR}/python314.dll")
    if(EXISTS "${_PY_INCLUDE}/Python.h" AND EXISTS "${_PY_LIB}")
        set(Python3_INCLUDE_DIRS "${_PY_INCLUDE}")
        set(Python3_LIBRARIES    "${_PY_LIB}")
        set(Python3_DLL          "${_PY_DLL}")
        message(STATUS "CPython found: ${Python3_ROOT_DIR}")
        add_definitions(-DQDV_WITH_PYTHON)
    else()
        message(WARNING "CPython not found at ${Python3_ROOT_DIR}, ScriptTool fallback to stub")
        set(QDV_WITH_PYTHON OFF CACHE BOOL "" FORCE)
    endif()
endif()

# [ZeroShotKit 依赖] ONNX Runtime 配置 - 优先 D:/onnxruntime（遵循非必要不放 C 盘）
# Task 4：接通 ONNX Runtime 后端
# 注意：MinGW 无法直接链接 MSVC 格式的 .lib 导入库，需直接链接 .dll
#       MSVC 则链接 .lib 导入库。此处根据编译器自动选择。
# 重要：仅当 HAS_ONNX_RUNTIME=ON 时才检测并链接 ONNX Runtime
#       GPU 版本依赖 CUDA DLLs，无 CUDA 环境会导致进程启动失败 (0xC0000135)
set(ONNXRUNTIME_ROOT_DIR "D:/onnxruntime-gpu" CACHE PATH "ONNX Runtime root")
if(HAS_ONNX_RUNTIME)
    if(EXISTS "${ONNXRUNTIME_ROOT_DIR}/include/onnxruntime_cxx_api.h"
       AND (EXISTS "${ONNXRUNTIME_ROOT_DIR}/lib/onnxruntime.lib"
            OR EXISTS "${ONNXRUNTIME_ROOT_DIR}/lib/onnxruntime.dll"))
        set(ONNXRUNTIME_INCLUDE_DIRS "${ONNXRUNTIME_ROOT_DIR}/include")
        # MinGW 直接链接 DLL；MSVC 链接 .lib 导入库
        if(MINGW)
            set(ONNXRUNTIME_LIBS "${ONNXRUNTIME_ROOT_DIR}/lib/onnxruntime.dll")
        else()
            set(ONNXRUNTIME_LIBS "${ONNXRUNTIME_ROOT_DIR}/lib/onnxruntime.lib")
        endif()
        message(STATUS "ONNX Runtime found: ${ONNXRUNTIME_ROOT_DIR} (lib: ${ONNXRUNTIME_LIBS})")
        set(ZEROSHOTKIT_ENABLE_ORT ON CACHE BOOL "" FORCE)
    else()
        message(WARNING "ONNX Runtime not found at ${ONNXRUNTIME_ROOT_DIR}, ZeroShotKit ORT disabled")
        set(ZEROSHOTKIT_ENABLE_ORT OFF CACHE BOOL "" FORCE)
    endif()
else()
    message(STATUS "ONNX Runtime disabled (HAS_ONNX_RUNTIME=OFF). Enable with -DHAS_ONNX_RUNTIME=ON")
    set(ZEROSHOTKIT_ENABLE_ORT OFF CACHE BOOL "" FORCE)
endif()

# [ZeroShotKit] 第三方零样本推理库（需在 UI 之前就绪，供 UI 静态库链接）
add_subdirectory(third_party/ZeroShotKit)

add_subdirectory(src/Core)
add_subdirectory(src/AI)
add_subdirectory(src/UI)
add_subdirectory(src/TrainingInference)
add_subdirectory(src/Vision)
add_subdirectory(src/Communication)
add_subdirectory(src/Database)
add_subdirectory(src/Plugins)
add_subdirectory(src/OperatorSDK)
# v2.7.0 O1a：自定义算子库模块（必须在 UI 之前就绪，供 UI 静态库链接 QDV::OperatorLibrary）
add_subdirectory(src/OperatorLibrary)
add_subdirectory(src/Core/AgentOperatorRouter)
add_subdirectory(src/Monitoring)
add_subdirectory(src/operators/extensions/Histogram)

# 导出功能：算子流程导出编排器（静态库，供 UI 调用）
add_subdirectory(src/Export)
# 导出功能：模板预编译（产出 wrapper DLL/EXE 供导出时复制）
add_subdirectory(templates)

add_subdirectory(apps/SmartVision)
add_subdirectory(tests)
if(OpenCV_FOUND)
    add_subdirectory(apps/OpenCVTest)
else()
    message(STATUS "OpenCVTest skipped: OpenCV MinGW libs not available")
endif()

# [QDV Annotator] 数据标注工具（独立子目录，复用 QDV.EditView QML 模块）
add_subdirectory(tools/qdv-annotator)