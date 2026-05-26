# OpenCV 开发环境配置文档

## 环境信息

| 项目 | 值 |
|------|------|
| 操作系统 | Windows 11 |
| 编译器 | MinGW-w64 (GCC 11.2.0) |
| OpenCV 版本 | 4.13.0 |
| Python OpenCV | 4.13.0 (pip/conda) |
| C++ 库状态 | 配置完成，待源码编译 |
| Python 库状态 | ✅ 可用 |

## 目录结构

```
D:\opencv\
├── build\                    # MSVC 预编译版本（当前使用）
│   ├── OpenCVConfig.cmake    # CMake 配置文件
│   ├── x64\
│   │   ├── vc16\bin\         # MSVC DLL
│   │   └── vc16\lib\         # MSVC .lib
│   └── include\              # C++ 头文件
└── build_mingw\              # MinGW 编译目录（待生成）
    └── install\              # MinGW 安装目录（待生成）
        ├── bin\              # .dll
        ├── lib\              # .a 静态库
        └── include\          # 头文件
```

## 已完成配置

### 1. Python OpenCV 环境

```bash
# 已安装（国内镜像）
pip install opencv-python opencv-contrib-python numpy \
    -i https://pypi.tuna.tsinghua.edu.cn/simple

# 验证
python tests/test_opencv.py
```

### 2. CMake 项目集成

项目 `CMakeLists.txt` 已添加 OpenCV 配置：
```cmake
set(OpenCV_DIR "D:/opencv/build" CACHE PATH "OpenCV CMake config directory")
find_package(OpenCV QUIET)
if(OpenCV_FOUND)
    # 使用 OpenCV
else()
    # Fallback: 手动配置头文件和库路径
endif()
```

### 3. 测试程序

- **Python**: `tests/test_opencv.py` ✅ 全部通过
- **C++**: `apps/OpenCVTest/main.cpp` （待编译）

## 待完成：C++ MinGW 编译

### 方案 A：CMake 源码编译（推荐）

```bash
# 下载源码
git clone https://github.com/opencv/opencv.git
cd opencv && git checkout 4.13.0

# 下载 contrib 模块
git clone https://github.com/opencv/opencv_contrib.git
cd opencv_contrib && git checkout 4.13.0

# 创建编译目录
mkdir build_mingw && cd build_mingw

# CMake 配置
cmake .. \
    -G "MinGW Makefiles" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER="D:\Qt\6.11\Tools\mingw1120_64\bin\gcc.exe" \
    -DCMAKE_CXX_COMPILER="D:\Qt\6.11\Tools\mingw1120_64\bin\g++.exe" \
    -DCMAKE_INSTALL_PREFIX="D:\opencv\mingw_install" \
    -DBUILD_opencv_world=ON \
    -DOPENCV_ENABLE_NONFREE=ON \
    -DWITH_QT=OFF \
    -DBUILD_TESTS=OFF \
    -DBUILD_PERF_TESTS=OFF \
    -DBUILD_EXAMPLES=OFF

# 编译（约 2-4 小时，取决于 CPU）
mingw32-make -j4

# 安装
mingw32-make install
```

### 方案 B：使用 vcpkg

```bash
cd D:\vcpkg
.\vcpkg.exe install opencv4[contrib,nonfree]:x64-mingw-dynamic
```

### 方案 C：预编译 MinGW 库

从第三方获取预编译的 MinGW 版本 OpenCV（如 msys2 仓库）。

## 验证步骤

### 1. Python 验证（已完成）

```bash
python tests/test_opencv.py
```

### 2. C++ 验证（待编译后）

```bash
cd build_new
cmake ..
mingw32-make OpenCVTest
.\bin\OpenCVTest.exe
```

## 环境变量配置

```powershell
# 添加到系统 PATH
[Environment]::SetEnvironmentVariable(
    "Path", 
    "$([Environment]::GetEnvironmentVariable('Path', 'User'));D:\opencv\build\x64\vc16\bin", 
    "User"
)

# OpenCV 根目录
[Environment]::SetEnvironmentVariable(
    "OPENCV_DIR", 
    "D:\opencv\build", 
    "User"
)
```

## 常见问题

### Q: 为什么官方预编译包不能直接用于 MinGW？

A: OpenCV 官方 Windows 预编译包使用 MSVC 编译器，而项目使用 MinGW-w64。
两种编译器生成的二进制格式不兼容（MSVC 使用 .lib，MinGW 使用 .a）。

### Q: 如何快速解决 MinGW 链接问题？

A: 三种方案：
1. 源码编译 MinGW 版本（最彻底）
2. 使用 vcpkg 安装 MinGW 版本（最方便）
3. 使用 MSYS2 仓库的 OpenCV 包

### Q: Python OpenCV 是否兼容？

A: Python 使用 pip 安装的预编译 wheel，不依赖 MinGW 或 MSVC 库，可立即使用。

## 项目集成

在 Qt 项目中使用 OpenCV：

```cpp
#ifdef HAS_OPENCV
#include <opencv2/opencv.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/highgui.hpp>

// 图像处理代码
cv::Mat image = cv::imread("test.jpg");
cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
#endif
```

---

**文档版本**: 1.0  
**最后更新**: 2025-01-XX  
**维护者**: Trae AI
