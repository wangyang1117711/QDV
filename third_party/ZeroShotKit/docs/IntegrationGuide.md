# ZeroShotKit 集成指南

本文档介绍如何将 ZeroShotKit 集成到主项目中，包括依赖项说明、三种集成方式、Core-only 与 Core+UI 配置、日志适配、中文路径处理、线程模型与常见问题。

## 目录

- [依赖项详细说明](#依赖项详细说明)
- [集成方式一：add_subdirectory（源码集成，推荐）](#集成方式一add_subdirectory源码集成推荐)
- [集成方式二：find_package（预编译安装，预留）](#集成方式二find_package预编译安装预留)
- [集成方式三：直接复制源码（不推荐）](#集成方式三直接复制源码不推荐)
- [Core-only 集成](#core-only-集成)
- [Core+UI 集成](#coreui-集成)
- [日志适配](#日志适配)
- [中文路径处理说明](#中文路径处理说明)
- [线程模型说明](#线程模型说明)
- [常见问题（FAQ）](#常见问题faq)

---

## 依赖项详细说明

### Qt6

- 最小版本：6.2
- 必需组件：`Core`、`Concurrent`
- UI 层额外需要：`Widgets`
- 用途：基础数据类型（QString/QList/QJsonObject）、事件循环、异步推理（`QtConcurrent::run`）、信号槽（异步结果回调）、UI 控件

CMake 查找：

```cmake
find_package(Qt6 REQUIRED COMPONENTS Core Concurrent Widgets)
```

### OpenCV

- 最小版本：4.5
- 用途：图像读写（`cv::imread` / `cv::imdecode`）、图像预处理（`cv::resize` / `cv::cvtColor` / `cv::blobFromImage`）、`cv::dnn` 后端（用于对比基准）、二值掩码处理、热力图伪彩色

CMake 查找：

```cmake
find_package(OpenCV REQUIRED)
```

### ONNX Runtime

- 推荐版本：1.14+
- 用途：ONNX 模型推理后端，由 `ZEROSHOTKIT_ENABLE_ORT` 选项控制是否启用
- 编译时定义宏：`ZSU_HAS_ORT`（启用后由 CMake 自动添加到 `ZeroShotKitCore` 的 `PUBLIC` 编译定义）

CMake 查找顺序（详见顶层 `CMakeLists.txt`）：

1. 若主项目已设置 `ONNXRUNTIME_INCLUDE_DIRS` 与 `ONNXRUNTIME_LIBS` 变量，直接复用。
2. 否则按以下路径顺序查找：
   - `$ENV{ONNXRUNTIME_ROOT}/include` 与 `$ENV{ONNXRUNTIME_ROOT}/lib`
   - `C:/onnxruntime/include` 与 `C:/onnxruntime/lib`
   - `D:/onnxruntime/include` 与 `D:/onnxruntime/lib`
3. 找到则设置 `ZSU_HAS_ORT=TRUE`，否则输出警告并禁用 ORT 支持。

### 编译器要求

- C++ 标准：C++17（`CMAKE_CXX_STANDARD 17`，`CMAKE_CXX_STANDARD_REQUIRED ON`）
- MSVC：2019 16.11+（建议 2022）
- MinGW GCC：9+（ORT C++ 头文件需要 `-fpermissive`，已由 CMake 自动配置）
- Clang：10+
- 必须启用 `CMAKE_AUTOMOC`（ZeroShotKit 类使用 `Q_OBJECT` 宏）

---

## 集成方式一：add_subdirectory（源码集成，推荐）

将 ZeroShotKit 作为子目录添加到主项目 CMake 中。这是当前最完整的集成方式。

### 完整 CMake 示例

主项目 `CMakeLists.txt`：

```cmake
cmake_minimum_required(VERSION 3.16)
project(MyApp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# 查找 Qt6
find_package(Qt6 REQUIRED COMPONENTS Core Concurrent Widgets)

# 查找 OpenCV
find_package(OpenCV REQUIRED)

# 配置 ONNX Runtime 路径（二选一）
# 方式 A：使用环境变量
# set(ENV{ONNXRUNTIME_ROOT} "D:/onnxruntime")
# 方式 B：直接设置 CMake 变量（优先级高于环境变量）
# set(ONNXRUNTIME_INCLUDE_DIRS "D:/onnxruntime/include")
# set(ONNXRUNTIME_LIBS "D:/onnxruntime/lib/onnxruntime.lib")

# 添加 ZeroShotKit 子目录
add_subdirectory(third_party/ZeroShotKit)

# 主项目目标
add_executable(MyApp src/main.cpp)
target_link_libraries(MyApp PRIVATE
    Qt6::Core
    Qt6::Concurrent
    Qt6::Widgets
    ${OpenCV_LIBS}
    ZeroShotKit::Core
    # ZeroShotKit::UI    # 如需 UI 面板，取消注释
)
```

### 选项说明

ZeroShotKit 提供三个 CMake 选项，可在 `add_subdirectory` 之前设置：

```cmake
# 在 add_subdirectory(third_party/ZeroShotKit) 之前设置
set(ZEROSHOTKIT_BUILD_UI    ON  CACHE BOOL "" FORCE)  # 编译 UI 层（默认 ON）
set(ZEROSHOTKIT_BUILD_TESTS OFF CACHE BOOL "" FORCE)  # 编译测试（默认 OFF）
set(ZEROSHOTKIT_ENABLE_ORT  ON  CACHE BOOL "" FORCE)  # 启用 ONNX Runtime（默认 ON）
```

| 选项 | 默认值 | 说明 |
| --- | --- | --- |
| `ZEROSHOTKIT_BUILD_UI` | `ON` | 是否编译 UI 层（`ZeroShotPanel` / `ZeroShotResultPanel` / `ModelNotesDialog`） |
| `ZEROSHOTKIT_BUILD_TESTS` | `OFF` | 是否编译测试（预留目录，当前未启用） |
| `ZEROSHOTKIT_ENABLE_ORT` | `ON` | 是否启用 ONNX Runtime 后端，关闭后仅可用 OpenCV DNN |

### ONNX Runtime 路径配置

ZeroShotKit 按以下优先级解析 ONNX Runtime 路径：

1. **主项目预设变量**：若主项目在 `add_subdirectory` 之前设置了 `ONNXRUNTIME_INCLUDE_DIRS` 与 `ONNXRUNTIME_LIBS`，ZeroShotKit 直接复用，不进行查找。这是主项目集成时的推荐方式。

   ```cmake
   set(ONNXRUNTIME_INCLUDE_DIRS "D:/onnxruntime/include")
   set(ONNXRUNTIME_LIBS "D:/onnxruntime/lib/onnxruntime.lib")
   add_subdirectory(third_party/ZeroShotKit)
   ```

2. **环境变量**：设置 `ONNXRUNTIME_ROOT` 环境变量指向 ONNX Runtime 安装根目录。

   ```bash
   # Windows PowerShell
   $env:ONNXRUNTIME_ROOT = "D:\onnxruntime"

   # Windows CMD
   set ONNXRUNTIME_ROOT=D:\onnxruntime
   ```

   ZeroShotKit 会查找 `$ENV{ONNXRUNTIME_ROOT}/include` 与 `$ENV{ONNXRUNTIME_ROOT}/lib`。

3. **默认路径**：若上述两种方式均未配置，ZeroShotKit 会尝试以下默认路径：
   - `C:/onnxruntime/include` 与 `C:/onnxruntime/lib`
   - `D:/onnxruntime/include` 与 `D:/onnxruntime/lib`

   建议将 ONNX Runtime 解压到这两个默认路径之一以简化配置（按主项目规范，优先使用 `D:/onnxruntime` 以减少 C 盘占用）。

---

## 集成方式二：find_package（预编译安装，预留）

未来版本将提供 `ZeroShotKitConfig.cmake` 安装包，支持通过 `find_package` 集成。

```cmake
find_package(ZeroShotKit 1.0 REQUIRED COMPONENTS Core UI)
target_link_libraries(MyApp PRIVATE ZeroShotKit::Core ZeroShotKit::UI)
```

> 当前版本未实现 `install()` 规则与 `ZeroShotKitConfig.cmake` 生成，集成方请使用 `add_subdirectory` 方式。预留此章节以便未来补充。

---

## 集成方式三：直接复制源码（不推荐）

将 `include/ZeroShotKit/` 与 `src/` 下的源文件直接复制到主项目中编译。这种方式不推荐，仅在没有 CMake 或无法使用 `add_subdirectory` 时作为最后手段。

注意事项：

1. **ORT 宏定义**：需手动在编译选项中添加 `ZSU_HAS_ORT` 宏定义，并手动配置 ONNX Runtime 的 include 与链接库。

   ```cmake
   target_compile_definitions(MyApp PRIVATE ZSU_HAS_ORT)
   target_include_directories(MyApp PRIVATE D:/onnxruntime/include)
   target_link_libraries(MyApp PRIVATE D:/onnxruntime/lib/onnxruntime.lib)
   ```

2. **Qt MOC**：必须启用 `CMAKE_AUTOMOC ON`，因为所有 Core/UI 类都使用 `Q_OBJECT` 宏。

3. **include 路径**：需手动添加 `ZeroShotKit/include` 与 `ZeroShotKit/src` 两个 include 路径。

   ```cmake
   target_include_directories(MyApp PRIVATE
       third_party/ZeroShotKit/include
       third_party/ZeroShotKit/src
   )
   ```

4. **失去别名目标**：不会获得 `ZeroShotKit::Core` / `ZeroShotKit::UI` 别名目标，所有源文件需直接添加到主项目目标。

5. **升级困难**：每次升级需手动覆盖源文件，无法通过 Git submodule 或 `add_subdirectory` 自动同步。

6. **MinGW 兼容性**：MinGW GCC 下需手动添加 `-fpermissive` 编译选项（ORT C++ 头文件的类型隐式转换需要）。

   ```cmake
   target_compile_options(MyApp PRIVATE -fpermissive)
   ```

---

## Core-only 集成

当目标场景无需 UI（如服务端推理、边缘端部署、CI 评估），可只集成 Core 层以减少依赖。

### CMake 配置

```cmake
set(ZEROSHOTKIT_BUILD_UI OFF CACHE BOOL "" FORCE)  # 禁用 UI 层
add_subdirectory(third_party/ZeroShotKit)

target_link_libraries(MyApp PRIVATE
    Qt6::Core
    Qt6::Concurrent
    ${OpenCV_LIBS}
    ZeroShotKit::Core     # 仅链接 Core
)
```

### 特点

- 不依赖 `Qt6::Widgets`，减少二进制体积。
- 无法使用 `ZeroShotPanel` / `ZeroShotResultPanel` / `ModelNotesDialog`。
- 仍可通过 `Kit` / `ZeroShotEngine` / `PipelineEngine` / `EvaluationEngine` 等 Core 层 API 完成全部推理与评估功能。

适用场景：

- 服务端批处理推理
- CI/CD 评估流水线
- 边缘端无 GUI 部署
- 单元测试

---

## Core+UI 集成

当目标场景需要可视化配置与结果展示时，集成 Core + UI 层。

### CMake 配置

```cmake
set(ZEROSHOTKIT_BUILD_UI ON CACHE BOOL "" FORCE)  # 启用 UI 层（默认）
add_subdirectory(third_party/ZeroShotKit)

target_link_libraries(MyApp PRIVATE
    Qt6::Core
    Qt6::Concurrent
    Qt6::Widgets          # UI 层需要 Widgets
    ${OpenCV_LIBS}
    ZeroShotKit::Core
    ZeroShotKit::UI       # 链接 UI 层
)
```

### 使用示例

```cpp
#include <QMainWindow>
#include "ZeroShotKit/ZeroShotKit.h"
#include "ZeroShotPanel.h"
#include "ZeroShotResultPanel.h"

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(QWidget* parent = nullptr) : QMainWindow(parent) {
        zsu::Kit* kit = new zsu::Kit(this);

        QDVMini::ZeroShotPanel* panel = new QDVMini::ZeroShotPanel(this);
        QDVMini::ZeroShotResultPanel* resultPanel = new QDVMini::ZeroShotResultPanel(this);

        // 注入引擎到面板
        panel->setEngine(kit->engine());
        panel->setModelNotesManager(kit->notesManager());
        panel->setBadCaseRecorder(kit->badCaseRecorder());

        // 布局
        QSplitter* splitter = new QSplitter(this);
        splitter->addWidget(panel);
        splitter->addWidget(resultPanel);
        setCentralWidget(splitter);
    }
};
```

详细示例参见 [Examples.md](Examples.md) 中的「UI 集成」一节。

---

## 日志适配

ZeroShotKit 默认通过 `ZSU_LOG_INFO` / `ZSU_LOG_WARN` / `ZSU_LOG_ERROR` / `ZSU_LOG_DEBUG` 四个宏输出日志，默认实现是 `std::printf` 输出到 stdout（详见 `include/ZeroShotKit/Logger.h`）。

集成方可通过 CMake `target_compile_definitions` 重定向这四个宏到自有日志系统。

### 重定向示例

假设主项目有日志类：

```cpp
// MyLogger.h
class MyLogger {
public:
    static void info(const QString& msg);
    static void warn(const QString& msg);
    static void error(const QString& msg);
    static void debug(const QString& msg);
};
```

在主项目 CMake 中重定向：

```cmake
target_compile_definitions(MyApp PRIVATE
    "ZSU_LOG_INFO(msg)=MyLogger::info(msg)"
    "ZSU_LOG_WARN(msg)=MyLogger::warn(msg)"
    "ZSU_LOG_ERROR(msg)=MyLogger::error(msg)"
    "ZSU_LOG_DEBUG(msg)=MyLogger::debug(msg)"
)
```

注意事项：

- 宏参数 `msg` 可以是 `QString` 或 `const char*` 字符串字面量。ZeroShotKit 内部用 `QString(msg)` 构造临时对象，兼容两种类型，自有日志函数应接受 `QString` 参数。
- 若希望完全静默 ZeroShotKit 日志，可将宏重定向到空操作：

  ```cmake
  target_compile_definitions(MyApp PRIVATE
      "ZSU_LOG_INFO(msg)=((void)0)"
      "ZSU_LOG_WARN(msg)=((void)0)"
      "ZSU_LOG_ERROR(msg)=((void)0)"
      "ZSU_LOG_DEBUG(msg)=((void)0)"
  )
  ```

- 由于 `ZeroShotKitCore` 在 `PUBLIC` 接口中使用这些宏，定义需要加在链接 `ZeroShotKit::Core` 的目标上，否则在包含 ZeroShotKit 头文件时宏仍为默认实现。

---

## 中文路径处理说明

OpenCV 的 `cv::imread` 在 Windows 下不支持包含中文的文件路径，会返回空 `cv::Mat`。ZeroShotKit 在 `Kit::readImage` 中通过 `QFile` + `cv::imdecode` 规避此问题。

### 实现原理

`Kit::readImage` 实现位于 `src/ZeroShotKit.cpp`：

```cpp
cv::Mat Kit::readImage(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        ZSU_LOG_ERROR(QString("Kit: 无法打开图片文件: %1").arg(path));
        return cv::Mat();
    }
    QByteArray data = file.readAll();
    file.close();

    cv::Mat img = cv::imdecode(std::vector<uchar>(data.begin(), data.end()), cv::IMREAD_COLOR);
    if (img.empty()) {
        ZSU_LOG_WARN(QString("Kit: 图片解码失败: %1").arg(path));
    }
    return img;
}
```

- 使用 `QFile` 以字节流方式读取文件内容（`QFile` 内部使用宽字符 API，支持中文路径）。
- 使用 `cv::imdecode` 从内存字节流解码图像，绕过 `cv::imread` 的路径限制。

### 注意事项

- `Kit::readImage` 是 `private static` 方法，仅供 `Kit::inferAsync` / `Kit::inferBatchAsync` 内部使用，外部无法直接调用。
- 同步推理 `Kit::infer(const cv::Mat&)` 接受的是 `cv::Mat`，调用方需自行读取图像。若路径含中文，请参考 `Kit::readImage` 的实现方式读取。
- 推荐封装一个工具函数：

  ```cpp
  cv::Mat readImageChineseSafe(const QString& path) {
      QFile file(path);
      if (!file.open(QIODevice::ReadOnly)) return cv::Mat();
      QByteArray data = file.readAll();
      return cv::imdecode(std::vector<uchar>(data.begin(), data.end()), cv::IMREAD_COLOR);
  }
  ```

---

## 线程模型说明

ZeroShotKit 的异步推理基于 `QtConcurrent::run` + `QFutureWatcher` 实现。

### 异步推理执行流程

以 `Kit::inferAsync` 为例（位于 `src/ZeroShotKit.cpp`）：

1. **主线程**调用 `Kit::inferAsync(imagePath)`，重置取消标志位。
2. 通过 `QtConcurrent::run` 在工作线程执行 lambda：
   - 工作线程读取图像（`readImage`）。
   - 工作线程调用 `Kit::infer(img)` 执行推理。
3. 工作线程返回 `ZeroShotResult`，由 `QFutureWatcher` 监听。
4. `QFutureWatcher::finished` 信号触发主线程的 lambda，发射 `inferenceCompleted` 信号。

### 信号发射线程

- `inferenceCompleted` / `batchCompleted` 信号在主线程发射（通过 `QFutureWatcher::finished` 转发）。
- `progressUpdated` 信号在主线程发射（通过 `QMetaObject::invokeMethod` + `Qt::QueuedConnection` 从工作线程投递到主线程）。
- `errorOccurred` 信号在主线程发射（同样通过 `QMetaObject::invokeMethod` 投递）。

因此，连接这些信号的槽函数可以安全地更新 UI，无需额外线程切换。

### 线程安全注意事项

- `Kit::infer` 与 `Kit::inferBatch` 是同步方法，调用方需自行保证不同时调用（同一 `Kit` 实例不可在多线程同时推理）。
- `Kit::cancel` 是线程安全的，使用 `std::atomic<bool>` 标志位。
- `ORTInferenceEngine::cancelInference` 同样使用原子操作，线程安全。
- 异步推理过程中不应同时调用同步推理方法，否则可能导致 ORT 会话状态不一致。
- `PipelineEngine` 的并行模式（`setParallelMode(true)`）使用 `QtConcurrent` 并行执行阶段，但同一 `ZeroShotEngine` 实例仍是单线程使用，绑定不同引擎实例才能实现真正的阶段并行。

### 事件循环要求

- 异步推理依赖 Qt 事件循环，调用方必须运行 `QCoreApplication::exec()` 或 `QEventLoop`。
- 在控制台应用中需使用 `QCoreApplication`；在 GUI 应用中使用 `QApplication`。
- 若不运行事件循环，`QFutureWatcher::finished` 信号不会被处理，`inferenceCompleted` 永远不会发射。

---

## 常见问题（FAQ）

### 模型加载失败怎么办

**症状**：`Kit::loadModel` 返回 `false`，或 `ORTInferenceEngine::errorState()` 返回非 `NoError` 值。

**排查步骤**：

1. 检查模型文件路径是否存在，路径分隔符在 Windows 下建议使用 `/` 或转义 `\\`。
2. 检查 `ZeroShotModelType` 与模型文件是否匹配。AnomalyCLIP 的 `modelPath` 应指向 `clip_vision_vit_b32.onnx` 所在目录，而非文件本身。
3. 通过 `kit.engine()` 获取 `ZeroShotEngine` 内部的 `ORTInferenceEngine`，调用 `errorState()` 与 `lastError()` 查看具体错误。
4. 若 `errorState()` 返回 `ModelForwardTestFailed`，可能是模型与 ORT 版本不兼容，尝试 `loadModel` 时设置 `skipForwardTest=true`。
5. 检查 ONNX Runtime 与模型 opset 版本是否匹配。
6. 中文路径问题：若路径包含中文，确认使用 `QFile` 方式读取而非 `cv::imread`。

### ORT 找不到怎么办

**症状**：CMake 配置阶段输出警告 `ZeroShotKit: ONNX Runtime not found, building without ORT support`，或编译时 `ZSU_HAS_ORT` 未定义。

**解决方案**：

1. 确认 `ZEROSHOTKIT_ENABLE_ORT` 选项为 `ON`（默认值）。
2. 设置 `ONNXRUNTIME_ROOT` 环境变量指向 ONNX Runtime 安装根目录：

   ```bash
   set ONNXRUNTIME_ROOT=D:\onnxruntime
   ```

3. 或在主项目 CMake 中直接设置变量：

   ```cmake
   set(ONNXRUNTIME_INCLUDE_DIRS "D:/onnxruntime/include")
   set(ONNXRUNTIME_LIBS "D:/onnxruntime/lib/onnxruntime.lib")
   add_subdirectory(third_party/ZeroShotKit)
   ```

4. 或将 ONNX Runtime 解压到默认路径 `C:/onnxruntime` 或 `D:/onnxruntime`。
5. 确认 `onnxruntime_cxx_api.h` 存在于 `<root>/include/onnxruntime/` 下。
6. 确认 `onnxruntime.lib`（Windows）或 `libonnxruntime.so`（Linux）存在于 `<root>/lib/` 下。
7. 运行时需将 ONNX Runtime 的动态库目录加入 `PATH` 环境变量或部署目录。

### 中文路径报错怎么办

**症状**：图像读取失败，`cv::Mat::empty()` 返回 `true`，但文件确实存在。

**原因**：`cv::imread` 在 Windows 下不支持中文路径。

**解决方案**：

1. 异步推理使用 `Kit::inferAsync(QString)`，内部已通过 `QFile` + `cv::imdecode` 规避中文路径问题。
2. 同步推理使用 `cv::Mat` 入参，需调用方自行读取图像。请使用以下方式读取：

   ```cpp
   cv::Mat readImageChineseSafe(const QString& path) {
       QFile file(path);
       if (!file.open(QIODevice::ReadOnly)) return cv::Mat();
       QByteArray data = file.readAll();
       return cv::imdecode(std::vector<uchar>(data.begin(), data.end()), cv::IMREAD_COLOR);
   }
   ```

3. 模型文件路径也应避免中文，ORT 在某些情况下对中文路径支持不完善。

### 内存占用高怎么办

**症状**：长时间批量推理后内存持续增长。

**排查与优化**：

1. **PatchCore memory bank 过大**：`addNormalSample` 会将特征向量缓存到 memory bank，样本越多内存占用越高。可通过 `clearNormalSamples()` 清空，或控制添加样本数。
2. **OpenCV Mat 引用计数**：`ZeroShotResult::mask` / `anomalyMap` 持有 `cv::Mat`，确保结果使用完毕后及时释放（离开作用域或调用 `cv::Mat::release()`）。
3. **批量推理结果列表**：`Kit::inferBatch` 返回 `QList<ZeroShotResult>`，大批量场景下应分批处理，避免一次性持有所有结果。
4. **ORT 内存竞技场**：若内存紧张，可禁用 `ORTSessionConfig::enableCpuMemArena` 与 `enableMemPattern`，但会牺牲推理性能。
5. **ORT 线程数**：`ORTSessionConfig::intraOpNumThreads` 与 `interOpNumThreads` 过高会占用更多内存，按 CPU 核数合理配置。
6. **模型量化**：启用量化模型（`setUseQuantizedModel(true)`）可显著降低内存占用，详见下一问。

### 如何启用量化模型

**步骤**：

1. 准备量化后的 ONNX 模型文件，命名为 `<原名>_int8.onnx`，与原始模型放在同一目录。例如 `clip_vision_vit_b32.onnx` 的量化版本为 `clip_vision_vit_b32_int8.onnx`。
2. 在加载模型前启用量化：

   ```cpp
   kit.engine()->setUseQuantizedModel(true);
   kit.loadModel(zsu::ZeroShotModelType::AnomalyCLIP, "D:/models/anomaly_clip");
   ```

3. `ZeroShotEngine` 会自动将原始路径 `clip_vision_vit_b32.onnx` 推导为 `clip_vision_vit_b32_int8.onnx`，并优先加载量化版本。
4. 若量化模型文件不存在，会自动回退到原始模型，并通过 `lastLoadUsedQuantized()` 返回 `false`。

   ```cpp
   if (!kit.engine()->lastLoadUsedQuantized()) {
       qWarning() << "量化模型未找到，已回退到原始模型";
   }
   ```

5. 也可通过 `ORTSessionConfig::enableInt8Quantization` 与 `quantizationModelPath` 显式指定量化模型路径。

### 如何禁用 UI 编译

**方法**：在 `add_subdirectory` 之前设置 `ZEROSHOTKIT_BUILD_UI=OFF`。

```cmake
set(ZEROSHOTKIT_BUILD_UI OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/ZeroShotKit)
```

禁用后：

- 不会查找 `Qt6::Widgets`。
- 不会编译 `ui/` 目录下的源文件。
- 不会生成 `ZeroShotKit::UI` 别名目标。
- 主项目链接 `ZeroShotKit::UI` 会报错，应仅链接 `ZeroShotKit::Core`。

适用场景：

- 服务端推理、CI 评估、无 GUI 部署环境。
- 主项目不使用 Qt Widgets，仅使用 Qt Core + Concurrent。
- 减少 UI 相关代码的编译时间。
