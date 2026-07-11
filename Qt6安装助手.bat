@echo off
REM =================================================================
REM Q-DetectVision Qt6 安装助手
REM =================================================================

echo.
echo ========================================
echo Q-DetectVision Qt6 D盘 安装
echo ========================================
echo.

echo 已准备的文件:
echo.
echo [1] D:\Qt\Qt6_D盘安装指南.md
echo [2] D:\Qt\Examples\HelloQt\
echo [3] E:\anchor\Trae\QDV\build_D盘.ps1
echo.

choice /C 123 /M "请选择: "
if errorlevel 3 goto build
if errorlevel 2 goto hello
if errorlevel 1 goto guide

:guide
echo.
start "" D:\Qt\Qt6_D盘安装指南.md
goto end

:hello
echo.
start "" D:\Qt\Examples\HelloQt\
goto end

:build
echo.
echo 请确保已安装 Qt 后再运行！
powershell -ExecutionPolicy Bypass -File "E:\anchor\Trae\QDV\build_D盘.ps1"
goto end

:end
echo.
pause