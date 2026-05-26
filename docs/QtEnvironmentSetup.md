# Q-DetectVision Qt6 开发环境配置指南

## 一、系统环境检查结果

### 当前状态
| 项目 | 状态 | 说明 |
|:---|:---:|:---|
| CMake | ✅ | 版本 4.3.2 |
| MSVC 编译器 | ❌ | 未找到 cl.exe |
| Qt6 | ❌ | 未安装 |
| Ninja | ❌ | 未安装 |
| OpenCV | ❌ | 未安装 |

### 问题分析
当前系统缺少必要的开发工具链，无法直接编译 Qt6 C++ 项目。需要安装以下组件：

1. **Visual Studio 2022** - 包含 MSVC 编译器
2. **Qt6.5+** - Qt 开发框架
3. **OpenCV 4.x** - 计算机视觉库
4. **vcpkg** - 包管理器（可选）

---

## 二、Qt6 开发环境安装步骤

### 步骤1：安装 Visual Studio 2022

1. 下载 Visual Studio 2022 Installer
   - 官方下载：[Visual Studio 2022](https://visualstudio.microsoft.com/zh-hans/downloads/)
   
2. 安装工作负载：
   - **桌面开发使用 C++**（必须）
   - **通用 Windows 平台开发**（可选）

3. 安装完成后配置环境：
   ```powershell
   # 设置MSVC环境变量（根据实际安装路径）
   & "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"
   ```

### 步骤2：安装 Qt6.5

1. 下载 Qt Online Installer
   - 官方下载：[Qt Downloads](https://www.qt.io/download-open-source)

2. 选择组件：
   - Qt 6.5.x -> MSVC 2019/2022 (64-bit)
   - Qt 6.5.x -> Additional Libraries -> Qt Quick 3D
   - Qt 6.5.x -> Additional Libraries -> Qt Charts
   - Tools -> Qt Creator

3. 安装路径建议：`C:\Qt\6.5.0\msvc2022_64`

### 步骤3：安装 OpenCV 4.x

#### 方法A：使用 vcpkg（推荐）
```powershell
# 安装 vcpkg
git clone https://github.com/microsoft/vcpkg.git
cd vcpkg
.\bootstrap-vcpkg.bat

# 安装 OpenCV
.\vcpkg install opencv4:x64-windows
.\vcpkg integrate install
```

#### 方法B：手动安装
1. 下载 OpenCV Windows 包：[OpenCV Releases](https://opencv.org/releases/)
2. 解压到 `C:\opencv`
3. 设置环境变量：
   ```
   OPENCV_DIR=C:\opencv\build
   PATH += C:\opencv\build\x64\vc16\bin
   ```

### 步骤4：安装其他依赖
```powershell
# 使用 vcpkg 安装其他依赖
.\vcpkg install nlohmann-json:x64-windows
.\vcpkg install spdlog:x64-windows
```

---

## 三、项目配置与编译

### 配置环境变量
```powershell
# 设置 Qt 路径
$env:QTDIR = "C:\Qt\6.5.0\msvc2022_64"
$env:PATH = "$env:QTDIR\bin;$env:PATH"

# 设置 OpenCV 路径（如果手动安装）
$env:OpenCV_DIR = "C:\opencv\build"

# 验证配置
qmake --version
```

### 创建构建目录并编译
```powershell
# 创建构建目录
mkdir build -Force
cd build

# 配置 CMake（使用 vcpkg）
cmake .. `
    -G "Visual Studio 17 2022" `
    -A x64 `
    -DCMAKE_TOOLCHAIN_FILE="C:\path\to\vcpkg\scripts\buildsystems\vcpkg.cmake" `
    -DCMAKE_PREFIX_PATH="$env:QTDIR"

# 编译项目
cmake --build . --config Release

# 运行测试（可选）
ctest -C Release
```

### 使用 Qt Creator（推荐）
1. 打开 Qt Creator
2. 打开项目：选择 `QDV/CMakeLists.txt`
3. 配置构建套件：选择 Qt 6.5.0 (MSVC 2022 64-bit)
4. 点击构建按钮

---

## 四、编译验证清单

### 构建成功标志
| 检查项 | 预期结果 |
|:---|:---|
| CMake 配置 | 无错误，生成 .sln 文件 |
| 编译过程 | 无警告，无错误 |
| 输出文件 | `bin/Q-DetectVision.exe` |
| 测试运行 | 所有测试通过 |

### 常见问题

**Q1: CMake 找不到 Qt**
```powershell
# 确保设置了正确的路径
$env:CMAKE_PREFIX_PATH = "C:\Qt\6.5.0\msvc2022_64"
```

**Q2: 缺少 OpenCV**
```powershell
# 使用 vcpkg 安装或设置环境变量
$env:OpenCV_DIR = "C:\path\to\opencv\build"
```

**Q3: MSVC 编译器未找到**
```powershell
# 运行 Visual Studio 命令提示符或设置环境
& "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"
```

---

## 五、部署与运行

### 运行前准备
1. 确保 Qt DLL 路径在 PATH 中
2. 确保 OpenCV DLL 路径在 PATH 中

### 运行程序
```powershell
# 设置路径
$env:PATH = "C:\Qt\6.5.0\msvc2022_64\bin;$env:PATH"
$env:PATH = "C:\opencv\build\x64\vc16\bin;$env:PATH"

# 运行程序
.\bin\Release\Q-DetectVision.exe
```

### 使用 windeployqt（自动部署）
```powershell
cd bin/Release
windeployqt Q-DetectVision.exe
```

---

## 六、开发环境验证脚本

创建 `scripts/setup_env.ps1`:

```powershell
<#
Q-DetectVision 开发环境设置脚本
#>

Write-Host "=== Q-DetectVision 开发环境设置 ===" -ForegroundColor Cyan

# 设置 Qt 路径
$qtPath = "C:\Qt\6.5.0\msvc2022_64"
if (Test-Path $qtPath) {
    $env:QTDIR = $qtPath
    $env:PATH = "$qtPath\bin;$env:PATH"
    Write-Host "✓ Qt6 路径已设置" -ForegroundColor Green
} else {
    Write-Host "✗ Qt6 未安装在预期路径" -ForegroundColor Red
}

# 设置 OpenCV 路径（vcpkg 安装）
$vcpkgRoot = "C:\vcpkg"
$opencvDll = Join-Path $vcpkgRoot "installed\x64-windows\bin\opencv_world480.dll"
if (Test-Path $opencvDll) {
    $env:PATH = "$vcpkgRoot\installed\x64-windows\bin;$env:PATH"
    Write-Host "✓ OpenCV 路径已设置" -ForegroundColor Green
} else {
    Write-Host "✗ OpenCV 未安装" -ForegroundColor Red
}

# 验证环境
Write-Host "`n=== 环境验证 ===" -ForegroundColor Cyan

try {
    $qmakeVersion = qmake --version 2>&1
    Write-Host "✓ Qt 版本: $qmakeVersion" -ForegroundColor Green
} catch {
    Write-Host "✗ Qt 未找到" -ForegroundColor Red
}

try {
    $cmakeVersion = cmake --version
    Write-Host "✓ CMake 版本: $cmakeVersion" -ForegroundColor Green
} catch {
    Write-Host "✗ CMake 未找到" -ForegroundColor Red
}

Write-Host "`n环境设置完成！" -ForegroundColor Cyan
```

---

## 七、项目结构

```
Q-DetectVision/
├── CMakeLists.txt              # 主 CMake 配置
├── cmake/                      # CMake 模块
│   ├── CompilerSettings.cmake
│   ├── Dependencies.cmake
│   └── QDVConfig.cmake.in
├── src/                        # 源代码
│   ├── Core/                   # 核心模块
│   ├── Vision/                 # 视觉工具
│   ├── UI/                     # 用户界面
│   ├── Database/               # 数据库
│   ├── AI/                     # AI 推理
│   ├── Communication/          # 通信模块
│   └── Plugins/                # 插件系统
├── include/                    # 头文件
├── apps/
│   └── SmartVision/            # 主应用
├── tests/                      # 测试代码
├── resources/                  # 资源文件
└── docs/                       # 文档
```

---

## 八、总结

由于当前系统缺少 Qt6 开发环境，无法直接编译项目。请按照以下步骤操作：

1. **安装 Visual Studio 2022**（包含 C++ 工具链）
2. **安装 Qt 6.5.x**（MSVC 2022 64-bit）
3. **安装 OpenCV 4.x**（通过 vcpkg 或手动安装）
4. **运行环境设置脚本** `scripts/setup_env.ps1`
5. **使用 CMake 或 Qt Creator 编译项目**

完成以上步骤后，项目将能够正常编译和运行。