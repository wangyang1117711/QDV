# Q-DetectVision 部署说明

## 1. 环境要求

### 1.1 操作系统

- **Windows**: Windows 10 1903+ / Windows 11
- **架构**: x64

### 1.2 硬件要求

| 项目 | 最低要求 | 推荐配置 |
|------|---------|---------|
| CPU | 4核 | 8核+ |
| 内存 | 4GB | 8GB+ |
| 硬盘 | 200MB可用空间 | 10GB+（含模型） |
| 显卡 | 集成显卡 | NVIDIA GPU（支持CUDA 12.1+） |

### 1.3 依赖环境

| 依赖 | 版本 | 说明 |
|------|------|------|
| Qt6 | 6.7+ | UI框架 |
| OpenCV | 4.9.0 | 视觉算法库 |
| ONNX Runtime | 1.17+ | AI推理引擎 |
| SQLite3 | 3.45+ | 数据库 |
| vcpkg | 2024.04+ | C++包管理 |

## 2. 构建步骤

### 2.1 安装依赖

```bash
# 安装 vcpkg
git clone https://github.com/microsoft/vcpkg.git
cd vcpkg
./bootstrap-vcpkg.sh  # Linux/Mac
bootstrap-vcpkg.bat   # Windows

# 安装项目依赖
vcpkg install opencv:x64-windows
vcpkg install spdlog:x64-windows
vcpkg install nlohmann-json:x64-windows
vcpkg install qt6-base:x64-windows
vcpkg install qt6-quick:x64-windows
```

### 2.2 配置环境变量

```bash
# Windows
set VCPKG_ROOT=C:\path\to\vcpkg
set PATH=%VCPKG_ROOT%\installed\x64-windows\bin;%PATH%

# Linux/Mac
export VCPKG_ROOT=/path/to/vcpkg
export PATH=$VCPKG_ROOT/installed/x64-linux/bin:$PATH
```

### 2.3 编译项目

```bash
mkdir build && cd build

# 使用 vcpkg 工具链
cmake .. -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake

# 启用GPU支持（可选）
cmake .. -DCMAKE_TOOLCHAIN_FILE=... -DENABLE_GPU=ON

# 编译
cmake --build . --config Release -j8
```

### 2.4 构建测试

```bash
cd build
ctest -C Release
```

## 3. 部署打包

### 3.1 使用 NSIS（Windows）

```bash
# 安装 NSIS
# 创建安装脚本
makensis installer.nsi
```

### 3.2 手动打包

```bash
# 创建部署目录
mkdir -p QDV/bin
mkdir -p QDV/resources
mkdir -p QDV/models

# 复制可执行文件
cp build/bin/SmartVision.exe QDV/bin/

# 复制依赖DLL
cp %VCPKG_ROOT%/installed/x64-windows/bin/*.dll QDV/bin/

# 复制资源文件
cp -r resources/* QDV/resources/

# 压缩打包
zip -r QDV_v1.0.0.zip QDV/
```

## 4. 运行

### 4.1 启动软件

```bash
cd QDV/bin
SmartVision.exe
```

### 4.2 默认账户

- **用户名**: `a`
- **密码**: `a`

### 4.3 配置文件

配置文件位于 `%APPDATA%/QDetectVision/LocalConfig.ini`（Windows）或 `~/.config/QDetectVision/LocalConfig.ini`（Linux/Mac）。

## 5. 维护指南

### 5.1 日志管理

日志文件位于 `./logs/qdv_YYYY-MM-DD.log`，包含 DEBUG、INFO、WARN、ERROR 四个级别。

### 5.2 数据备份

```bash
# 备份数据库
cp data/qdv_results.db data/qdv_results_backup.db

# 备份方案
cp -r schemes/ schemes_backup/
```

### 5.3 升级步骤

1. 停止运行中的程序
2. 备份数据
3. 替换 `bin/` 目录中的可执行文件
4. 启动新版本

## 6. 故障排除

### 6.1 启动失败

- 检查依赖DLL是否齐全
- 查看日志文件获取错误信息
- 确认Qt环境变量配置正确

### 6.2 相机无法连接

- 检查网络连接
- 确认相机IP地址正确
- 检查防火墙设置

### 6.3 模型推理错误

- 确认模型文件路径正确
- 检查ONNX Runtime版本兼容性
- 确认GPU驱动已安装（启用GPU时）