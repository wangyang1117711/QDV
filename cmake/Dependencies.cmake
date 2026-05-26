# 项目依赖配置

# Qt6
find_package(Qt6 6.5 REQUIRED COMPONENTS
    Core
    Gui
    Widgets
    Quick
    Qml
    Network
    Sql
)

# OpenCV
find_package(OpenCV 4.8 REQUIRED COMPONENTS
    core
    imgproc
    imgcodecs
    features2d
)

# 日志库
find_package(spdlog 1.10 REQUIRED)

# JSON库
find_package(nlohmann_json 3.10 REQUIRED)

# GPU支持
if(ENABLE_GPU)
    find_package(CUDA 11.0 REQUIRED)
    enable_language(CUDA)
endif()

# 单元测试框架
if(BUILD_TESTS)
    find_package(Catch2 3.0 REQUIRED)
endif()

# 为每个目标设置包含目录
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${OpenCV_INCLUDE_DIRS}
)
