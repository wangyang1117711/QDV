# OpenCV MinGW Environment Setup Script
# Converts MSVC prebuilt OpenCV libs to MinGW compatible format

$ErrorActionPreference = "Stop"
$OpenCVRoot = "D:\opencv"
$OpenCVBinDir = "$OpenCVRoot\build\x64\vc16\bin"
$OpenCVLibDir = "$OpenCVRoot\build\x64\vc16\lib"
$OpenCVIncDir = "$OpenCVRoot\build\include"
$MingwLibDir = "$OpenCVRoot\build\x64\mingw\lib"
$MingwBinDir = "$OpenCVRoot\build\x64\mingw\bin"

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  OpenCV MinGW Environment Setup" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# Step 1: Create MinGW lib directories
Write-Host "[1/5] Creating MinGW lib directories..." -ForegroundColor Yellow
New-Item -ItemType Directory -Path $MingwLibDir -Force | Out-Null
New-Item -ItemType Directory -Path $MingwBinDir -Force | Out-Null
Write-Host "  -> Created $MingwLibDir" -ForegroundColor Green
Write-Host "  -> Created $MingwBinDir" -ForegroundColor Green
Write-Host ""

# Step 2: Generate MinGW .a import libs from DLLs
Write-Host "[2/5] Generating MinGW compatible .a import libs..." -ForegroundColor Yellow

$dllList = @(
    "opencv_world4130",
    "opencv_world4130d"
)

foreach ($dllName in $dllList) {
    $dllPath = "$OpenCVBinDir\$dllName.dll"
    $defPath = "$MingwLibDir\$dllName.def"
    $aPath = "$MingwLibDir\lib${dllName}.a"

    if (Test-Path $dllPath) {
        Write-Host "  Processing ${dllName}..." -ForegroundColor White

        # Step 1: Generate .def file from DLL using gendef
        & gendef "$dllPath" 2>&1
        if (Test-Path ".\${dllName}.def") {
            Move-Item ".\${dllName}.def" "$defPath" -Force
        }

        # Step 2: Convert .def to .a using dlltool
        if (Test-Path $defPath) {
            & dlltool -D "${dllName}.dll" -d "$defPath" -l "$aPath" --as-flags="--64" 2>&1
        }

        if (Test-Path $aPath) {
            $size = [math]::Round((Get-Item $aPath).Length / 1KB, 2)
            Write-Host "    -> Generated $aPath (${size}KB)" -ForegroundColor Green
        } else {
            Write-Host "    -> Warning: Failed to generate .a file" -ForegroundColor Red
        }
    } else {
        Write-Host "  Skipping ${dllName} (DLL not found)" -ForegroundColor Gray
    }
}
Write-Host ""

# Step 3: Copy DLLs to MinGW bin directory
Write-Host "[3/5] Copying DLLs to MinGW bin directory..." -ForegroundColor Yellow
Copy-Item "$OpenCVBinDir\*.dll" -Destination $MingwBinDir -Force
Write-Host "  -> DLLs copied to $MingwBinDir" -ForegroundColor Green
Write-Host ""

# Step 4: Add to PATH (user level)
Write-Host "[4/5] Configuring environment variables..." -ForegroundColor Yellow

$currentPath = [Environment]::GetEnvironmentVariable("Path", "User")
if ($currentPath -notlike "*$MingwBinDir*") {
    [Environment]::SetEnvironmentVariable("Path", "$currentPath;$MingwBinDir", "User")
    Write-Host "  -> Added $MingwBinDir to PATH" -ForegroundColor Green
} else {
    Write-Host "  -> PATH already contains OpenCV bin" -ForegroundColor Green
}

$existingOpenCVDir = [Environment]::GetEnvironmentVariable("OPENCV_DIR", "User")
if ($existingOpenCVDir -ne "$OpenCVRoot\build") {
    [Environment]::SetEnvironmentVariable("OPENCV_DIR", "$OpenCVRoot\build", "User")
    Write-Host "  -> Set OPENCV_DIR = $OpenCVRoot\build" -ForegroundColor Green
} else {
    Write-Host "  -> OPENCV_DIR already set" -ForegroundColor Green
}

# Also set for current session
$env:Path += ";$MingwBinDir"
$env:OPENCV_DIR = "$OpenCVRoot\build"
Write-Host ""

# Step 5: Create CMake config file
Write-Host "[5/5] Creating CMake OpenCV config..." -ForegroundColor Yellow

$cmakeDir = "E:\anchor\Trae\QDV\cmake"
if (-not (Test-Path $cmakeDir)) {
    New-Item -ItemType Directory -Path $cmakeDir -Force | Out-Null
}

$cmakeConfig = @"
# OpenCV CMake Configuration - MinGW Compatible
# Add this to your project CMakeLists.txt after find_package(Qt6...)

set(OpenCV_DIR "$OpenCVRoot/build")

find_package(OpenCV REQUIRED)

if(OpenCV_FOUND)
    message(STATUS "OpenCV Version: `${OpenCV_VERSION}")
    message(STATUS "OpenCV Include: `${OpenCV_INCLUDE_DIRS}")
    message(STATUS "OpenCV Libs: `${OpenCV_LIBS}")
endif()

add_definitions(-DHAS_OPENCV)
"@

$cmakeConfig | Out-File -FilePath "$cmakeDir\FindOpenCVConfig.cmake" -Encoding UTF8
Write-Host "  -> Created cmake/FindOpenCVConfig.cmake" -ForegroundColor Green
Write-Host ""

# Done
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  Setup Complete!" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "OpenCV Root: $OpenCVRoot" -ForegroundColor White
Write-Host "MinGW Libs: $MingwLibDir" -ForegroundColor White
Write-Host "MinGW DLLs: $MingwBinDir" -ForegroundColor White
Write-Host ""
