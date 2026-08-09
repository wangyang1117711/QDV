<#
Q-DetectVision 项目构建脚本
Qt版本: 6.11.1
#>

Write-Host "`n========================================" -ForegroundColor Cyan
Write-Host "Q-DetectVision 项目构建" -ForegroundColor Cyan
Write-Host "========================================`n" -ForegroundColor Cyan

$QtRoot = "D:\Qt_new\6.11.1\mingw_64"
$MinGW = "D:\Qt_new\Tools\mingw1310_64"

$env:QTDIR = $QtRoot
$env:CMAKE_PREFIX_PATH = $QtRoot
$env:PATH = "$QtRoot\bin;$MinGW\bin;D:\Qt_new\Tools\CMake_64\bin;D:\Qt_new\Tools\Ninja;$env:PATH"

Write-Host "Qt 路径: $QtRoot"
Write-Host "MinGW: $MinGW`n"

$ProjectRoot = "E:\anchor\Trae\QDV"
Set-Location $ProjectRoot
Write-Host "项目目录: $ProjectRoot"

Write-Host "`n--- 清理旧构建 ---"
if (Test-Path "build") {
    Remove-Item -Path "build" -Recurse -Force
    Write-Host "已删除 build 目录"
}

New-Item -ItemType Directory -Path "build" -Force | Out-Null
Set-Location "build"

Write-Host "`n--- 配置 CMake ---"
cmake .. -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="D:/Qt_new/6.11.1/mingw_64"

if ($LASTEXITCODE -ne 0) {
    Write-Host "`n[错误] CMake 配置失败！" -ForegroundColor Red
    exit 1
}

Write-Host "`n--- 编译项目 ---"
cmake --build . -j 8

if ($LASTEXITCODE -ne 0) {
    Write-Host "`n[错误] 编译失败！" -ForegroundColor Red
    exit 1
}

Write-Host "`n========================================" -ForegroundColor Green
Write-Host "构建成功！" -ForegroundColor Green
Write-Host "========================================`n" -ForegroundColor Green

$exePath = Join-Path $PWD "bin\Q-DetectVision.exe"
if (Test-Path $exePath) {
    Write-Host "可执行文件位置:"
    Write-Host "  $exePath"
}

Write-Host "运行测试:"
Write-Host "  ctest"