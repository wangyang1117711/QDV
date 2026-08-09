# ============================================================================
# 批量推理示例 CMake 配置
# 项目名: zeroshot_batch
# 假设 ZeroShotKit 位于 ../../ 路径
# ============================================================================

cmake_minimum_required(VERSION 3.16)
project(zeroshot_batch LANGUAGES CXX)

# C++17 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 自动 MOC（Kit 使用 QFutureWatcher 等元对象机制）
set(CMAKE_AUTOMOC ON)

# 查找依赖
find_package(Qt6 REQUIRED COMPONENTS Core Concurrent)
find_package(OpenCV REQUIRED)

# 引入 ZeroShotKit 库（假设位于上级目录 ../../）
add_subdirectory(../../ zeroshotkit_build)

# 可执行目标
add_executable(zeroshot_batch main.cpp)

# 链接 ZeroShotKit::Core + Qt6::Concurrent（异步推理依赖 QtConcurrent）
target_link_libraries(zeroshot_batch PRIVATE
    ZeroShotKit::Core
    Qt6::Core
    Qt6::Concurrent
    ${OpenCV_LIBS}
)
