# OpenCV MinGW 编译脚本 - 从源码编译 MinGW 兼容版本
# 使用方法: powershell -ExecutionPolicy Bypass -File scripts\build_opencv_mingw.ps1

$ErrorActionPreference = "Stop"
$OpenCVVersion = "4.13.0"
$SourceDir = "D:\opencv\src"
$BuildDir = "D:\opencv\build_mingw"
$InstallDir = "D:\opencv\mingw_install"
$MinGWPath = "D:\Qt\6.11\Tools\mingw1120_64"

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  OpenCV MinGW Build Script" -ForegroundColor Cyan
Write-Host "  Version: ${OpenCVVersion}" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# Check MinGW
if (-not (Test-Path "${MinGWPath}\bin\gcc.exe")) {
    Write-Host "ERROR: MinGW GCC not found at ${MinGWPath}\bin\gcc.exe" -ForegroundColor Red
    exit 1
}

$gccVersion = & "${MinGWPath}\bin\gcc.exe" --version | Select-Object -First 1
Write-Host "GCC: ${gccVersion}" -ForegroundColor Green

# Check CMake
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    Write-Host "ERROR: CMake not found in PATH" -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "[1/4] Downloading OpenCV source..." -ForegroundColor Yellow

$srcZipUrl = "https://github.com/opencv/opencv/archive/refs/tags/${OpenCVVersion}.zip"
$srcZipPath = "D:\Soft\opencv-${OpenCVVersion}-sources.zip"

if (-not (Test-Path $srcZipPath)) {
    Write-Host "  Downloading from GitHub..." -ForegroundColor White
    # Download via curl or Invoke-WebRequest
    try {
        Invoke-WebRequest -Uri $srcZipUrl -OutFile $srcZipPath -UseBasicParsing -TimeoutSec 600
    } catch {
        Write-Host "  Download failed: $_" -ForegroundColor Red
        Write-Host "  Please download manually from: ${srcZipUrl}" -ForegroundColor Yellow
        exit 1
    }
}

if (Test-Path $srcZipPath) {
    $size = [math]::Round((Get-Item $srcZipPath).Length / 1MB, 2)
    Write-Host "  Source archive: ${size}MB" -ForegroundColor Green
} else {
    Write-Host "  Source archive not found. Please download from GitHub." -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "[2/4] Extracting OpenCV source..." -ForegroundColor Yellow

if (-not (Test-Path "${SourceDir}\CMakeLists.txt")) {
    Expand-Archive -Path $srcZipPath -DestinationPath "D:\opencv" -Force
    # Rename extracted folder
    $extractedDir = Get-ChildItem "D:\opencv" -Filter "opencv-${OpenCVVersion}" -Directory
    if ($extractedDir) {
        Rename-Item -Path $extractedDir.FullName -NewName "src" -Force
        Write-Host "  Extracted to ${SourceDir}" -ForegroundColor Green
    } else {
        Write-Host "  ERROR: Extraction failed" -ForegroundColor Red
        exit 1
    }
} else {
    Write-Host "  Source already extracted at ${SourceDir}" -ForegroundColor Green
}

Write-Host ""
Write-Host "[3/4] Configuring CMake build..." -ForegroundColor Yellow

New-Item -ItemType Directory -Path $BuildDir -Force | Out-Null

# Set PATH to include MinGW
$env:PATH = "${MinGWPath}\bin;" + $env:PATH

# Run CMake configuration
cmake -S $SourceDir -B $BuildDir `
    -G "MinGW Makefiles" `
    -DCMAKE_BUILD_TYPE=Release `
    -DCMAKE_C_COMPILER="${MinGWPath}\bin\gcc.exe" `
    -DCMAKE_CXX_COMPILER="${MinGWPath}\bin\g++.exe" `
    -DCMAKE_INSTALL_PREFIX=$InstallDir `
    -DBUILD_SHARED_LIBS=ON `
    -DBUILD_opencv_world=ON `
    -DOPENCV_ENABLE_NONFREE=ON `
    -DWITH_QT=OFF `
    -DWITH_GTK=OFF `
    -DWITH_OPENGL=OFF `
    -DBUILD_TESTS=OFF `
    -DBUILD_PERF_TESTS=OFF `
    -DBUILD_EXAMPLES=OFF `
    -DBUILD_DOCS=OFF `
    -DBUILD_JAVA=OFF `
    -DBUILD_opencv_python2=OFF `
    -DBUILD_opencv_python3=OFF `
    2>&1

if ($LASTEXITCODE -ne 0) {
    Write-Host "  CMake configuration failed!" -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "[4/4] Building and installing..." -ForegroundColor Yellow

cmake --build $BuildDir -j 4 2>&1
if ($LASTEXITCODE -ne 0) {
    Write-Host "  Build failed!" -ForegroundColor Red
    exit 1
}

cmake --build $BuildDir --target install 2>&1
if ($LASTEXITCODE -ne 0) {
    Write-Host "  Install failed!" -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  OpenCV MinGW Build Complete!" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "Install directory: ${InstallDir}" -ForegroundColor White
Write-Host "To use in your project:" -ForegroundColor Yellow
Write-Host "  set(OpenCV_DIR `"$InstallDir`")" -ForegroundColor White
Write-Host "  find_package(OpenCV REQUIRED)" -ForegroundColor White
Write-Host "  target_link_libraries(YourTarget ${OpenCV_LIBS})" -ForegroundColor White
