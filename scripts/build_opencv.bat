@echo off
set PATH=D:\Qt\6.11\Tools\mingw1120_64\bin;D:\Qt\6.11\6.11.1\mingw_64\bin;%PATH%
echo =========================================
echo OpenCV 4.13.0 MinGW Build
echo =========================================
echo.
echo [1/3] CMake Configuration...
rmdir /s /q D:\opencv\build_mingw 2>nul
mkdir D:\opencv\build_mingw

cmake -S D:\opencv\sources -B D:\opencv\build_mingw ^
    -G "MinGW Makefiles" ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_C_COMPILER=D:/Qt/6.11/Tools/mingw1120_64/bin/gcc.exe ^
    -DCMAKE_CXX_COMPILER=D:/Qt/6.11/Tools/mingw1120_64/bin/g++.exe ^
    -DCMAKE_INSTALL_PREFIX=D:/opencv/mingw_install ^
    -DBUILD_SHARED_LIBS=ON ^
    -DBUILD_opencv_world=ON ^
    -DWITH_FFMPEG=OFF ^
    -DWITH_IPP=OFF ^
    -DWITH_MSMF=ON ^
    -DBUILD_TESTS=OFF ^
    -DBUILD_PERF_TESTS=OFF ^
    -DBUILD_EXAMPLES=OFF ^
    -DBUILD_opencv_python3=OFF ^
    -DBUILD_JAVA=OFF ^
    -DBUILD_opencv_apps=OFF

if %ERRORLEVEL% NEQ 0 (
    echo ERROR: CMake configuration failed!
    exit /b 1
)
echo CMake configuration completed.

echo.
echo [2/3] Building OpenCV...
cd /d D:\opencv\build_mingw
mingw32-make -j4
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: Build failed!
    exit /b 1
)
echo Build completed.

echo.
echo [3/3] Installing...
mingw32-make install
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: Install failed!
    exit /b 1
)
echo.
echo =========================================
echo OpenCV MinGW Build Complete!
echo Install: D:\opencv\mingw_install
echo =========================================