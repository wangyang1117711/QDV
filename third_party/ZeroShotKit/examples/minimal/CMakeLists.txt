# ============================================================================
# 最简推理示例 CMake 配置
# 项目名: zeroshot_minimal
# 假设 ZeroShotKit 位于 ../../ 路径
# ============================================================================

cmake_minimum_required(VERSION 3.16)
project(zeroshot_minimal LANGUAGES CXX)

# C++17 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 自动 MOC（Kit 是 QObject 子类，需要元对象编译）
set(CMAKE_AUTOMOC ON)

# 查找依赖
find_package(Qt6 REQUIRED COMPONENTS Core)
find_package(OpenCV REQUIRED)

# 引入 ZeroShotKit 库（假设位于上级目录 ../../）
add_subdirectory(../../ zeroshotkit_build)

# 可执行目标
add_executable(zeroshot_minimal main.cpp)

# 链接 ZeroShotKit::Core（PUBLIC 传递 Qt6::Core / OpenCV / ONNX Runtime）
target_link_libraries(zeroshot_minimal PRIVATE
    ZeroShotKit::Core
    Qt6::Core
    ${OpenCV_LIBS}
)
