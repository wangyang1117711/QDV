@echo off
REM ======================================================================
REM Q-DetectVision 构建脚本（Visual Studio 版本）
REM ----------------------------------------------------------------------
REM 注意：本脚本使用 "Visual Studio 17 2022" 生成器，与项目主推的
REM       MinGW 工具链（Qt 6.11.1 + MinGW 13.1.0）不匹配。
REM       仅作为 VS 构建的备用参考脚本保留，非主推构建方式。
REM
REM 主推构建脚本（MinGW 工具链）：
REM   - build.ps1            （PowerShell，推荐）
REM   - build_D盘.bat        （批处理）
REM   - build_D盘.ps1        （PowerShell，D 盘 Qt）
REM
REM 如需使用本脚本，请确保已安装 Visual Studio 2022 及对应 Qt MSVC 版本。
REM ======================================================================

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
