# ============================================================================
# 带 UI 集成示例 CMake 配置
# 项目名: zeroshot_with_ui
# 假设 ZeroShotKit 位于 ../../ 路径
# ============================================================================

cmake_minimum_required(VERSION 3.16)
project(zeroshot_with_ui LANGUAGES CXX)

# C++17 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 自动 MOC（MainWindow 使用 Q_OBJECT 宏，需要元对象编译）
set(CMAKE_AUTOMOC ON)

# 启用 ZeroShotKit UI 层（必须在 add_subdirectory 之前设置）
set(ZEROSHOTKIT_BUILD_UI ON CACHE BOOL "Build ZeroShotKit UI" FORCE)

# 查找依赖
find_package(Qt6 REQUIRED COMPONENTS Core Concurrent Widgets)
find_package(OpenCV REQUIRED)

# 引入 ZeroShotKit 库（假设位于上级目录 ../../）
add_subdirectory(../../ zeroshotkit_build)

# 可执行目标（WIN32 表示 Windows 下不弹出控制台窗口）
add_executable(zeroshot_with_ui WIN32 main.cpp)

# 链接 ZeroShotKit::UI（PUBLIC 传递 ZeroShotKit::Core / Qt6::Widgets / Qt6::Concurrent / OpenCV）
target_link_libraries(zeroshot_with_ui PRIVATE
    ZeroShotKit::UI
    Qt6::Core
    Qt6::Concurrent
    Qt6::Widgets
    ${OpenCV_LIBS}
)
