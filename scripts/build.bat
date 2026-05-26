@echo off
REM Q-DetectVision 构建脚本

setlocal enabledelayedexpansion

echo =========================================
echo Q-DetectVision 构建脚本
echo =========================================
echo.

REM 设置构建目录
set BUILD_DIR=%~dp0..\build
set SOURCE_DIR=%~dp0..

if not exist "%BUILD_DIR%" (
    echo 创建构建目录: %BUILD_DIR%
    mkdir "%BUILD_DIR%"
)

cd /d "%BUILD_DIR%"

REM 检查CMake配置选项
set CMAKE_OPTIONS=
set CMAKE_OPTIONS=%CMAKE_OPTIONS% -DCMAKE_BUILD_TYPE=Release
set CMAKE_OPTIONS=%CMAKE_OPTIONS% -DBUILD_TESTS=ON
set CMAKE_OPTIONS=%CMAKE_OPTIONS% -DBUILD_PLUGINS=ON
set CMAKE_OPTIONS=%CMAKE_OPTIONS% -DENABLE_GPU=OFF
set CMAKE_OPTIONS=%CMAKE_OPTIONS% -G "Visual Studio 17 2022"
set CMAKE_OPTIONS=%CMAKE_OPTIONS% -A x64

REM 配置CMake
echo.
echo 配置CMake...
cmake "%SOURCE_DIR%" %CMAKE_OPTIONS%
if %errorlevel% neq 0 (
    echo.
    echo CMake配置失败！
    exit /b 1
)

REM 构建项目
echo.
echo 构建项目...
cmake --build . --config Release --parallel
if %errorlevel% neq 0 (
    echo.
    echo 构建失败！
    exit /b 1
)

REM 运行测试
echo.
echo 运行测试...
ctest -C Release --output-on-failure
if %errorlevel% neq 0 (
    echo.
    echo 测试失败！
    exit /b 1
)

echo.
echo =========================================
echo 构建成功完成！
echo =========================================
echo 可执行文件位置: %BUILD_DIR%\bin\Release\Q-DetectVision.exe
echo =========================================
