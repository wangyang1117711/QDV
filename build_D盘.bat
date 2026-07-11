@echo off
REM =================================================================
REM Q-DetectVision Qt6 D盘 构建脚本
REM =================================================================

echo.
echo ========================================
echo Q-DetectVision 项目构建
echo ========================================
echo.

REM 设置 Qt 环境
set QTDIR=D:\Qt\6.5.0\mingw_64
set PATH=%QTDIR%\bin;D:\Qt\Tools\mingw1120_64\bin;%PATH%
set CMAKE_PREFIX_PATH=%QTDIR%

REM 检查 Qt 是否存在
if not exist "%QTDIR%\bin\qmake.exe" (
    echo.
    echo [错误] Qt6 未找到！
    echo.
    echo 请确保已将 Qt 安装到 D:\Qt\6.5.0\mingw_64
    echo 或运行: qmake --version
    echo.
    echo 安装指南: D:\Qt\Qt6_D盘安装指南.md
    echo.
    pause
    exit /b 1
)

echo [OK] Qt6 已找到
qmake --version

REM 检查项目目录
if not exist "CMakeLists.txt" (
    echo [错误] 项目文件未找到
    exit /b 1
)

echo.
echo ========================================
echo 清理旧构建
echo ========================================
if exist "build" (
    echo 移除 build 目录...
    rmdir /s /q "build"
)

mkdir build
cd build

echo.
echo ========================================
echo 配置 CMake
echo ========================================
cmake .. -G "MinGW Makefiles" ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_PREFIX_PATH="D:/Qt/6.5.0/mingw_64"

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo [错误] CMake 配置失败！
    cd ..
    pause
    exit /b 1
)

echo.
echo ========================================
echo 编译项目
echo ========================================
cmake --build . -j 8

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo [错误] 编译失败！
    cd ..
    pause
    exit /b 1
)

echo.
echo ========================================
echo 构建成功！
echo ========================================
echo.
echo 可执行文件位置:
echo   %CD%\bin\Q-DetectVision.exe
echo.
echo 运行测试:
echo   cd %CD%
echo   ctest
echo.
cd ..
pause