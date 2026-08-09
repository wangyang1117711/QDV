<#
.SYNOPSIS
Q-DetectVision Qt6 D盘 构建脚本

.DESCRIPTION
一键配置 Qt6 环境并编译项目
#>

# 设置环境
Write-Host "`n========================================" -ForegroundColor Cyan
Write-Host "Q-DetectVision 项目构建" -ForegroundColor Cyan
Write-Host "========================================`n" -ForegroundColor Cyan

# 检查 D 盘 Qt
$QtRoot = "D:\Qt_new\6.11.1\mingw_64"
$MinGW = "D:\Qt_new\Tools\mingw1310_64"
if (-not (Test-Path $QtRoot)) {
    Write-Host "[错误] Qt6 未找到！" -ForegroundColor Red
    Write-Host "请确保已将 Qt 安装到: $QtRoot"
    Write-Host "`n安装指南: D:\Qt\Qt6_D盘安装指南.md" -ForegroundColor Yellow
    exit 1
}

Write-Host "[OK] Qt6 已找到" -ForegroundColor Green
Write-Host "Qt 路径: $QtRoot"
Write-Host "MinGW: $MinGW"

# 设置环境变量
$env:QTDIR = $QtRoot
$env:PATH = "$QtRoot\bin;$MinGW\bin;D:\Qt_new\Tools\CMake_64\bin;$env:PATH"
$env:CMAKE_PREFIX_PATH = $QtRoot

# 验证 qmake
try {
    $qmake = & qmake --version 2>&1
    if ($LASTEXITCODE -eq 0) {
        Write-Host "qmake 版本: $qmake"
    }
} catch {
    Write-Host "[警告] qmake 测试失败" -ForegroundColor Yellow
}

# 进入项目目录
$ProjectRoot = "E:\anchor\Trae\QDV"
if (-not (Test-Path $ProjectRoot)) {
    Write-Host "[错误] 项目目录不存在: $ProjectRoot" -ForegroundColor Red
    exit 1
}

Set-Location $ProjectRoot
Write-Host "项目目录: $ProjectRoot"

# 检查 CMakeLists.txt
if (-not (Test-Path "CMakeLists.txt")) {
    Write-Host "[错误] 项目文件未找到" -ForegroundColor Red
    exit 1
}

# 清理旧构建
Write-Host "`n--- 清理旧构建 ---"
if (Test-Path "build") {
    Remove-Item -Path "build" -Recurse -Force
    Write-Host "已删除 build 目录"
}

# 创建新构建目录
New-Item -ItemType Directory -Path "build" -Force | Out-Null
Set-Location "build"

# 配置 CMake
Write-Host "`n--- 配置 CMake ---"
$cmakeArgs = @(
    ".."
    "-G", "MinGW Makefiles"
    "-DCMAKE_BUILD_TYPE=Release"
    "-DCMAKE_PREFIX_PATH=D:/Qt_new/6.11.1/mingw_64"
)

$cmakeResult = & cmake @cmakeArgs

if ($LASTEXITCODE -ne 0) {
    Write-Host "`n[错误] CMake 配置失败！" -ForegroundColor Red
    Write-Host $cmakeResult
    exit 1
}

# 编译项目
Write-Host "`n--- 编译项目 ---"
$buildResult = & cmake --build . -j 8

if ($LASTEXITCODE -ne 0) {
    Write-Host "`n[错误] 编译失败！" -ForegroundColor Red
    Write-Host $buildResult
    exit 1
}

Write-Host "`n========================================" -ForegroundColor Green
Write-Host "构建成功！" -ForegroundColor Green
Write-Host "========================================`n" -ForegroundColor Green

$exePath = Join-Path $PWD "bin\Q-DetectVision.exe"
if (Test-Path $exePath) {
    Write-Host "可执行文件位置:"
    Write-Host "  $exePath`n"
}

Write-Host "运行测试:"
Write-Host "  ctest`n"
