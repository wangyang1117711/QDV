# QDetectVision 代码 Wiki

> **文档版本**：v1.0
> **生成日期**：2026-07-04
> **项目代号**：QDetectVision（奇测视觉检测系统，简称 QDV）
> **适用代码状态**：截至 2026-07-04 的仓库快照
> **详细程度**：标准级（架构 + 模块 + 关键类函数 + 依赖 + 运行方式）

---

## 目录

1. [项目概述](#1-项目概述)
2. [系统架构](#2-系统架构)
3. [模块详解](#3-模块详解)
4. [关键类与函数说明](#4-关键类与函数说明)
5. [关键流程](#5-关键流程)
6. [依赖关系](#6-依赖关系)
7. [项目运行方式](#7-项目运行方式)
8. [设计模式与编码约定](#8-设计模式与编码约定)

---

## 1. 项目概述

### 1.1 项目定位

QDetectVision 是一款面向工业自动化场景的**智能视觉检测桌面应用程序**。系统基于 Qt6 C++ 框架构建，集成 OpenCV 计算机视觉库，为工业制造领域提供相机采集、图像处理、方案管理、检测监控等核心视觉检测能力，并配备小模型训练与快速推理模块。

- **目标平台**：Windows 10/11 x64
- **目标用户**：工业视觉工程师、质检操作员、自动化产线维护人员
- **部署环境**：工控机 / 台式工作站

### 1.2 技术栈

| 技术 | 版本 | 用途 |
|------|------|------|
| C++ | C++17（根 CMake）/ C++20（REQUIREMENTS 目标） | 主开发语言 |
| Qt | 6.11.1（mingw_64） | UI 框架、网络、SQL、信号/槽、QML |
| OpenCV | 4.13.0（MinGW 源码编译） | 图像采集、处理、计算机视觉、DNN 推理 |
| MinGW-w64 GCC | 11.2.0 | 编译工具链 |
| CMake | 3.16+ | 构建系统 |
| Python | 3.x（系统环境，可选） | 脚本引擎（QProcess 调用）+ 训练脚本 |
| QSS | — | 全局深色主题样式 |
| SQLite | Qt6::Sql 内置 | 检测结果持久化 |

### 1.3 仓库目录结构

```
QDV/
├── CMakeLists.txt                  # 根构建配置（定义 OpenCV/Qt 查找 + 子模块）
├── apps/
│   ├── SmartVision/                # ★ 主应用可执行目标（QDetectVision.exe）
│   │   ├── CMakeLists.txt          # 链接所有内部库 + windeployqt 后处理
│   │   ├── main.cpp                # ★ 真实应用入口
│   │   └── resources/              # dark_theme.qss + styles.qrc
│   └── OpenCVTest/                 # OpenCV 链接性测试程序（可选）
├── cmake/                          # CMake 配置模板（CompilerSettings/Dependencies/QDVConfig）
├── config/
│   └── operators.json              # 算子元数据外部配置（可热改，损坏回退内置）
├── docs/                           # 项目文档（需求/设计/测试报告/使用说明等）
├── include/                        # 所有公共头文件（按模块分子目录）
│   ├── AI/  Core/  Database/  Plugins/
│   ├── TrainingInference/  UI/  Vision/
│   └── Communication/
├── qml/
│   └── EditView/                   # EditView 的 QML 实现（Main.qml + 11 个组件）
│       └── ... + EditView.qrc
├── resources/                      # 全局资源 resources.qrc
├── scripts/                        # 构建/环境校验脚本（build.bat/build.sh/install_qt_env.ps1 等）
├── src/                            # 所有实现源文件（按模块分子目录，每个有 CMakeLists.txt）
│   ├── AI/  Core/  Database/  Plugins/
│   ├── TrainingInference/  UI/  Vision/  Communication/
│   └── main.cpp                    # ⚠ 遗留 QML 入口，未被构建系统引用（非真实入口）
├── tests/                          # 单元/集成测试（QDV_tests 目标 + ctest）
├── tools/                          # 辅助工具（create_user.cpp / init_user.cpp）
├── build.ps1 / build_D盘.bat       # 构建脚本
├── run_tests.ps1                   # 测试脚本
└── *.md / *.drawio                 # 根目录散落的需求/调试/架构文档
```

> **入口提示**：真实应用入口是 [apps/SmartVision/main.cpp](file:///e:/anchor/Trae/QDV/apps/SmartVision/main.cpp)；根目录 `src/main.cpp` 是早期 QML 版本入口，当前未被任何 CMakeLists 引用，属遗留文件。

---

## 2. 系统架构

### 2.1 分层架构

系统采用 5 层分层架构，自上而下依次为：

```
┌─────────────────────────────────────────────────────────────────┐
│                    用户交互层 (Entry Layer)                       │
│   apps/SmartVision/main.cpp → MainWindow → CentralWindow         │
│   CentralWindow 内 QStackedWidget 承载 8 个业务视图               │
├─────────────────────────────────────────────────────────────────┤
│                    表现层 (UI Views Layer)                        │
│   LoginView │ CameraView │ SchemeView │ EditView(全QML)          │
│   IOView │ CommView │ MonitorView │ TrainingInferenceView        │
│   EditViewBridge(C++↔QML桥) │ OperatorDescriptors │ SchemeSerializer│
├─────────────────────────────────────────────────────────────────┤
│                    业务逻辑层 (Business Logic Layer)              │
│   Core: AuthService / Logger / SchemeManager / UndoManager       │
│         Scheme / BranchNode / Result<T> / 各 Config              │
│   Vision: VisionTool基类 / 15个工具 / ToolFactory / ToolChainExecutor│
│   AI: InferenceEngine / ModelManager / TrainingBridge / Metrics  │
│   TrainingInference: ImageMgr / CategoryMgr / ExportMgr (单例三件套)│
├─────────────────────────────────────────────────────────────────┤
│                    数据与通信层 (Data & Comm Layer)               │
│   TCPCommunicator │ SerialCommunicator │ IOController            │
│   ResultDatabase │ DatabaseIntegrator │ Python QProcess           │
├─────────────────────────────────────────────────────────────────┤
│                    基础设施层 (Infrastructure Layer)              │
│   Qt6 Core/Gui/Widgets/Network/Sql/Charts/Concurrent/Test/Quick  │
│   OpenCV 4.13.0 (core/imgproc/imgcodecs/features2d/dnn)          │
│   MinGW-w64 GCC 11.2.0 │ CMake 3.x │ ONNXRuntime(可选)           │
└─────────────────────────────────────────────────────────────────┘
```

### 2.2 模块依赖关系图

```
QDetectVision.exe (apps/SmartVision)
 ├── Qt6::Core/Gui/Widgets/Network/Sql/Quick/QuickWidgets/Qml/Concurrent/Test
 ├── OpenCV (opencv_world4130.dll)
 ├── Core        (libCore)         ← 基础数据与服务层，无内部依赖
 ├── AI          (libAI)    → Core ← 推理引擎，依赖 OpenCV dnn
 ├── Vision      (libVision)→ Core ← 视觉工具，依赖 OpenCV + AI(InferenceEngine)
 ├── UI          (libUI)    → Core/Vision ← 表现层，依赖 Qt Quick/Widgets
 ├── TrainingInference → Core/AI ← 训练推理 UI，依赖 Qt Widgets
 ├── Communication      → Core  ← 通信抽象，依赖 Qt Network
 ├── Database    (libDatabase)→ Core ← SQLite 持久化
 └── Plugins     (libPlugins) → Core ← 动态插件系统
```

**模块内聚方向**：Core 是最底层基础（无内部依赖）；AI 与 Vision 平行但 Vision 的 `AiClassifyTool` 反向依赖 AI 的 `InferenceEngine`；UI 依赖几乎所有下层模块；TrainingInference 依赖 AI 与 Core。

### 2.3 构建系统

| 项 | 值 |
|----|-----|
| 构建工具 | CMake 3.16+ |
| C++ 标准 | C++17（根 CMake 设 `CXX_STANDARD 17`，部分模块 `cxx_std_17`）|
| 元对象编译 | `AUTOMOC`/`AUTORCC`/`AUTOUIC` 全开 |
| 构建生成器 | MinGW Makefiles（推荐）/ Visual Studio 17 2022（build.bat 备选）|
| 部署工具 | `windeployqt --qmldir qml/`（自动扫描 QML import 部署 Qt Quick 模块）|
| 后处理 1 | 自动复制 `libopencv_world4130.dll` 到输出目录 |
| 后处理 2 | 部署 `config/operators.json` 到 exe 同级 `config/`（不覆盖已存在）|
| 可选开关 | `ENABLE_ONNX_RUNTIME`（OFF 默认，启用后接入 ONNXRuntime 后端）|

### 2.4 应用启动流程

`apps/SmartVision/main.cpp` 的 `main()` 流程：

```
1. qputenv("QT_QUICK_CONTROLS_STYLE", "Basic")   ← 避免 Universal/Fusion 覆盖自定义色
2. QApplication 初始化（setApplicationName/Organization）
3. parseCommandLine(argc, argv)                   ← 解析 --export-operators / --auto-test / --add-op
4. 分支 1: --export-operators <path>              ← 导出算子元数据 JSON 后退出
5. 分支 2: --auto-test <png>                      ← AutoTestRunner 端到端截图后退出
6. 正常流程:
   a. applyDarkTheme()                            ← 加载 :/styles/dark_theme.qss
   b. LoggerGuard（RAII，保证 Logger::shutdown）
   c. UI::OperatorDescriptors::all()              ← 预热算子元数据，避免首次拖入卡顿
   d. MainWindow window                           ← 构造主窗口
   e. LoginIsolation::enter(window)               ← 登录门控（见下）
   f. app.exec()
```

**登录门控（LoginIsolation）**：当前版本 `kLoginEnabled=false`（登录功能被隔离），通过环境变量 `QDV_FORCE_LOGIN=1` 可强制启用。禁用时直接 `window.show()` + `window.showMain()` 绕过 LoginView；启用时走 `AuthService::isFirstRun()` + `showFirstRunSetup()` + 显示 LoginView 的标准流程。

---

## 3. 模块详解

### 3.1 Core 核心层

**职责**：提供全局基础服务（认证/日志/方案/撤销）、核心数据结构（Scheme/Result/ToolResult）、配置契约。

**关键文件**：
- [include/Core/AuthService.h](file:///e:/anchor/Trae/QDV/include/Core/AuthService.h) / [src/Core/AuthService.cpp](file:///e:/anchor/Trae/QDV/src/Core/AuthService.cpp)
- [include/Core/Logger.h](file:///e:/anchor/Trae/QDV/include/Core/Logger.h) / [src/Core/Logger.cpp](file:///e:/anchor/Trae/QDV/src/Core/Logger.cpp)
- [include/Core/SchemeManager.h](file:///e:/anchor/Trae/QDV/include/Core/SchemeManager.h) / [src/Core/SchemeManager.cpp](file:///e:/anchor/Trae/QDV/src/Core/SchemeManager.cpp)
- [include/Core/UndoManager.h](file:///e:/anchor/Trae/QDV/include/Core/UndoManager.h) / [src/Core/UndoManager.cpp](file:///e:/anchor/Trae/QDV/src/Core/UndoManager.cpp)
- [include/Core/Scheme.h](file:///e:/anchor/Trae/QDV/include/Core/Scheme.h) / [src/Core/Scheme.cpp](file:///e:/anchor/Trae/QDV/src/Core/Scheme.cpp)
- [include/Core/VisionTool.h](file:///e:/anchor/Trae/QDV/include/Core/VisionTool.h)（视觉工具基类，位于 Core 命名空间 QDV）
- [include/Core/Result.h](file:///e:/anchor/Trae/QDV/include/Core/Result.h) / [include/Core/BranchNode.h](file:///e:/anchor/Trae/QDV/include/Core/BranchNode.h)
- [include/Core/DetectionStats.h](file:///e:/anchor/Trae/QDV/include/Core/DetectionStats.h)
- [include/Core/CameraConfig.h](file:///e:/anchor/Trae/QDV/include/Core/CameraConfig.h) / [OutputConfig.h](file:///e:/anchor/Trae/QDV/include/Core/OutputConfig.h) / [TriggerConfig.h](file:///e:/anchor/Trae/QDV/include/Core/TriggerConfig.h) / [ModelBinding.h](file:///e:/anchor/Trae/QDV/include/Core/ModelBinding.h)

**核心类**：

| 类 | 性质 | 职责 |
|----|------|------|
| `AuthService` | 单例 / QObject | 用户登录/登出、Token 鉴权、失败锁定（5 次锁 15 分钟）、密码加盐哈希存储加密 |
| `QDV::Logger` | 单例 / QObject | 线程安全分级日志（Trace→Critical），100ms 缓冲刷新、50MB 滚动、7 天清理 |
| `SchemeManager` | 单例 / QObject | 方案文件加载/保存/导入导出/列举/删除，维护当前方案与缓存 |
| `UndoManager` | 单例 / QObject | 封装 `QUndoStack`，提供 `ToolAdd/Remove/MoveCommand` 三个命令类 |
| `Scheme` | 聚合根 / 非QObject | 方案数据聚合根，持有工具链、分支、相机/触发/输出/模型绑定配置，JSON 序列化 + Schema 校验 |
| `QDV::VisionTool` | 抽象基类 / 非QObject | 所有视觉工具基类，定义 `type()/configure()/execute()/serialize()` 接口，内嵌 `ToolResult` |
| `BranchNode` | 值类型 / 非QObject | 描述工具结果条件分支（源工具/操作符/阈值/True-False 分支 ID 列表）|
| `QDV::Result<T>` | 模板 / 非QObject | Rust 风格 Result 类型，封装成功值/错误信息，支持 `map`/`andThen` 链式 |
| `DetectionStats` | POD struct | 检测统计聚合（总数/合格/不合格/告警/通过率/吞吐/批次进度/状态）|
| `CameraConfig`/`TriggerConfig`/`OutputConfig`/`ModelBinding` | 配置类 | 各提供 `serialize()/deserialize()` 内联方法，值语义 |

**对外核心 API**：
- 单例入口：`AuthService::instance()`、`Logger::instance()`、`SchemeManager::instance()`、`UndoManager::instance()`
- 数据契约：`Scheme`（聚合根）、`QDV::Result<T>`（返回值契约）、`QDV::VisionTool`（执行契约）、`ToolResult`（工具输出契约）
- 配置契约：四个 Config 类的 `serialize/deserialize`

### 3.2 Vision 视觉工具层

**职责**：实现 15 种图像处理与检测工具，提供工厂注册与工具链顺序执行能力。

**关键文件**：
- [include/Vision/ToolFactory.h](file:///e:/anchor/Trae/QDV/include/Vision/ToolFactory.h) / [src/Vision/ToolFactory.cpp](file:///e:/anchor/Trae/QDV/src/Vision/ToolFactory.cpp)
- [include/Vision/ToolChainExecutor.h](file:///e:/anchor/Trae/QDV/include/Vision/ToolChainExecutor.h) / [src/Vision/ToolChainExecutor.cpp](file:///e:/anchor/Trae/QDV/src/Vision/ToolChainExecutor.cpp)
- [include/Vision/ToolChainVerifier.h](file:///e:/anchor/Trae/QDV/include/Vision/ToolChainVerifier.h) / [src/Vision/ToolChainVerifier.cpp](file:///e:/anchor/Trae/QDV/src/Vision/ToolChainVerifier.cpp)
- 15 个工具类：`ReadImageTool`/`ThresholdTool`/`BlobDetectTool`/`ColorDetectTool`/`EdgeDetectTool`/`ContourAnalyzeTool`/`GeometryMeasureTool`/`ImageArithmeticTool`/`ImageMergeTool`/`ImagePreprocessTool`/`ImageTransformTool`/`LineCircleDetectTool`/`TemplateMatchTool`/`AiClassifyTool`/`BranchControlTool`

**核心设计**：

- **VisionTool 基类**（位于 `Core/VisionTool.h`，命名空间 `QDV`）：不继承 QObject，无信号槽。唯一纯虚函数 `type()`；`configure()/execute()` 有默认空实现。构造时自动生成 UUID 作为 `m_id`。
- **ToolResult 契约**：`{bool ok, QJsonObject data, double score, qint64 elapsedMs, cv::Mat overlayImage}`。`overlayImage` 既作为可视化输出，也作为下一工具的输入图像（工具链串联机制）。
- **ToolFactory**：单例，构造函数中硬编码注册全部 15 种工具（lambda 工厂）。`createTool(type)` 返回裸指针，调用方拥有所有权。类型字符串须与 `type()` 返回值严格一致（如 `"Threshold"`）。
- **ToolChainExecutor**：继承 QObject。`execute(input)` 顺序执行工具链，工具失败仅记日志不中断；`executeAsync` 通过 `QtConcurrent::run` 异步执行；`stop()` 原子标志请求停止。提供 `toolExecuted/chainCompleted/executionProgress` 三个信号。`evaluateBranch` 支持三种比较（`==` 对 ok 布尔，`>`/`<` 对 score 浮点）。
- **ToolChainVerifier**：非 QObject，13 项工具自检（缺少 `verifyAiClassify` 和 `verifyReadImage`），用合成测试图像验证每个工具的 `configure/execute` 路径。

**15 个工具一览**：

| 工具类 | type() | 算法 | OpenCV 模块 |
|--------|--------|------|-------------|
| ReadImageTool | ReadImage | `cv::imread` | imgcodecs |
| ThresholdTool | Threshold | `cv::threshold` | imgproc |
| BlobDetectTool | BlobDetect | `SimpleBlobDetector` | features2d |
| ColorDetectTool | ColorDetect | BGR→HSV + `inRange` | imgproc |
| EdgeDetectTool | EdgeDetect | `cv::Canny` | imgproc |
| ContourAnalyzeTool | ContourAnalyze | `findContours` + 面积过滤 | imgproc |
| GeometryMeasureTool | GeometryMeasure | `minAreaRect`/`arcLength`/`minEnclosingCircle`/`moments` | imgproc |
| ImageArithmeticTool | ImageArithmetic | `add/subtract/multiply` + `bitwise_*` | core |
| ImageMergeTool | ImageMerge | ROI 拷贝 + `addWeighted` | imgproc |
| ImagePreprocessTool | ImagePreprocess | `bilateralFilter` + `morphologyEx` | imgproc |
| ImageTransformTool | ImageTransform | `resize/flip/warpAffine/warpPerspective` | imgproc |
| LineCircleDetectTool | LineCircleDetect | `HoughLines/HoughLinesP/HoughCircles` | imgproc |
| TemplateMatchTool | TemplateMatch | `cv::matchTemplate` | imgproc |
| AiClassifyTool | AiClassify | 委托 `InferenceEngine` | dnn（间接）|
| BranchControlTool | BranchControl | 透传输入，分支逻辑由 Executor + BranchNode 完成 | — |

### 3.3 AI 推理层

**职责**：封装深度学习推理后端、多模型 LRU 管理、Python 训练脚本桥接、分类评估指标计算。

**关键文件**：
- [include/AI/InferenceEngine.h](file:///e:/anchor/Trae/QDV/include/AI/InferenceEngine.h) / [src/AI/InferenceEngine.cpp](file:///e:/anchor/Trae/QDV/src/AI/InferenceEngine.cpp)
- [include/AI/ModelManager.h](file:///e:/anchor/Trae/QDV/include/AI/ModelManager.h) / [src/AI/ModelManager.cpp](file:///e:/anchor/Trae/QDV/src/AI/ModelManager.cpp)
- [include/AI/TrainingBridge.h](file:///e:/anchor/Trae/QDV/include/AI/TrainingBridge.h) / [src/AI/TrainingBridge.cpp](file:///e:/anchor/Trae/QDV/src/AI/TrainingBridge.cpp)
- [include/AI/VisionClassifier.h](file:///e:/anchor/Trae/QDV/include/AI/VisionClassifier.h)
- [include/AI/ClassificationMetrics.h](file:///e:/anchor/Trae/QDV/include/AI/ClassificationMetrics.h) / [src/AI/ClassificationMetrics.cpp](file:///e:/anchor/Trae/QDV/src/AI/ClassificationMetrics.cpp)

**核心类**：

| 类 | 性质 | 职责 |
|----|------|------|
| `InferenceEngine` | 非单例 / QObject | 推理引擎，封装 OpenCV DNN / ONNXRuntime，含 LRU 缓存（默认 50）+ 错误重试（MAX_RETRIES=3）+ 预热 |
| `ModelManager` | 单例 / QObject | 多模型 LRU 缓存管理（默认 3），默认模型目录 `E:/anchor/Trae/QDV/models`，训练后模型注册（onnx+labels 复制 + manifest 更新）|
| `QDV::TrainingBridge` | 非单例 / QObject | QProcess 调用 `training/train.py`，JSON 行协议通信，5 秒心跳 + 30 秒超时 |
| `QDV::VisionClassifier` | 非单例 / 非QObject | 高层分类封装，持有 `unique_ptr<InferenceEngine>`，提供 `classify()` TopK 接口 |
| `QDV::ClassificationMetrics` | 静态工具类 | accuracy/precision/recall/F1/混淆矩阵/macroF1/microF1 计算（STL 与 Qt 双接口）|

**关键 API**：
- `InferenceEngine::loadModel(path, inputSize, mean, scale, swapRB)` / `infer(input, output, result)` / `inferWithResult(input, imageId)` / `warmUp(iterations)`
- `ModelManager::instance()->getEngine(modelId)` / `loadDefaultModel()` / `registerTrainedModel(onnxPath, labelsPath, modelName)`
- `TrainingBridge::startTraining(manifest, outputDir, modelType, epochs, batch, lr, pythonPath)` + 4 信号（progress/completed/error/logOutput）

### 3.4 UI 表现层

**职责**：主窗口框架、8 视图导航、各业务视图、QML 桥接、算子元数据、方案序列化、撤销命令、自动测试。

**关键文件**：
- [include/UI/MainWindow.h](file:///e:/anchor/Trae/QDV/include/UI/MainWindow.h) / [src/UI/MainWindow.cpp](file:///e:/anchor/Trae/QDV/src/UI/MainWindow.cpp)
- [include/UI/CentralWindow.h](file:///e:/anchor/Trae/QDV/include/UI/CentralWindow.h) / [src/UI/CentralWindow.cpp](file:///e:/anchor/Trae/QDV/src/UI/CentralWindow.cpp)
- [include/UI/EditView.h](file:///e:/anchor/Trae/QDV/include/UI/EditView.h) / [src/UI/EditView.cpp](file:///e:/anchor/Trae/QDV/src/UI/EditView.cpp)
- [include/UI/EditViewBridge.h](file:///e:/anchor/Trae/QDV/include/UI/EditViewBridge.h) / [src/UI/EditViewBridge.cpp](file:///e:/anchor/Trae/QDV/src/UI/EditViewBridge.cpp)
- [include/UI/OperatorDescriptors.h](file:///e:/anchor/Trae/QDV/include/UI/OperatorDescriptors.h)
- [include/UI/SchemeSerializer.h](file:///e:/anchor/Trae/QDV/include/UI/SchemeSerializer.h)
- [include/UI/UndoCommands.h](file:///e:/anchor/Trae/QDV/include/UI/UndoCommands.h)
- [include/UI/AutoTestRunner.h](file:///e:/anchor/Trae/QDV/include/UI/AutoTestRunner.h)
- 其余视图：LoginView/CameraView/SchemeView/IOView/CommView/MonitorView/RenderWidget

**核心架构**：
- **MainWindow**：应用外壳，自绘标题栏（紫色 `#660874`），`QStackedWidget` 在 LoginView ↔ (空) 间切换。**重要**：CentralWindow 是**独立 top-level 窗口**而非嵌入 MainWindow（v2.1.0 M4 修复，因 CentralWindow 含多个 QQuickWidget 子视图，reparent 会触发 OpenGL 初始化崩溃）。
- **CentralWindow**：登录后显示，顶部 72px 导航条 + `QStackedWidget` 承载 8 视图。导航项硬编码，`switchView(idx)` 切换并 `emit viewChanged`。支持 Alt+1..8 快捷键 + 首页卡片点击跳转。
- **EditView**：v2.1.0 M7 起**全 QML 重构**，C++ 端仅承载单个 `QQuickWidget` 渲染 `qrc:/qml/EditView/Main.qml`，三栏布局完全由 QML 实现。
- **EditViewBridge**：C++↔QML 桥，9 个 Q_PROPERTY + 20+ public slots（节点编辑/元数据查询/收藏最近常用/图像分析）+ 13 个信号。撤销命令通过 `QUndoStack` 注入。**v2.5.0 新增**：`runScheme(inputImagePath)` 整链部署执行、`runSingleOperator(nodeId, inputImagePath)` 单算子运行（含上游链 DFS 拓扑排序）、`setCameraFrame(path)` 相机帧预留接口；内部辅助方法 `buildToolChainFromNodes`/`computeUpstreamChain`/`saveMatToTempPng` 支持节点→VisionTool 列表转换、上游链计算、输出图像临时文件持久化。
- **OperatorDescriptors**：单例算子元数据注册中心。`all()` 首次调用先尝试 `loadFromJson(config/operators.json)`，失败回退 `buildRegistry()`（内置 13 个算子）。`exportToJson(path)` 用权威内置数据导出（与代码版本强一致）。
- **SchemeSerializer**：异步 I/O（`QtConcurrent::run` + `QSaveFile` 原子写），避免大方案阻塞 UI。返回 `"OK"` 或 `"<ERR_TYPE>:<msg>"` 协议字符串。
- **UndoCommands**：6 个 `QUndoCommand` 子类（AddNode/RemoveNode/MoveNode/ConnectNodes/DisconnectNodes/PropertyChange），`MoveNodeCommand` 与 `PropertyChangeCommand` 支持 `mergeWith` 合并连续操作。
- **AutoTestRunner**：`--auto-test` 命令行端到端截图工具，通过反射式 `findChild` 定位 CentralWindow 内容栈与 EditViewBridge，支持经典/QML 双视图截图。

**8 视图导航**：

| index | 名称 | 快捷键 | 视图类 |
|-------|------|--------|--------|
| 0 | 首页 | Alt+1 | HomeView（内置，统计卡片 + 快速入口）|
| 1 | 相机 | Alt+2 | CameraView |
| 2 | 方案 | Alt+3 | SchemeView |
| 3 | 编辑 | Alt+4 | EditView（全 QML）|
| 4 | IO监控 | Alt+5 | IOView |
| 5 | 通信 | Alt+6 | CommView |
| 6 | 监控 | Alt+7 | MonitorView |
| 7 | 训练推理 | Alt+8 | TrainingInferenceView |

### 3.5 TrainingInference 训练推理层

**职责**：小模型快速训练与推理的 UI 集成层，含 14 个类、3 个单例管理器。

**关键文件**：
- [include/TrainingInference/TrainingInferenceView.h](file:///e:/anchor/Trae/QDV/include/TrainingInference/TrainingInferenceView.h) / [src/TrainingInference/TrainingInferenceView.cpp](file:///e:/anchor/Trae/QDV/src/TrainingInference/TrainingInferenceView.cpp)
- 3 个单例：[ImageManager.h](file:///e:/anchor/Trae/QDV/include/TrainingInference/ImageManager.h) / [CategoryManager.h](file:///e:/anchor/Trae/QDV/include/TrainingInference/CategoryManager.h) / [ExportManager.h](file:///e:/anchor/Trae/QDV/include/TrainingInference/ExportManager.h)
- UI 组件：ImageViewWidget / CategoryPanel / InferencePanel / ScriptEditorWidget / ResultPanel
- 对话框：PreprocessDialog / ModelLibraryDialog / TrainingProgressDialog
- 处理器：ImagePreprocessor（单例，非 QObject）

**核心类**：

| 类 | 性质 | 职责 |
|----|------|------|
| `TrainingInferenceView` | 非单例 / QWidget | 顶层整合视图，工具栏 + 三栏布局（图像列表+预览 / 类别面板+推理面板+脚本/结果堆栈 / 训练日志）|
| `ImageManager` | 单例 / QObject | 图像数据集中枢，`QMap<QString,ImageEntry>`，导入/移除/选择/标签/缩略图 |
| `CategoryManager` | 单例 / QObject | 层级类别树（最大深度 2），CRUD + JSON/CSV 导入导出 + 预设加载 + 同名兄弟校验 |
| `ExportManager` | 单例 / QObject | 推理结果导出（CSV/JSON/TXT），定义共享数据契约 `InferenceResult` |
| `ImageViewWidget` | 非单例 / QWidget | 图像查看与标注，缩放/旋转/裁剪/矩形标注（QRubberBand）|
| `CategoryPanel` | 非单例 / QWidget | 类别树面板（QTreeWidget + 搜索 + CRUD + 右键菜单）|
| `InferencePanel` | 非单例 / QWidget | 推理控制面板（模型下拉 + 运行/停止 + 进度条 + 批量模式）|
| `ScriptEditorWidget` | 非单例 / QWidget | Python 脚本编辑器，`PythonHighlighter` 语法高亮 + `QProcess` `python -c` 内联执行 |
| `ResultPanel` | 非单例 / QWidget | 推理结果表格（5 列：图像/类别/置信度/柱状图/状态）+ 导出按钮 |
| `ImagePreprocessor` | 单例 / 非QObject | 根据 `PreprocessParams` 执行 QImage 像素级处理 |

**子组件协作**：`ImageManager` 单例发信号驱动 `TrainingInferenceView::rebuildImageList()`；`InferencePanel::inferenceRequested` → `onInferenceRequested()` → 当前为**模拟实现**（QRandomGenerator 生成 0.85~1.0 置信度）；`CategoryPanel::addToCategoryRequested` → `ImageManager::setLabel()`；`TrainingBridge` 4 信号 → 日志写入 + 进度对话框 + 模型注册。

### 3.6 Communication 通信层

**职责**：TCP/串口/IO 通信抽象 + 通信自检。

**关键文件**：
- [include/Communication/TCPCommunicator.h](file:///e:/anchor/Trae/QDV/include/Communication/TCPCommunicator.h) / [src/Communication/TCPCommunicator.cpp](file:///e:/anchor/Trae/QDV/src/Communication/TCPCommunicator.cpp)
- [include/Communication/SerialCommunicator.h](file:///e:/anchor/Trae/QDV/include/Communication/SerialCommunicator.h) / [src/Communication/SerialCommunicator.cpp](file:///e:/anchor/Trae/QDV/src/Communication/SerialCommunicator.cpp)
- [include/Communication/IOController.h](file:///e:/anchor/Trae/QDV/include/Communication/IOController.h)
- [include/Communication/CommunicationVerifier.h](file:///e:/anchor/Trae/QDV/include/Communication/CommunicationVerifier.h)

**核心类**：

| 类 | 性质 | 职责 | 实现状态 |
|----|------|------|----------|
| `TCPCommunicator` | 非单例 / QObject | QTcpSocket 客户端，连接/断开/发送（字节+JSON），10MB 缓冲溢出保护 | ✅ 完整 |
| `SerialCommunicator` | 非单例 / QObject | 串口通信抽象（BaudRate/DataBits/StopBits/Parity 枚举）| ⚠️ **存根实现**，仅维护状态与缓冲，未接入 QSerialPort，`dataReceived` 信号从未发射 |
| `IOController` | 非单例 / QObject | IO 线控制（InputMode/OutputMode/PulseOutput 三模式）| ✅ 完整（模拟）|
| `CommunicationVerifier` | 非单例 / QObject | 通信自检（TCP 环回/串口枚举/IO 控制器三项）| ✅ 完整 |

### 3.7 Database 数据持久化层

**职责**：基于 SQLite 的检测结果持久化。

**关键文件**：
- [include/Database/ResultDatabase.h](file:///e:/anchor/Trae/QDV/include/Database/ResultDatabase.h) / [src/Database/ResultDatabase.cpp](file:///e:/anchor/Trae/QDV/src/Database/ResultDatabase.cpp)
- [include/Database/DatabaseIntegrator.h](file:///e:/anchor/Trae/QDV/include/Database/DatabaseIntegrator.h) / [src/Database/DatabaseIntegrator.cpp](file:///e:/anchor/Trae/QDV/src/Database/DatabaseIntegrator.cpp)

**核心类**：

| 类 | 性质 | 职责 |
|----|------|------|
| `ResultDatabase` | 单例 / QObject | SQLite 持久化，`open(dbPath="./data/qdv_results.db")` / `insertResult(...)` / `queryResults(schemeId, startTime, endTime)` / `deleteResults(...)` / `getResultCount(...)` |
| `DatabaseIntegrator` | 单例 / QObject | 数据库高层门面，`initialize()` / `shutdown()` / `saveDetectionResult(...)` 两个重载（统计聚合 / 单条）/ `loadStatsForScheme(schemeId)` |

### 3.8 Plugins 插件层

**职责**：Qt 插件式扩展系统。

**关键文件**：
- [include/Plugins/IPlugin.h](file:///e:/anchor/Trae/QDV/include/Plugins/IPlugin.h)
- [include/Plugins/PluginManager.h](file:///e:/anchor/Trae/QDV/include/Plugins/PluginManager.h) / [src/Plugins/PluginManager.cpp](file:///e:/anchor/Trae/QDV/src/Plugins/PluginManager.cpp)
- [include/Plugins/PluginVerifier.h](file:///e:/anchor/Trae/QDV/include/Plugins/PluginVerifier.h)
- [include/Plugins/TestPlugin.h](file:///e:/anchor/Trae/QDV/include/Plugins/TestPlugin.h) / [src/Plugins/TestPlugin.cpp](file:///e:/anchor/Trae/QDV/src/Plugins/TestPlugin.cpp)

**核心类**：

| 类 | 性质 | 职责 |
|----|------|------|
| `IPlugin` | 纯虚接口 | Qt 插件抽象（IID `com.qdetectvision.plugin.IPlugin`），定义 `name/version/initialize/cleanup/onSchemeLoaded/onFrameAcquired/onToolResult` |
| `PluginManager` | 单例 / QObject | 扫描 `*.dll`，通过 `createPlugin` C 符号实例化 `IPlugin*`，管理已加载插件与 QLibrary 句柄 |
| `PluginVerifier` | 非单例 / QObject | 4 项自检（创建/生命周期/执行/管理器枚举）|
| `TestPlugin` | 非单例 / QObject + IPlugin | 演示插件，导出 `extern "C" Q_DECL_EXPORT IPlugin* createPlugin()` |

> ⚠️ **已知契约不一致**：`TestPlugin.h` 声明的方法（`description()`/`bool initialize()`/`enable()`/`disable()`/`isEnabled()`）与 `IPlugin.h` 的纯虚方法（`void initialize(QWidget*)`/`onSchemeLoaded`/`onFrameAcquired`/`onToolResult`）签名不匹配。实际 `TestPlugin.cpp` 实现的是自身接口，未完整实现 IPlugin 全部纯虚方法。

---

## 4. 关键类与函数说明

### 4.1 单例清单（共 14 个）

| 单例 | 模块 | 入口方法 |
|------|------|----------|
| `AuthService` | Core | `AuthService::instance()` |
| `QDV::Logger` | Core | `Logger::instance()` / 6 个静态便捷方法（`Logger::info(...)` 等）|
| `SchemeManager` | Core | `SchemeManager::instance()` |
| `UndoManager` | Core | `UndoManager::instance()` |
| `ResultDatabase` | Database | `ResultDatabase::instance()` |
| `DatabaseIntegrator` | Database | `DatabaseIntegrator::instance()` |
| `PluginManager` | Plugins | `PluginManager::instance()` |
| `ModelManager` | AI | `ModelManager::instance()` |
| `ImageManager` | TrainingInference | `ImageManager::instance()` |
| `CategoryManager` | TrainingInference | `CategoryManager::instance()` |
| `ExportManager` | TrainingInference | `ExportManager::instance()` |
| `ImagePreprocessor` | TrainingInference | `ImagePreprocessor::instance()` |
| `ToolFactory` | Vision | `ToolFactory::instance()` |
| `UI::OperatorDescriptors` | UI | `OperatorDescriptors::all()` 等静态方法 |

### 4.2 核心数据结构

#### ToolResult（工具输出契约，定义于 `Core/VisionTool.h`）

```cpp
struct ToolResult {
    bool ok = false;              // 执行是否成功
    QJsonObject data;             // 自由格式结果数据（每个工具字段不同）
    double score = 0.0;           // 0~1 置信度或评分
    qint64 elapsedMs = 0;         // 耗时（由 Executor 填充）
    cv::Mat overlayImage;         // 可视化叠加图，同时作为下一工具输入
};
```

#### ToolType 枚举（15 种，定义于 `Core/VisionTool.h`）

`TemplateMatch / EdgeDetect / BlobDetect / ColorDetect / Threshold / ImagePreprocess / ContourAnalyze / GeometryMeasure / LineCircleDetect / ImageArithmetic / ImageTransform / ImageMerge / BranchControl / AiClassify / ReadImage`

#### InferenceResult（推理结果契约，定义于 `TrainingInference/ExportManager.h`）

```cpp
struct InferenceResult {
    QString imagePath;
    QString category;
    double confidence;
    QJsonObject rawOutput;
};
```

#### ParamSpec / OperatorMeta（算子元数据，定义于 `UI/OperatorDescriptors.h`）

- `ParamType` 枚举（7 种）：`Int | Float | Enum | Bool | String | ROI | Vector`，对应 QML ParamForm 的 7 种 Component
- `ParamSpec`：`name`/`cnName`/`type`/`defaultValue`/`minValue`/`maxValue`/`step`/`options`/`optionKeys`/`help`/`unit`
- `OperatorMeta`：`type`/`cnName`/`category`/`subGroup`/`iconPath`/`description`/`params`(QList<ParamSpec>)/`outputs`(QList<QVariantMap>)

#### Scheme 序列化 JSON 格式

```json
{
  "version": "2.1.0",
  "schemeName": "方案名",
  "savedAt": "2026-06-04T...",
  "nodes": [
    { "id": "<nodeId>", "type": "<operatorType>", "x": <double>, "y": <double>,
      "params": { "<paramName>": <QVariant>, ... } }
  ],
  "connections": [
    { "fromId": "<nodeId>", "fromPort": "<portName>",
      "toId": "<nodeId>", "toPort": "<portName>" }
  ]
}
```

### 4.3 关键公共 API 速查

#### 应用入口（apps/SmartVision/main.cpp）

```cpp
// 命令行选项
--export-operators <path>    // 导出算子元数据 JSON 后退出
--auto-test <png>            // 端到端自动化截图后退出（路径含 :edit: 为双视图模式）
--add-op <type>              // 配合 --auto-test，添加算子到画布（可多次）

// 环境变量
QDV_FORCE_LOGIN=1|true|yes   // 强制启用登录功能
QDV_FORCE_LOGIN=0|false|no   // 强制禁用登录功能
```

#### 视觉工具链

```cpp
// ToolFactory - 创建工具（裸指针，调用方拥有所有权）
QDV::VisionTool* tool = ToolFactory::instance()->createTool("Threshold");

// ToolChainExecutor - 执行工具链
executor.setTools(toolPtrs);          // NON-OWNING，所有权归 Scheme
executor.setBranches(branchMap);      // NON-OWNING
bool ok = executor.execute(input);    // 同步，始终返回 true（工具失败不中断）
QFuture<bool> f = executor.executeAsync(input);  // 异步
QMap<QString, ToolResult> results = executor.getResults();
```

#### AI 推理

```cpp
// ModelManager - 多模型管理
InferenceEngine* engine = ModelManager::instance()->getEngine(modelId);
bool loaded = ModelManager::instance()->loadDefaultModel();
bool ok = engine->infer(input, output, result);
RecognitionResult r = engine->inferWithResult(input, imageId);

// TrainingBridge - Python 训练桥接
bridge.startTraining(manifestPath, outputDir, "resnet18", 10, 8, 0.001, "python");
// 信号: trainingProgress / trainingCompleted / trainingError / logOutput
```

#### 方案管理

```cpp
// SchemeManager
SchemeManager::instance()->loadScheme(filePath);
SchemeManager::instance()->saveCurrentScheme();
Scheme* scheme = SchemeManager::instance()->currentScheme();
scheme->addTool(std::move(tool));           // 转移所有权
scheme->serialize();                        // QJsonObject
scheme->tryDeserialize(json);               // Result<void> 模式
```

#### 算子元数据

```cpp
// OperatorDescriptors - 静态接口
QList<OperatorMeta> all = UI::OperatorDescriptors::all();
QStringList cats = UI::OperatorDescriptors::categories();
OperatorMeta meta = UI::OperatorDescriptors::get("Threshold");
bool ok = UI::OperatorDescriptors::exportToJson(path);
```

#### 数据库

```cpp
// DatabaseIntegrator - 高层门面
DatabaseIntegrator::instance()->initialize("./data/qdv_results.db");
DatabaseIntegrator::instance()->saveDetectionResult(schemeId, name, stats);
DetectionStats stats = DatabaseIntegrator::instance()->loadStatsForScheme(schemeId);
```

---

## 5. 关键流程

### 5.1 应用启动流程

详见 [2.4 应用启动流程](#24-应用启动流程)。

### 5.2 方案编辑与序列化流程

```
QML 端拖入算子
  → EditViewBridge::addOperator(type, x, y)
  → push AddNodeCommand 到 QUndoStack
  → undo(): 移除节点 / redo(): 插入节点快照
  → 更新 m_currentNodes (QVariantList)
  → emit currentNodesChanged → QML 刷新画布
  → EditView 转发 emit toolCountChanged(count) → CentralWindow 更新首页统计

保存方案:
  → EditViewBridge::saveToFile(path)
  → SchemeSerializer::saveAsync(path, nodes, connections, name)
  → QtConcurrent::run → serializeToJson → QSaveFile 原子写
  → emit saveFinished(path, success, message) → QML Toast

加载方案:
  → loadAsync(path) → QFile::readAll → QJsonDocument::fromJson 校验
  → emit loadFinished(path, success, message, jsonText)
  → EditViewBridge::applyLoadedJson → 写入 m_currentNodes/m_connections
```

### 5.3 视觉工具链执行流程

```
ToolChainExecutor::execute(input)
  1. 输入校验 (input.empty() → return false)
  2. m_running.exchange(true) 原子加锁防重入
  3. 主线程调用 warn（建议用 executeAsync）
  4. 加锁拷贝 m_tools / m_branches 快照后释放锁
  5. 顺序循环:
     a. 检查 m_running（stop() 可置 false 中断）
     b. executeTool(tool, currentInput, result)  // 记录 elapsedMs
     c. 失败 → Logger::error + continue（不中断链）
     d. 成功 → m_results[tool->id()] = result; emit toolExecuted(...)
     e. 若 result.overlayImage 非空 → currentInput = overlayImage.clone()
     f. emit executionProgress(current, total)
     g. 若工具关联 BranchNode → evaluateBranch; 不满足则 break
  6. m_running = false; emit chainCompleted(true); return true
```

> ⚠️ **设计注意**：`execute()` 始终返回 true，即便工具失败或中途 stop。工具间通过 `overlayImage` 隐式传递图像上下文，无显式 schema 校验。

### 5.4 训练推理工作流

```
用户点击"启动训练"
  → TrainingInferenceView::onRunTrainingPipeline()
  1. 收集已标注图像（ImageManager.selectedPaths + labels）
  2. 校验：样本数 <5 警告确认；类别数 <2 拒绝
  3. Python 环境检查（TrainingBridge::checkPythonEnvironment）
     - 优先 E:/anchor/Trae/QDV/training/venv/Scripts/python.exe
     - 回退系统 python
  4. 训练配置对话框（modelType/epochs/batch/lr/valSplit）
  5. generateDatasetManifest → 写 JSON + UTF-8 BOM（按 valSplit 分组打乱）
  6. writeTrainingConfig → 临时 qdv_train_config_<uuid>.json
  7. m_trainingBridge->startTraining(...) → QProcess 启动 train.py --config ... --output_dir ...
  8. 弹出 TrainingProgressDialog
  9. Python stdout JSON 行协议:
     - {type:"progress", epoch, trainLoss, valLoss, ...}
     - {type:"complete", success, onnxPath, labelsPath, metrics}
     - {type:"error", ...}
  10. 训练完成 → ModelManager::registerTrainedModel(onnx, labels, name)
                    → 复制 onnx+labels 到 models/ + manifest.json + 时间戳防冲突
                 → InferencePanel::refreshModelList()

快速推理（当前为模拟）:
  → InferencePanel::inferenceRequested(modelPath, imagePaths)
  → onInferenceRequested() → QRandomGenerator 生成 0.85~1.0 置信度 + 随机类别
  → onInferenceCompleted(QList<InferenceResult>)
  → ResultPanel::setResults() + m_centerStack->setCurrentIndex(1)
```

### 5.5 关键信号槽联动

```
LoginView::loginSuccess(username)      → MainWindow::onLoginSuccess → showMain()
CentralWindow::logout                  → MainWindow::onLogout → showLogin()
CentralWindow::viewChanged(name)       → MainWindow（视图名通知）
SchemeView::schemeCountChanged(n)      → CentralWindow::updateSchemeCount
EditView::toolCountChanged(n)          → CentralWindow::updateToolCount
EditViewBridge::currentNodesChanged    → EditView::toolCountChanged（转发）
MonitorView::detectionCountChanged(n)  → CentralWindow::updateDetectionCount
MonitorView::alertCountChanged(n)      → CentralWindow::updateAlertCount
EditView::requestRunDetection          → CentralWindow::switchView(5)
ImageManager::imagesImported(n)        → TrainingInferenceView::onImagesImported
InferencePanel::inferenceRequested     → TrainingInferenceView::onInferenceRequested
CategoryPanel::addToCategoryRequested  → TrainingInferenceView::onAddToCategoryRequested
TrainingBridge::trainingProgress       → TrainingInferenceView::onTrainingProgress
TrainingBridge::trainingCompleted      → TrainingInferenceView::onTrainingCompleted
ModelManager::defaultModelLoadFailed   → InferencePanel::onDefaultModelLoadFailed
```

---

## 6. 依赖关系

### 6.1 外部依赖

| 依赖 | 版本 | 类型 | 安装方式 | 必需性 |
|------|------|------|----------|--------|
| Qt6 | 6.11.1 | 运行时 SDK | Qt 官方安装器（mingw_64）| 必需 |
| OpenCV | 4.13.0 | 编译链接 | MinGW 源码编译（`D:/opencv/build_mingw`）| 必需 |
| GCC | 11.2.0 | 编译工具链 | MinGW-w64（Qt 自带 `Tools/mingw1120_64`）| 必需 |
| CMake | 3.16+ | 构建工具 | 系统安装 | 必需 |
| ONNXRuntime | 1.17.0 | 推理后端 | 手动下载（`D:/onnxruntime-win-x64-1.17.0`）| 可选（`ENABLE_ONNX_RUNTIME` 开关）|
| Python | 3.x | 脚本引擎 | 系统安装 / 项目 venv | 可选（ScriptEditor + 训练脚本需要）|

### 6.2 Qt6 模块使用

| Qt 模块 | 用途 |
|---------|------|
| Core | 基础（QObject/QString/QJsonObject/QSettings/QMutex 等）|
| Gui | QImage/QPainter/QPixmap/QSyntaxHighlighter |
| Widgets | QMainWindow/QWidget/QStackedWidget/QTreeWidget/QTableWidget 等 |
| Network | QTcpSocket（TCPCommunicator）|
| Sql | QSqlDatabase（ResultDatabase SQLite）|
| Concurrent | QtConcurrent::run（SchemeSerializer 异步 I/O）|
| Test | 单元测试（QDV_tests）|
| Quick / QuickWidgets / Qml | QQuickWidget 承载 EditView 的 QML |

### 6.3 OpenCV 模块使用

| 模块 | 用途 |
|------|------|
| core | cv::Mat/Scalar/Point/Vec/RotatedRect/Moments/Size/Rect |
| imgproc | threshold/Canny/findContours/matchTemplate/Hough*/inRange/morphology*/warp*/add/subtract/bitwise_*/resize/flip 等 |
| imgcodecs | cv::imread（ReadImageTool/TemplateMatchTool）|
| features2d | SimpleBlobDetector（BlobDetectTool）|
| dnn | cv::dnn::Net（InferenceEngine，AI 推理后端）|

### 6.4 模块间依赖矩阵

| 模块 | 依赖（内部模块）| 依赖（外部）|
|------|------------------|-------------|
| Core | （无）| Qt Core/Gui |
| Vision | Core | OpenCV（imgproc/imgcodecs/features2d）|
| AI | Core | OpenCV（dnn）/ ONNXRuntime（可选）|
| UI | Core / Vision / TrainingInference | Qt Widgets/Quick/QuickWidgets/Qml/Concurrent + OpenCV |
| TrainingInference | Core / AI | Qt Widgets |
| Communication | Core | Qt Network |
| Database | Core | Qt Sql |
| Plugins | Core | Qt Core（QLibrary）|

---

## 7. 项目运行方式

### 7.1 环境要求

| 项 | 要求 |
|----|------|
| 操作系统 | Windows 10/11 x64 |
| Qt | 6.11.1（mingw_64），路径 `D:\Qt\6.11\6.11.1\mingw_64` |
| MinGW | 11.2.0，路径 `D:\Qt\6.11\Tools\mingw1120_64` |
| OpenCV | 4.13.0 MinGW 编译，路径 `D:\opencv\build_mingw` |
| CMake | 3.16+ |
| Python | 可选，3.x（ScriptEditor + 训练脚本需要，推荐 venv `E:/anchor/Trae/QDV/training/venv`）|

### 7.2 构建步骤（PowerShell，推荐）

使用 [build.ps1](file:///e:/anchor/Trae/QDV/build.ps1)：

```powershell
# 脚本内置路径假设：
#   Qt: D:\Qt\6.11\6.11.1\mingw_64
#   MinGW: D:\Qt\6.11\Tools\mingw1120_64
#   项目根: E:\anchor\Trae\QDV

# 1. 在项目根目录执行
cd E:\anchor\Trae\QDV
.\build.ps1
```

脚本会自动：
1. 设置环境变量（QTDIR / CMAKE_PREFIX_PATH / PATH）
2. 清理并重建 `build/` 目录
3. `cmake .. -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release`
4. `cmake --build . -j 8`
5. 后处理：windeployqt（含 QML 模块扫描）+ 复制 OpenCV DLL + 部署 operators.json

产物：`build/bin/QDetectVision.exe`

### 7.2.1 构建步骤（手动 CMake）

```powershell
# 设置环境
$env:QTDIR = "D:\Qt\6.11\6.11.1\mingw_64"
$env:CMAKE_PREFIX_PATH = $env:QTDIR
$env:PATH = "$env:QTDIR\bin;D:\Qt\6.11\Tools\mingw1120_64\bin;$env:PATH"

# 配置 + 构建
cd E:\anchor\Trae\QDV
mkdir build; cd build
cmake .. -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build . -j 8

# 可选：启用 ONNX Runtime
cmake .. -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DENABLE_ONNX_RUNTIME=ON
```

### 7.2.2 构建步骤（Visual Studio，build.bat 备选）

```cmd
scripts\build.bat
```

> 注意：build.bat 使用 `Visual Studio 17 2022` 生成器，与 MinGW ABI 不兼容，OpenCV 需用 MSVC 编译版本。**推荐统一使用 MinGW 路径**。

### 7.3 运行应用

```powershell
# 构建后直接运行
.\build\bin\QDetectVision.exe

# 强制启用登录功能
$env:QDV_FORCE_LOGIN = "1"
.\build\bin\QDetectVision.exe

# 导出算子元数据 JSON
.\build\bin\QDetectVision.exe --export-operators .\operators_export.json

# 端到端自动测试截图
.\build\bin\QDetectVision.exe --auto-test .\screenshot.png
# 双视图模式（截经典 + QML 两张图）
.\build\bin\QDetectVision.exe --auto-test .\edit:shot.png --add-op Threshold --add-op BlobDetect
```

**运行时资源依赖**（构建后自动部署到 exe 同级）：
- Qt6 运行时 DLL（windeployqt 自动复制）
- `qml/` 目录的 Qt Quick 模块（windeployqt `--qmldir` 扫描）
- `libopencv_world4130.dll`
- `config/operators.json`
- `logs/`（运行时自动创建）
- `data/qdv_results.db`（数据库，运行时自动创建）

### 7.4 运行测试

#### 单元/集成测试（CMake/ctest）

测试目标 `QDV_tests`（见 [tests/CMakeLists.txt](file:///e:/anchor/Trae/QDV/tests/CMakeLists.txt)），聚合 26 个测试源文件，覆盖全部模块，共 277 个测试用例：

```powershell
cd E:\anchor\Trae\QDV\build
cmake --build . -j 8           # 构建 QDV_tests
ctest --output-on-failure      # 运行所有测试
.\bin\QDV_tests.exe            # 直接运行测试可执行
```

测试覆盖：
- Core: test_scheme / test_auth / test_branch_node / test_result / test_logger
- Vision: test_tools / test_tool_chain
- Communication: test_tcp / test_serial / test_io
- Database: test_database / test_integrator
- AI: test_inference / test_model_manager / test_classification_metrics
- UI: test_ui_smoke / test_mainwindow_upgrade / **test_editview_bridge**（联动+复制粘贴+部署运行 26 例）/ **test_roi_validation**（ROI 校验 19 例）
- Plugins: test_plugins
- TrainingInference: test_preprocessor / test_image_manager / test_training_inference_view / test_visual_verification
- Integration: test_integration

> v2.4.1 新增：EditViewBridge 联动功能测试（selectNode/openNodeEditor 信号、addOperator 节点管理）、复制粘贴功能测试（copyNodeParams/pasteNodeParams/hasClipParams 完整生命周期）、ROI 校验模块测试（19 个用例覆盖合法格式/分隔符/空字符串/字段数/非整数/负宽高/边界条件）。

> **v2.5.0 新增**：EditViewBridge 部署与单算子运行测试（7 个用例）：
> - 边界测试：`runScheme` 空方案/无效图像路径返回失败、`runSingleOperator` 不存在节点 ID/空输入图像返回失败
> - 接口测试：`cameraFramePath` 初始为空 + `setCameraFrame` 设置生效
> - 集成测试：`runSingleOperator` 单算子端到端执行（ReadImage 读取真实 PNG 验证输出文件存在）、双节点链 `ReadImage → Threshold` 验证上游链 DFS 拓扑排序正确执行
> - 测试设计遵循 AGENTS.md 红队测试要求：每条关键路径至少 1 条对抗性测试，验证返回值结构语义而非仅 `success=true`

#### 部署验证测试（run_tests.ps1）

```powershell
.\run_tests.ps1
# 生成 test_report_<date>.txt，检查 exe 存在/Qt DLL/平台插件/应用启动/项目结构
```

#### 环境验证脚本

- [scripts/verify_env.py](file:///e:/anchor/Trae/QDV/scripts/verify_env.py) — 环境校验
- [scripts/startup_verify.py](file:///e:/anchor/Trae/QDV/scripts/startup_verify.py) / [startup_full_verify.py](file:///e:/anchor/Trae/QDV/scripts/startup_full_verify.py) — 启动校验
- [scripts/verify_development.py](file:///e:/anchor/Trae/QDV/scripts/verify_development.py) — 开发环境校验
- [tests/verify_integration.ps1](file:///e:/anchor/Trae/QDV/tests/verify_integration.ps1) — 集成验证

### 7.5 辅助工具

- [tools/create_user.cpp](file:///e:/anchor/Trae/QDV/tools/create_user.cpp) — 创建用户工具
- [tools/init_user.cpp](file:///e:/anchor/Trae/QDV/tools/init_user.cpp) — 初始化用户工具
- [apps/OpenCVTest/](file:///e:/anchor/Trae/QDV/apps/OpenCVTest) — OpenCV 链接性测试程序

---

## 8. 设计模式与编码约定

### 8.1 设计模式

| 模式 | 应用位置 |
|------|----------|
| **单例** | 14 个单例（见 4.1），均通过 `static instance()` 懒加载（OperatorDescriptors 为静态方法访问） |
| **工厂方法** | `ToolFactory` 注册 lambda 工厂创建 `VisionTool*` |
| **命令模式** | `QUndoStack` + 6 个 `QUndoCommand` 子类（UndoCommands）+ Core 的 3 个 Tool 命令 |
| **桥接** | `EditViewBridge` 连接 C++ 数据层与 QML 表现层 |
| **门面** | `DatabaseIntegrator` 门面封装 `ResultDatabase` |
| **观察者** | Qt 信号槽贯穿全项目（模块解耦通信）|
| **策略** | `InferenceEngine::setBackend`（OpenCVDNN / ONNXRuntime）|
| **模板方法** | `VisionTool` 基类定义 `configure/execute` 默认实现，子类重写 |
| **Result 类型** | Rust 风格 `QDV::Result<T>` 封装成功值/错误信息 |
| **RAII** | `LoggerGuard`（保证 Logger::shutdown）、`QSaveFile`（原子写）、`std::unique_ptr` 管理配置所有权 |

### 8.2 编码约定

- **命名空间**：`QDV` 用于 VisionTool/Logger/Result/TrainingBridge/VisionClassifier/ClassificationMetrics；`QDV::UI` 用于 OperatorDescriptors/UndoCommands；其余类位于全局命名空间。
- **头文件组织**：公共头文件统一在 `include/<模块>/`，实现在 `src/<模块>/`，每个模块独立 CMakeLists.txt 生成静态库。
- **所有权**：`std::unique_ptr` 管理 Scheme 内的工具/分支/配置所有权；`ToolChainExecutor` 的 `m_tools` 与 `m_branches` 为 NON-OWNING（所有权归 Scheme）；`ToolFactory::createTool` 返回裸指针，调用方负责释放。
- **序列化**：所有配置类统一提供 `QJsonObject serialize() const` + `void deserialize(const QJsonObject&)` 内联方法；JSON 统一 UTF-8 + Indented 格式。
- **错误处理**：推荐使用 `QDV::Result<T>` 返回值模式（如 `Scheme::tryDeserialize`）；部分老接口仍用传统 bool。
- **日志**：全模块统一使用 `QDV::Logger::info/warn/error/...` 静态方法。
- **中文编码**：源文件统一 UTF-8，禁止使用会改变编码的工具（如 PowerShell `Set-Content` 修改 C++ 源文件）。
- **QSS 主题**：全局深色主题 `:/styles/dark_theme.qss`，主色 `#660874`（紫色），背景 `#1e1e1e`。

### 8.3 已知限制与待完善项

| 项 | 现状 | 影响 |
|----|------|------|
| `SerialCommunicator` | 存根实现，已添加存根警告日志（P1-C8）| 串口通信功能不可用，运行时输出警告 |
| `TrainingInferenceView::onInferenceRequested` | 模拟实现（QRandomGenerator）| 推理结果非真实模型输出 |
| `ToolChainExecutor::execute` | 始终返回 true，工具失败不中断 | 与"VERDICT: FAIL 阻断"原则不符 |
| `ToolChainVerifier` | 缺 `verifyAiClassify`/`verifyReadImage` | AI 推理与读图路径无自检 |
| ~~`TestPlugin` vs `IPlugin`~~ | ✅ P1-C9+C12 已修复：接口契约对齐，IPlugin 继承 QObject | 插件接口统一 |
| ~~`src/main.cpp`~~ | ✅ P1-C10 已修复：删除遗留 QML 入口死代码 | 死代码已清理 |
| ~~逐像素 pixelColor 性能~~ | ✅ P1-C11 已修复：ImagePreprocessor 改用 scanLine 行指针 | 图像处理性能提升 5-10x |
| ~~视图切换无动画~~ | ✅ P1-C14 已修复：CentralWindow 添加 200ms 滑动动画 | 交互体验改善 |
| ~~SQL 错误处理不完整~~ | ✅ P1-C15+C16 已修复：ResultDatabase/PluginManager 添加错误日志 | 错误可追溯 |
| ~~Token/密码时序攻击~~ | ✅ P1-C17 已修复：AuthService 改用常量时间比较 | 安全性增强 |
| ~~EditView 无快捷键~~ | ✅ P1-C13 已修复：Main.qml 添加 Ctrl+S/O/N/F2 等 | 操作效率提升 |
| 真实 ONNX 模型推理 | 接口层已完成，待接入 | M7 里程碑 |
| Vision 工具链完整集成 | 工具已实现，Executor 未接入主流程 | M8 里程碑 |
| Plugins 系统 | 接口已定义，未实际加载 | M9 里程碑 |
| 打包发布 | 未实现 | M10 里程碑 |

### 8.4 关键风险规避（来自 REQUIREMENTS.md）

| 风险 | 规避措施 |
|------|----------|
| PowerShell 编码损坏源文件 | 禁止 PowerShell `Set-Content` 修改 C++ 源文件，统一用 IDE 工具 |
| MOC 未生成 | CMakeLists 显式 glob 头文件确保 AUTOMOC 扫描 |
| 模块未集成 | 新模块须同步更新 CentralWindow 导航与视图栈 |
| OpenCV 编译兼容性 | `link_directories()` + 后处理 DLL 复制 |
| 中文编码乱码 | 源文件统一 UTF-8 |

---

## 附录：关键文件索引

### 入口与构建
- [apps/SmartVision/main.cpp](file:///e:/anchor/Trae/QDV/apps/SmartVision/main.cpp) — 真实应用入口
- [apps/SmartVision/CMakeLists.txt](file:///e:/anchor/Trae/QDV/apps/SmartVision/CMakeLists.txt) — 可执行目标配置
- [CMakeLists.txt](file:///e:/anchor/Trae/QDV/CMakeLists.txt) — 根构建配置
- [build.ps1](file:///e:/anchor/Trae/QDV/build.ps1) — PowerShell 构建脚本
- [config/operators.json](file:///e:/anchor/Trae/QDV/config/operators.json) — 算子元数据外部配置

### Core 模块
- [include/Core/VisionTool.h](file:///e:/anchor/Trae/QDV/include/Core/VisionTool.h) — 视觉工具基类 + ToolResult
- [include/Core/Scheme.h](file:///e:/anchor/Trae/QDV/include/Core/Scheme.h) — 方案聚合根
- [include/Core/Result.h](file:///e:/anchor/Trae/QDV/include/Core/Result.h) — Result<T> 模板
- [include/Core/AuthService.h](file:///e:/anchor/Trae/QDV/include/Core/AuthService.h) — 认证服务单例
- [include/Core/Logger.h](file:///e:/anchor/Trae/QDV/include/Core/Logger.h) — 日志系统单例

### Vision 模块
- [include/Vision/ToolFactory.h](file:///e:/anchor/Trae/QDV/include/Vision/ToolFactory.h) — 工具工厂单例
- [include/Vision/ToolChainExecutor.h](file:///e:/anchor/Trae/QDV/include/Vision/ToolChainExecutor.h) — 工具链执行器

### UI 模块
- [include/UI/MainWindow.h](file:///e:/anchor/Trae/QDV/include/UI/MainWindow.h) — 主窗口
- [include/UI/CentralWindow.h](file:///e:/anchor/Trae/QDV/include/UI/CentralWindow.h) — 中央导航
- [include/UI/EditViewBridge.h](file:///e:/anchor/Trae/QDV/include/UI/EditViewBridge.h) — C++↔QML 桥
- [include/UI/OperatorDescriptors.h](file:///e:/anchor/Trae/QDV/include/UI/OperatorDescriptors.h) — 算子元数据
- [include/UI/SchemeSerializer.h](file:///e:/anchor/Trae/QDV/include/UI/SchemeSerializer.h) — 方案异步序列化
- [qml/EditView/Main.qml](file:///e:/anchor/Trae/QDV/qml/EditView/Main.qml) — EditView QML 主文件

### AI 模块
- [include/AI/InferenceEngine.h](file:///e:/anchor/Trae/QDV/include/AI/InferenceEngine.h) — 推理引擎
- [include/AI/ModelManager.h](file:///e:/anchor/Trae/QDV/include/AI/ModelManager.h) — 模型管理单例
- [include/AI/TrainingBridge.h](file:///e:/anchor/Trae/QDV/include/AI/TrainingBridge.h) — Python 训练桥接

### TrainingInference 模块
- [include/TrainingInference/TrainingInferenceView.h](file:///e:/anchor/Trae/QDV/include/TrainingInference/TrainingInferenceView.h) — 训练推理主视图
- [include/TrainingInference/ExportManager.h](file:///e:/anchor/Trae/QDV/include/TrainingInference/ExportManager.h) — 定义 InferenceResult 共享契约

### 测试
- [tests/CMakeLists.txt](file:///e:/anchor/Trae/QDV/tests/CMakeLists.txt) — 测试目标配置

---

> **文档维护**：本文档基于 2026-07-05 仓库快照生成，反映当前实现状态（含 P1-A/B/C 修复、v2.4.1 单元测试扩充、v2.5.0 编辑模块五大功能开发：连接线删除交互/Halcon 风格图像预览/整链部署/单算子运行/上游链拓扑排序）。功能新增/修改/删除后应同步更新本文档与 [docs/REQUIREMENTS.md](file:///e:/anchor/Trae/QDV/docs/REQUIREMENTS.md)。
