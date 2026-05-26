#!/bin/bash
# Q-DetectVision 构建脚本 - Linux/macOS

set -e

echo "========================================="
echo "Q-DetectVision 构建脚本"
echo "========================================="
echo

# 设置构建目录
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SOURCE_DIR="$SCRIPT_DIR/.."
BUILD_DIR="$SOURCE_DIR/build"

# 创建构建目录
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# CMake配置选项
CMAKE_OPTIONS=(
    -DCMAKE_BUILD_TYPE=Release
    -DBUILD_TESTS=ON
    -DBUILD_PLUGINS=ON
    -DENABLE_GPU=OFF
)

# 配置CMake
echo
echo "配置CMake..."
cmake "${CMAKE_OPTIONS[@]}" "$SOURCE_DIR"

# 构建项目
echo
echo "构建项目..."
cmake --build . --config Release --parallel

# 运行测试
echo
echo "运行测试..."
ctest -C Release --output-on-failure

echo
echo "========================================="
echo "构建成功完成！"
echo "========================================="
echo "可执行文件位置: $BUILD_DIR/bin/Q-DetectVision"
echo "========================================="
