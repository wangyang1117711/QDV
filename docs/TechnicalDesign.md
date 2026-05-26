# Q-DetectVision 技术设计文档

## 1. 项目概述

Q-DetectVision（QDV）是一款面向工业质检场景的一体化机器视觉客户端软件，实现方案可视化编辑、深度学习全流程、多协议输出和GigE Vision相机管理。

## 2. 技术架构

### 2.1 分层架构

| 层级 | 组件 | 技术 |
|------|------|------|
| L1 | 主程序入口 | Qt6 QApplication |
| L2 | 界面层 | Qt6 QML |
| L3 | 业务逻辑层 | C++ Qt |
| L4 | 服务线程层 | C++ Thread |
| L5 | SDK/算法层 | OpenCV, ONNX Runtime |
| L6 | 硬件/系统层 | GigE Vision, TCP/UDP, SQLite |

### 2.2 核心模块

- **Core**: 核心数据结构（Scheme、VisionTool、ToolResult）
- **Vision**: 传统视觉工具链（模板匹配、边缘检测、Blob检测等）
- **AI**: 深度学习推理引擎（ONNX Runtime）
- **Communication**: 通信协议（TCP/UDP、串口、IO）
- **UI**: 用户界面（登录、主窗口、编辑器）
- **Database**: 结果存储（SQLite）
- **Plugins**: 插件系统

## 3. 核心类设计

### 3.1 Scheme 类

```cpp
class Scheme {
    QString id;                    // UUID
    QString name;                  // 方案名称
    QList<VisionTool*> toolChain;  // 工具链
    CameraConfig camera;           // 相机配置
    TriggerConfig trigger;         // 触发配置
    OutputConfig output;           // 输出配置
};
```

### 3.2 VisionTool 接口

```cpp
class VisionTool {
    virtual QString type() = 0;
    virtual bool configure(const QJsonObject& params) = 0;
    virtual bool execute(const cv::Mat& input, ToolResult& result) = 0;
    virtual QJsonObject serialize() const = 0;
};
```

### 3.3 ToolChainExecutor

负责执行工具链，支持顺序/并行执行和条件分支跳转。

## 4. 数据格式

### 4.1 方案文件（.svscheme）

JSON格式，包含版本、ID、名称、工具链、配置等信息。

### 4.2 模型文件（.svmodel）

ZIP压缩包，包含ONNX模型、元数据、分类标签等。

## 5. 关键技术

### 5.1 ONNX Runtime 推理
- 统一 CPU/GPU 推理接口
- LRU缓存管理
- 推理预热机制

### 5.2 GigE Vision 相机管理
- GenICam 标准接口
- 设备发现与枚举
- 参数配置与图像采集

### 5.3 插件系统
- DLL动态加载
- 热插拔支持
- 沙箱隔离

## 6. API 接口

### 6.1 SchemeManager

| 方法 | 功能 |
|------|------|
| loadScheme(path) | 加载方案 |
| saveScheme(scheme, path) | 保存方案 |
| listSchemes(dir) | 列出方案 |
| deleteScheme(path) | 删除方案 |

### 6.2 ToolChainExecutor

| 方法 | 功能 |
|------|------|
| setTools(tools) | 设置工具列表 |
| execute(input) | 执行工具链 |
| stop() | 停止执行 |
| getResults() | 获取结果 |

## 7. 部署说明

### 7.1 依赖环境

- Qt6.7+
- OpenCV 4.9.0
- ONNX Runtime 1.17+
- SQLite3 3.45+

### 7.2 构建步骤

```bash
mkdir build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build . --config Release
```

### 7.3 运行

```bash
./bin/SmartVision.exe
```

## 8. 安全考虑

- 方案文件加密存储
- 插件沙箱隔离
- 危险操作拦截
- 日志审计