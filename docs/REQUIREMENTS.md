# 奇测视觉检测系统 (QDetectVision) — 开发需求规格文档

> **文档版本**：v2.0  
> **更新日期**：2026-05-25  
> **项目代号**：QDetectVision  
> **文档状态**：已完成（反映最新实现状态）

---

## 目录

1. [项目概述](#1-项目概述)
2. [系统架构](#2-系统架构)
3. [技术规格](#3-技术规格)
4. [功能模块详细描述](#4-功能模块详细描述)
5. [非功能需求](#5-非功能需求)
6. [验收标准](#6-验收标准)
7. [需求优先级](#7-需求优先级)
8. [依赖关系](#8-依赖关系)
9. [风险评估与缓解](#9-风险评估与缓解)
10. [时间节点与里程碑](#10-时间节点与里程碑)
11. [附录](#11-附录)

---

## 1. 项目概述

### 1.1 项目背景

QDetectVision（奇测视觉检测系统）是一款面向工业自动化场景的智能视觉检测桌面应用程序。系统基于 Qt6 C++ 框架构建，集成 OpenCV 计算机视觉库，为工业制造领域提供相机采集、图像处理、方案管理、检测监控等核心视觉检测能力，并配备小模型训练与快速推理模块。

### 1.2 项目目标

| 目标 | 描述 | 优先级 |
|------|------|--------|
| **G1** | 提供完整的工业相机采集与图像预览功能 | P0 |
| **G2** | 支持视觉检测方案的可视化编辑与管理 | P0 |
| **G3** | 实现工具链式图像处理流程编排 | P0 |
| **G4** | 提供实时检测监控与告警面板 | P1 |
| **G5** | 支持 TCP/串口通信与 IO 监控 | P1 |
| **G6** | 集成小模型快速训练与推理能力 | P2 |
| **G7** | 提供 Python 脚本编辑器与在线执行 | P2 |
| **G8** | 支持检测结果的导出（CSV/JSON） | P2 |

### 1.3 适用范围

- 目标平台：Windows x64
- 目标用户：工业视觉工程师、质检操作员、自动化产线维护人员
- 部署环境：工控机 / 台式工作站（中等配置及以上）

---

## 2. 系统架构

### 2.1 分层架构

```
┌─────────────────────────────────────────────────────────────┐
│                    用户交互层 (Entry Layer)                    │
│   main.cpp 入口 → MainWindow → CentralWindow（8视图导航）      │
├─────────────────────────────────────────────────────────────┤
│                    表现层 (UI Views Layer)                     │
│ HomeView │ CameraView │ SchemeView │ EditView │ IOView       │
│ CommView │ MonitorView │ TrainingInferenceView [NEW]         │
├─────────────────────────────────────────────────────────────┤
│                    业务逻辑层 (Business Logic Layer)            │
│ Core (AuthService, Logger, SchemeMgr, UndoMgr, Config)       │
│ Vision (VisionTool 基类, Blob/Edge/Threshold/Preprocess)      │
│ AI (InferenceEngine, ModelManager)                           │
│ TrainingInference (ImageMgr, CategoryMgr, ExportMgr) [NEW]    │
├─────────────────────────────────────────────────────────────┤
│                    数据与通信层 (Data & Comm Layer)             │
│ TCPCommunicator │ SerialCommunicator │ IOController          │
│ ResultDatabase │ ExportManager │ Python QProcess              │
├─────────────────────────────────────────────────────────────┤
│                    基础设施层 (Infrastructure Layer)            │
│ Qt6 Core/Gui/Widgets/Network/Sql │ OpenCV 4.13.0             │
│ MinGW-w64 GCC 11.2.0 │ CMake 3.x │ Python Environment        │
└─────────────────────────────────────────────────────────────┘
```

> 完整架构图见：[`architecture.drawio`](file:///E:/anchor/Trae/QDV/docs/architecture.drawio)（可在 draw.io 中打开编辑）

### 2.2 模块依赖关系

```
QDetectVision.exe
 ├── Qt6::Core / Qt6::Gui / Qt6::Widgets / Qt6::Network / Qt6::Sql
 ├── OpenCV (opencv_world4130.dll)
 ├── Core (libCore.a)
 ├── AI (libAI.a) → Core
 ├── UI (libUI.a) → Core
 └── TrainingInference (libTrainingInference.a) → Core, AI, Qt6::Widgets
```

### 2.3 构建系统

| 项 | 值 |
|----|-----|
| 构建工具 | CMake 3.x |
| C++ 标准 | C++20 |
| 元对象编译 | AUTOMOC, AUTORCC, AUTOUIC |
| 部署工具 | windeployqt（Qt 运行时 DLL 自动部署） |
| 后处理 | 自动复制 OpenCV DLL 到输出目录 |

---

## 3. 技术规格

### 3.1 技术栈

| 技术 | 版本 | 用途 |
|------|------|------|
| C++ | C++20 | 主开发语言 |
| Qt | 6.x | UI 框架、网络、SQL、信号/槽 |
| OpenCV | 4.13.0 (MinGW 编译) | 图像采集、处理、计算机视觉 |
| MinGW-w64 GCC | 11.2.0 | 编译工具链 |
| CMake | 3.16+ | 构建系统 |
| Python | 3.x (系统环境) | 脚本引擎（QProcess 调用） |
| QSS (Qt Style Sheets) | — | 全局深色主题样式 |

### 3.2 性能指标

| 指标 | 目标值 | 当前状态 |
|------|--------|----------|
| 应用启动时间 | ≤ 3 秒 | ✅ 实测 ~2 秒 |
| 视图切换延迟 | ≤ 100ms | ✅ 即时切换 |
| 相机帧率 | 30 FPS (1280×720) | ✅ OpenCV VideoCapture |
| 单张推理延迟 | ≤ 100ms | ⚠️ 模拟数据（待接入真实模型） |
| 内存占用 | ≤ 500 MB | ✅ 正常范围内 |
| 支持图像格式 | PNG/JPG/BMP/TIFF/WebP | ✅ 已实现 |

### 3.3 UI/UX 规格

| 项 | 值 |
|----|-----|
| 语言 | 简体中文 |
| 主题 | 深色主题（全局 QSS） |
| 背景色 | `#1e1e1e`（全局）/ `#2d2d2d`（容器）/ `#252525`（面板） |
| 主文字 | `#e0e0e0` |
| 次要文字 | `#ccc` / `#aaa` / `#888` |
| 边框 | `#444` / `#555` |
| 主题色 | `#660874`（紫色系） |
| 最小分辨率 | 1200 × 800 |
| 字体 | Microsoft YaHei / Consolas（代码编辑器） |

### 3.4 兼容性要求

| 项 | 要求 |
|----|------|
| 操作系统 | Windows 10/11 x64 |
| 运行时 | Qt6 运行时 DLL（windeployqt 自动部署） |
| OpenCV DLL | opencv_world4130.dll（构建后自动复制） |
| 编译器 | MinGW-w64 GCC 11.2.0（必须，ABI 兼容性） |
| Python | 可选（ScriptEditorWidget 功能需系统安装 Python） |

---

## 4. 功能模块详细描述

### 4.1 用户认证模块 (LoginView)

**文件**：[`include/UI/LoginView.h`](file:///E:/anchor/Trae/QDV/include/UI/LoginView.h) | [`src/UI/LoginView.cpp`](file:///E:/anchor/Trae/QDV/src/UI/LoginView.cpp)

| 属性 | 值 |
|------|-----|
| 优先级 | P0 |
| 状态 | ✅ 已完成 |

**功能描述**：

- 用户名/密码输入框，支持回车键快捷登录
- "记住我"功能（QSettings 持久化，密码 Base64 编码存储）
- 表单验证（空值检查 + 错误提示）
- 登录成功 → 信号驱动切换到主界面
- 登录失败 → 红色错误提示 + 密码框清空并聚焦

**核心类**：
- `LoginView`：登录界面组件，发射 `loginSuccess(username)` / `loginFailed(reason)`
- `AuthService`（单例）：认证服务，`login(username, password) → bool`

**验收标准**：

- [x] 用户名空 → 显示"请输入用户名"
- [x] 密码空 → 显示"请输入密码"
- [x] 凭据错误 → 显示"用户名或密码错误"
- [x] 记住我勾选 → 重启后自动填充
- [x] 登录成功 → 过渡动画切换到主界面

---

### 4.2 主窗口框架 (MainWindow)

**文件**：[`include/UI/MainWindow.h`](file:///E:/anchor/Trae/QDV/include/UI/MainWindow.h) | [`src/UI/MainWindow.cpp`](file:///E:/anchor/Trae/QDV/src/UI/MainWindow.cpp)

| 属性 | 值 |
|------|-----|
| 优先级 | P0 |
| 状态 | ✅ 已完成 |

**功能描述**：

- 自定义标题栏（紫色 `#660874` 背景，Logo + 标题 + 版本号）
- 菜单栏（文件/编辑/视图/工具/帮助，含"关于"对话框）
- 状态栏（就绪提示）
- QStackedWidget 管理 LoginView ↔ CentralWindow 切换
- 窗口淡入淡出过渡动画（QPropertyAnimation）
- 窗口状态持久化（QSettings：geometry/windowState）
- 菜单项："关于"弹出 Qt + OpenCV 版本信息

**核心类**：
- `MainWindow`：主窗口，连接 `LoginView::loginSuccess → showMain()` / `CentralWindow::logout → showLogin()`

**验收标准**：

- [x] 窗口标题显示"奇测视觉检测系统 v1.0"
- [x] 菜单栏所有菜单可展开
- [x] "关于"对话框显示 Qt + OpenCV 版本
- [x] Ctrl+Q 退出
- [x] 窗口关闭后重启保持尺寸和位置

---

### 4.3 中央导航视图 (CentralWindow)

**文件**：[`include/UI/CentralWindow.h`](file:///E:/anchor/Trae/QDV/include/UI/CentralWindow.h) | [`src/UI/CentralWindow.cpp`](file:///E:/anchor/Trae/QDV/src/UI/CentralWindow.cpp)

| 属性 | 值 |
|------|-----|
| 优先级 | P0 |
| 状态 | ✅ 已完成 |

**功能描述**：

- 顶部导航栏（紫色下划线指示选中状态）
- 8 个导航按钮：首页(0) / 相机(1) / 方案(2) / 编辑(3) / IO监控(4) / 通信(5) / 监控(6) / 训练推理(7) [NEW]
- Alt+1~8 键盘快捷键切换视图
- 退出登录按钮（hover 红色高亮）
- 首页仪表盘：4 个统计卡片 + 7 个快速入口步骤卡片（可点击跳转）
- 统计卡片实时响应子模块信号更新计数

**8 个视图**：

| 索引 | 名称 | 快捷键 | 组件 |
|------|------|--------|------|
| 0 | 首页 | Alt+1 | HomeView（内置） |
| 1 | 相机 | Alt+2 | CameraView |
| 2 | 方案 | Alt+3 | SchemeView |
| 3 | 编辑 | Alt+4 | EditView |
| 4 | IO监控 | Alt+5 | IOView |
| 5 | 通信 | Alt+6 | CommView |
| 6 | 监控 | Alt+7 | MonitorView |
| 7 | 训练推理 | Alt+8 | TrainingInferenceView [NEW] |

**验收标准**：

- [x] 8 个导航按钮可点击切换视图
- [x] Alt+1~8 快捷键全部可用
- [x] 首页统计卡片数值随子模块更新
- [x] 退出登录按钮触发 Logout → 返回登录界面
- [x] 步骤卡片点击跳转到对应视图

---

### 4.4 相机视图 (CameraView)

**文件**：[`include/UI/CameraView.h`](file:///E:/anchor/Trae/QDV/include/UI/CameraView.h) | [`src/UI/CameraView.cpp`](file:///E:/anchor/Trae/QDV/src/UI/CameraView.cpp)

| 属性 | 值 |
|------|-----|
| 优先级 | P0 |
| 状态 | ✅ 已完成（功能骨架） |

**功能描述**：

- 相机选择下拉框（Camera 0/1）
- 分辨率选择（640×480 ~ 1920×1080）
- 帧率选择（15/30/60 FPS）
- 连接/断开相机按钮
- 拍照快照功能（保存为 PNG/JPG）
- 录像功能（预留，待实现）
- 实时视频预览（QLabel + OpenCV cv::VideoCapture）
- 帧缓存（cv::Mat 存储最后一帧）

**核心类**：
- `CameraView`：相机界面，`startCamera(idx)` / `stopCamera()` / `updateFrame()` / `saveSnapshot()` / `applyCameraSettings()`

**验收标准**：

- [x] 连接相机 → 预览画面显示
- [x] 断开 → 预览清空显示提示文字
- [x] 拍照 → 文件对话框保存 PNG/JPG
- [ ] 录像功能 → 待开发
- [x] 分辨率和帧率选择可正确设置

---

### 4.5 方案视图 (SchemeView)

**文件**：[`include/UI/SchemeView.h`](file:///E:/anchor/Trae/QDV/include/UI/SchemeView.h) | [`src/UI/SchemeView.cpp`](file:///E:/anchor/Trae/QDV/src/UI/SchemeView.cpp)

| 属性 | 值 |
|------|-----|
| 优先级 | P0 |
| 状态 | ✅ 已完成 |

**功能描述**：

- 方案树形列表（名称/版本/工具数/更新时间）
- 新建方案（QInputDialog 输入名称）
- 保存方案（QFileDialog → JSON 文件）
- 加载方案（从 JSON 文件恢复）
- 方案数量信号 → 更新首页统计卡片

**核心类**：
- `SchemeView`：方案视图，`onNewScheme()` / `onSaveScheme()` / `onLoadScheme()` / `refreshSchemeTree()`
- `SchemeData`：方案数据结构体（name, version, author, tools）

**验收标准**：

- [x] 新建方案 → 树中出现新条目
- [x] 保存方案 → JSON 文件正确写入
- [x] 加载方案 → 树恢复之前保存的数据
- [x] 方案数变更 → 首页统计卡片更新

---

### 4.6 编辑视图 (EditView)

**文件**：[`include/UI/EditView.h`](file:///E:/anchor/Trae/QDV/include/UI/EditView.h) | [`src/UI/EditView.cpp`](file:///E:/anchor/Trae/QDV/src/UI/EditView.cpp)

| 属性 | 值 |
|------|-----|
| 优先级 | P0 |
| 状态 | ✅ 已完成（功能骨架） |

**功能描述**：

- 三栏布局（QSplitter 可拖拽）：工具库 | 画布 | 属性面板
- 工具库（13 个工具按类别分组，可展开/折叠）
- 双击工具 → 添加到工具链
- 工具链支持拖拽排序（InternalMove）
- 属性面板（选中工具链中的工具 → 显示参数：名称/阈值/缩放/模式/ROI）
- 撤销/重做（QUndoStack + AddToolCommand 命令模式）
- 删除工具（自动更新序号）
- 运行检测按钮 → 切换到监控视图
- 工具数量信号 → 更新首页统计卡片

**核心类**：
- `EditView`：编辑视图，`onUndo()` / `onRedo()` / `onDeleteTool()` / `onToolDoubleClicked()`
- `AddToolCommand`：QUndoCommand 子类，封装添加/撤销操作

**验收标准**：

- [x] 双击工具库中的工具 → 出现在工具链
- [x] 撤销 → 移除最近添加的工具
- [x] 重做 → 恢复撤销的工具
- [x] 选中工具链中的工具 → 属性面板显示参数
- [x] 点击"运行检测" → 切换到监控视图

---

### 4.7 IO 监控视图 (IOView)

**文件**：[`include/UI/IOView.h`](file:///E:/anchor/Trae/QDV/include/UI/IOView.h) | [`src/UI/IOView.cpp`](file:///E:/anchor/Trae/QDV/src/UI/IOView.cpp)

| 属性 | 值 |
|------|-----|
| 优先级 | P1 |
| 状态 | ✅ 已完成 |

**功能描述**：

- IO 通道表格（通道ID / 名称 / 状态 / 更新时间）
- DI/DO/AI/AO 四类通道
- 刷新按钮 → 重新生成状态（使用 QRandomGenerator 模拟）
- 导出日志（预留，待实现）

**验收标准**：

- [x] 表格显示所有 IO 通道
- [x] 刷新 → 状态和时间更新

---

### 4.8 通信视图 (CommView)

**文件**：[`include/UI/CommView.h`](file:///E:/anchor/Trae/QDV/include/UI/CommView.h) | [`src/UI/CommView.cpp`](file:///E:/anchor/Trae/QDV/src/UI/CommView.cpp)

| 属性 | 值 |
|------|-----|
| 优先级 | P1 |
| 状态 | ✅ 已完成 |

**功能描述**：

- TCP 连接配置（IP 地址 + 端口号）
- 连接/断开按钮
- 通信日志窗口（只读 QTextEdit，时间戳标注）
- 数据发送（QLineEdit + 回车键 / 发送按钮）
- 清空日志按钮

**核心类**：
- `CommView`：通信视图，`tcpConnect(host, port)` / `tcpDisconnect()` / `tcpSend(data)`
- `QTcpSocket`：底层 TCP 通信

**验收标准**：

- [x] 连接指定 IP:Port → 日志记录连接成功
- [x] 断开 → 日志记录断开
- [x] 发送数据 → 日志记录发送内容
- [x] 清空 → 日志清除

---

### 4.9 监控视图 (MonitorView)

**文件**：[`include/UI/MonitorView.h`](file:///E:/anchor/Trae/QDV/include/UI/MonitorView.h) | [`src/UI/MonitorView.cpp`](file:///E:/anchor/Trae/QDV/src/UI/MonitorView.cpp)

| 属性 | 值 |
|------|-----|
| 优先级 | P1 |
| 状态 | ✅ 已完成 |

**功能描述**：

- 系统状态面板（运行中/停止/速度/良品率）
- 检测进度条（百分比）
- 检测计数面板（总数/合格数/不合格数）
- 开始检测 / 停止检测 / 重置计数 按钮
- 定时器每 2 秒自动更新检测和告警计数
- 检测计数信号 → 更新首页统计卡片
- 告警计数信号 → 更新首页统计卡片

**验收标准**：

- [x] 开始检测 → 面板数据更新
- [x] 停止检测 → 数据停止更新
- [x] 重置 → 所有计数归零
- [x] 定时器每 2 秒更新数据

---

### 4.10 训练推理视图 (TrainingInferenceView) [NEW]

**文件**：[`include/TrainingInference/TrainingInferenceView.h`](file:///E:/anchor/Trae/QDV/include/TrainingInference/TrainingInferenceView.h) | [`src/TrainingInference/TrainingInferenceView.cpp`](file:///E:/anchor/Trae/QDV/src/TrainingInference/TrainingInferenceView.cpp)

| 属性 | 值 |
|------|-----|
| 优先级 | P2 |
| 状态 | ✅ 已完成（v1.0） |

**功能概述**：

小模型快速训练与快速推理功能模块，提供从图像导入、类别标注、模型选择、脚本预处理到推理执行与结果导出的完整工作流。

**子组件**：

| 组件 | 文件 | 功能 |
|------|------|------|
| ImageManager | [h](file:///E:/anchor/Trae/QDV/include/TrainingInference/ImageManager.h) / [cpp](file:///E:/anchor/Trae/QDV/src/TrainingInference/ImageManager.cpp) | 单例，图像导入/移除/缩略图生成，支持 PNG/JPG/BMP/TIFF/WebP |
| CategoryManager | [h](file:///E:/anchor/Trae/QDV/include/TrainingInference/CategoryManager.h) / [cpp](file:///E:/anchor/Trae/QDV/src/TrainingInference/CategoryManager.cpp) | 单例，类别树 CRUD、JSON 持久化、模糊搜索 |
| ExportManager | [h](file:///E:/anchor/Trae/QDV/include/TrainingInference/ExportManager.h) / [cpp](file:///E:/anchor/Trae/QDV/src/TrainingInference/ExportManager.cpp) | 单例，CSV/JSON/TXT 三种格式导出 |
| ImageViewWidget | [h](file:///E:/anchor/Trae/QDV/include/TrainingInference/ImageViewWidget.h) / [cpp](file:///E:/anchor/Trae/QDV/src/TrainingInference/ImageViewWidget.cpp) | 图像渲染、滚轮缩放、鼠标平移、Ctrl+拖拽矩形标注 |
| CategoryPanel | [h](file:///E:/anchor/Trae/QDV/include/TrainingInference/CategoryPanel.h) / [cpp](file:///E:/anchor/Trae/QDV/src/TrainingInference/CategoryPanel.cpp) | 类别树展示、搜索、右键菜单 CRUD |
| InferencePanel | [h](file:///E:/anchor/Trae/QDV/include/TrainingInference/InferencePanel.h) / [cpp](file:///E:/anchor/Trae/QDV/src/TrainingInference/InferencePanel.cpp) | 模型选择下拉框、批量/单张推理模式、进度条 |
| ScriptEditorWidget | [h](file:///E:/anchor/Trae/QDV/include/TrainingInference/ScriptEditorWidget.h) / [cpp](file:///E:/anchor/Trae/QDV/src/TrainingInference/ScriptEditorWidget.cpp) | Python 语法高亮（PythonHighlighter）、QProcess 执行、stdout/stderr 捕获 |
| ResultPanel | [h](file:///E:/anchor/Trae/QDV/include/TrainingInference/ResultPanel.h) / [cpp](file:///E:/anchor/Trae/QDV/src/TrainingInference/ResultPanel.cpp) | 5 列表格（图像/类别/置信度/柱状图/状态）、CSV/JSON 导出按钮 |

**界面布局**：

```
┌── 工具栏 ──────────────────────────────────────────────────┐
│ [导入图像] [导入文件夹] [启动训练] [快速推理] │ [筛选...]      │
├──────────┬─────────────────────────┬──────────────────────┤
│ 图像列表  │ 类别面板   推理面板      │ 脚本编辑器/结果面板    │
│          │                         │                      │
│ [列表项]  │ [搜索...]              │ Python 代码编辑区     │
│ [列表项]  │  ├ 类别A               │ (语法高亮)            │
│ [列表项]  │  └ 类别B               │                      │
│          │ [添加][编辑][删除]       │ 或: 推理结果表格      │
│          │ [模型选择 ▼]            │                      │
│ ┌──────┐ │ [开始推理] [停止推理]    │                      │
│ │ 图像  │ │ [████████░░] 进度       │                      │
│ │ 预览  │ │                        │                      │
│ └──────┘ │                        │                      │
├──────────┴─────────────────────────┴──────────────────────┤
│ 训练日志                                                  │
└──────────────────────────────────────────────────────────┘
```

**验收标准**：

- [x] 导入图像 → 左侧列表显示缩略图
- [x] 导入文件夹 → 批量导入所有支持的图像格式
- [x] 点击列表项 → 预览图在下方显示
- [x] 滚轮 → 缩放预览图
- [x] 鼠标拖拽 → 平移预览图
- [x] Ctrl+拖拽 → 绘制矩形标注
- [x] 添加/编辑/删除类别 → 树实时更新
- [x] 搜索类别 → 过滤显示
- [x] 选择模型 → 显示模型信息
- [x] 勾选批量模式 → 对所有图像推理
- [x] 快速推理 → 生成模拟推理结果
- [x] 结果以表格形式展示（类别/置信度/状态）
- [x] 导出 CSV → 文件正确保存
- [x] 导出 JSON → 文件正确保存
- [x] Python 编辑器语法高亮正确（关键字/字符串/注释/数字/装饰器）
- [x] 运行脚本 → stdout 输出到控制台
- [ ] 真实模型加载推理 → 待接入（当前为模拟数据）

---

### 4.11 AI 推理引擎模块

**文件**：[`include/AI/InferenceEngine.h`](file:///E:/anchor/Trae/QDV/include/AI/InferenceEngine.h) | [`src/AI/InferenceEngine.cpp`](file:///E:/anchor/Trae/QDV/src/AI/InferenceEngine.cpp) | [`include/AI/ModelManager.h`](file:///E:/anchor/Trae/QDV/include/AI/ModelManager.h) | [`src/AI/ModelManager.cpp`](file:///E:/anchor/Trae/QDV/src/AI/ModelManager.cpp)

| 属性 | 值 |
|------|-----|
| 优先级 | P2 |
| 状态 | ✅ 已完成（接口层） |

**核心类**：
- `InferenceEngine`：推理引擎接口，`loadModel(path)` / `unloadModel()` / `infer(input, output, result)`
- `ModelManager`（单例）：模型管理器，`loadModel(path, id)` / `unloadModel(id)` / `getEngine(id)` / `listModels(dir)`

**验收标准**：

- [x] 模型文件存在 → loadModel 返回 true
- [x] 模型文件不存在 → loadModel 返回 false
- [x] infer 调用返回输入图像副本 + JSON 元信息
- [ ] 真实 ONNX/PyTorch 模型推理 → 待接入

---

### 4.12 全局深色主题

**文件**：[`apps/SmartVision/main.cpp`](file:///E:/anchor/Trae/QDV/apps/SmartVision/main.cpp)

| 属性 | 值 |
|------|-----|
| 优先级 | P1 |
| 状态 | ✅ 已完成 |

**QSS 覆盖范围**：

- QWidget / QMainWindow / QGroupBox — 背景色、边框
- QPushButton / QToolButton — 默认/悬停/禁用/选中状态
- QLineEdit / QTextEdit / QSpinBox / QDoubleSpinBox / QComboBox — 输入框样式
- QTableWidget / QTreeWidget / QListWidget — 表格/树/列表
- QScrollBar — 垂直/水平
- QMenu / QMenuBar — 菜单系统
- QToolTip / QStatusBar / QLabel / QHeaderView — 辅助组件

**配色方案**：

| 元素 | 颜色 |
|------|------|
| 全局背景 | `#1e1e1e` |
| 容器/卡片/工具栏背景 | `#2d2d2d` / `#252525` |
| 主文字 | `#e0e0e0` |
| 次要文字 | `#ccc` / `#aaa` / `#888` |
| 边框 | `#444` / `#555` |
| 主题色（按钮/选中/标题栏） | `#660874` |

---

## 5. 非功能需求

### 5.1 性能

| 指标 | 要求 |
|------|------|
| 启动时间 | ≤ 3 秒 |
| UI 响应 | 主线程不阻塞，所有耗时操作异步 |
| 内存泄漏 | 零泄漏，QObject 树正确销毁 |
| 编译时间 | 增量编译 ≤ 30 秒 |

### 5.2 安全性

- 密码使用 Base64 编码存储（QSettings）
- 无明文日志记录密码
- 所有外部输入（图像路径、网络数据）进行格式校验

### 5.3 可维护性

- 模块化目录结构（Core / AI / UI / TrainingInference / Vision / Communication）
- 单例模式统一管理全局服务
- QObject 父子关系自动内存管理
- 信号/槽实现模块间解耦通信

### 5.4 可扩展性

- IPlugin 插件接口预留扩展点
- ToolFactory 工厂模式支持新视觉工具注册
- TrainingInference 模块支持新的导出格式

### 5.5 深色主题一致性

- 全局 QSS 样式表 + 组件的内联 styleSheet 属性
- 所有新增组件强制遵循深色主题配色

---

## 6. 验收标准

### 6.1 构建与部署

- [x] `cmake --build .` 零错误零警告（当前状态：100% 通过）
- [x] windeployqt 自动部署 Qt 运行时 DLL
- [x] OpenCV DLL 自动复制到输出目录
- [x] 生成 `QDetectVision.exe` 可独立运行

### 6.2 核心功能

- [x] 登录/登出流程正常
- [x] 8 个视图全部可导航（含新增训练推理视图）
- [x] 相机预览、拍照功能可用
- [x] 方案创建、保存、加载可用
- [x] 工具链编辑支持撤销/重做
- [x] IO 监控表格可刷新
- [x] TCP 通信连接/发送/日志可用
- [x] 监控面板数据定时更新
- [x] 训练推理图像导入、类别管理、推理、导出可用

### 6.3 UI/UX

- [x] 全局深色主题一致
- [x] 中文界面无乱码
- [x] 快捷键全部可用（Alt+1~8, Ctrl+Q）
- [x] 窗口状态持久化

---

## 7. 需求优先级

| 优先级 | 级别定义 | 模块 |
|--------|----------|------|
| **P0** | 核心功能，必须实现 | LoginView, MainWindow, CentralWindow, CameraView, SchemeView, EditView |
| **P1** | 重要功能，应当实现 | IOView, CommView, MonitorView, 全局深色主题 |
| **P2** | 增强功能，可延迟 | TrainingInferenceView [NEW], AI 推理引擎, Python 脚本编辑器 |
| **P3** | 未来规划，暂未开发 | Vision 工具链完整集成, Database 模块, Plugins 插件系统, 真实模型推理 |

---

## 8. 依赖关系

### 8.1 外部依赖

| 依赖 | 版本 | 类型 | 安装方式 |
|------|------|------|----------|
| Qt6 | 6.x | 运行时 SDK | Qt 官方安装器 |
| OpenCV | 4.13.0 | 编译链接 | MinGW 源码编译 |
| GCC | 11.2.0 | 编译工具链 | MinGW-w64 |
| CMake | 3.16+ | 构建工具 | 系统安装 |
| Python | 3.x | 可选运行时 | 系统安装（ScriptEditor 需要） |

### 8.2 模块间依赖

```
TrainingInferenceView → ImageManager, CategoryManager, ExportManager
                      → CategoryPanel, InferencePanel, ScriptEditorWidget, ResultPanel
                      → ImageViewWidget
                      → AI::InferenceEngine, AI::ModelManager

CentralWindow → CameraView, SchemeView, EditView, IOView, CommView, MonitorView
              → TrainingInferenceView [NEW]

MainWindow → LoginView, CentralWindow

InferencePanel → AI::InferenceEngine
ScriptEditorWidget → Python (QProcess)
```

---

## 9. 风险评估与缓解

| 风险 | 影响 | 概率 | 缓解措施 | 状态 |
|------|------|------|----------|------|
| **PowerShell 编码损坏** | 高（源文件全部清空） | 高 | ⚠️ 禁止使用 PowerShell `Set-Content` 修改 C++ 源文件；统一使用 IDE/SearchReplace 工具 | ✅ 已规避 |
| **MOC 未生成** | 高（链接失败） | 中 | CMakeLists.txt 显式 glob 头文件，确保 AUTOMOC 扫描 | ✅ 已修复 |
| **模块未集成** | 高（功能不可见） | 中 | 新模块需同步更新 CentralWindow 导航和视图栈 | ✅ 已修复 |
| **OpenCV 编译兼容性** | 中（链接失败） | 低 | 使用 `link_directories()` + 后处理 DLL 复制 | ✅ 已修复 |
| **真实模型推理性能** | 中（推理耗时高） | 中 | 预留异步接口，后续接入 ONNX Runtime | ⚠️ 待评估 |
| **Python 环境缺失** | 低（脚本编辑不可用） | 中 | 检测 Python 是否安装，缺失时禁用脚本执行按钮 | 🔲 待实现 |
| **中文编码** | 中（界面乱码） | 中 | 源文件统一 UTF-8 编码，避免使用会改变编码的工具 | ✅ 已修复 |

---

## 10. 时间节点与里程碑

| 里程碑 | 描述 | 状态 |
|--------|------|------|
| **M1** | 项目骨架搭建：CMake、Qt6、OpenCV 配置 | ✅ 已完成 |
| **M2** | 29 个按钮/菜单项功能实现 | ✅ 已完成 |
| **M3** | 深色主题全局转换 | ✅ 已完成 |
| **M4** | TrainingInference 模块开发与集成 | ✅ 已完成 |
| **M5** | 乱码修复 + MOC/链接修复 | ✅ 已完成 |
| **M6** | Python 环境检测与降级处理 | 🔲 计划中 |
| **M7** | 真实模型推理接入（ONNX Runtime） | 🔲 计划中 |
| **M8** | Vision 工具链完整集成 + ToolChainExecutor | 🔲 计划中 |
| **M9** | Plugins 插件系统实现 | 🔲 计划中 |
| **M10** | 打包发布（NSIS/MSI 安装包） | 🔲 计划中 |

---

## 11. 附录

### 11.1 项目文件清单

```
QDetectVision/
├── CMakeLists.txt                          # 根构建配置
├── docs/
│   ├── architecture.drawio                 # 架构图（可编辑）
│   └── REQUIREMENTS.md                     # 本需求文档
├── include/
│   ├── Core/   (AuthService, Logger, SchemeManager, UndoManager, ...)
│   ├── AI/     (InferenceEngine, ModelManager)
│   ├── UI/     (MainWindow, CentralWindow, LoginView, CameraView, ...)
│   └── TrainingInference/  (ImageViewWidget, CategoryPanel, ...)
├── src/
│   ├── Core/   (AuthService.cpp, Logger.cpp, ...)
│   ├── AI/     (InferenceEngine.cpp, ModelManager.cpp)
│   ├── UI/     (MainWindow.cpp, CentralWindow.cpp, ...)
│   └── TrainingInference/  (ImageViewWidget.cpp, ...)
├── apps/
│   └── SmartVision/
│       ├── CMakeLists.txt
│       └── main.cpp
└── build/
    └── bin/QDetectVision.exe               # 编译产物
```

### 11.2 关键信号/槽连接图

```
LoginView::loginSuccess    ──→ MainWindow::onLoginSuccess    (切换到主界面)
CentralWindow::logout      ──→ MainWindow::onLogout          (返回登录界面)
CentralWindow::viewChanged ──→ MainWindow                     (视图名通知)
SchemeView::schemeCountChanged   ──→ CentralWindow::updateSchemeCount
EditView::toolCountChanged       ──→ CentralWindow::updateToolCount
MonitorView::detectionCountChanged ──→ CentralWindow::updateDetectionCount
MonitorView::alertCountChanged     ──→ CentralWindow::updateAlertCount
EditView::requestRunDetection     ──→ CentralWindow (switchView(5))
InferencePanel::inferenceRequested ──→ TrainingInferenceView::onInferenceRequested
ImageManager::imagesImported      ──→ TrainingInferenceView::onImagesImported
```

### 11.3 版本历史

| 版本 | 日期 | 变更 |
|------|------|------|
| v1.0 | 2026-Q1 | 初始开发：Core + UI + AI 基础模块 |
| v1.5 | 2026-05 | 29 个按钮功能实现 + 深色主题 |
| v2.0 | 2026-05-25 | TrainingInference 训练推理模块 + 架构图 + 需求文档 |

---

> **文档维护**：本文档反映截至 2026-05-25 的最新实现状态。任何功能的新增、修改或删除都应同步更新本文档。架构图源文件 `architecture.drawio` 可在 draw.io 中打开编辑。