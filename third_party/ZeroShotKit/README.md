# ZeroShotKit

## 模块概述

ZeroShotKit 是一个面向工业质检场景的零样本功能模块化库，从主项目 QDVMini 中抽取而来。它将零样本推理能力封装为独立的、可复用的 C++ 静态库，提供 AnomalyCLIP / Grounding DINO / MobileSAM / PatchCore 等多种零样本模型统一调用接口，并支持多阶段流水线推理、批量推理、异步推理、评估与 Bad Case 记录等完整能力。整个模块分为 Core 层（纯推理，仅依赖 Qt6 Core / OpenCV / ONNX Runtime）和 UI 层（可选的 Qt Widgets 配置与结果展示面板），便于在无 GUI 的服务端或边缘端复用。

## 功能特性

### Core 层组件（7 个）

- `Kit`：门面类，封装零样本功能的完整调用流程，对外暴露同步/异步推理、模型管理、配置、PatchCore 样本管理等接口。
- `ZeroShotEngine`：零样本推理引擎，统一调度 AnomalyCLIP / Grounding DINO / MobileSAM / OpenCLIP / PatchCore 五种模型，支持稳定性推理与 NMS 去重。
- `ORTInferenceEngine`：ONNX Runtime 推理引擎，封装模型加载、预处理、推理、后处理、预热、取消与超时控制，兼容 OpenCV DNN 后端用于对比基准。
- `PipelineEngine`：流水线并行推理引擎，支持异常检测→目标检测→分割三阶段流水线，可配置串行/并行执行模式与阶段触发阈值。
- `EvaluationEngine`：评估体系引擎，支持从目录加载测试集、运行评估、计算分类/异常检测/性能指标、生成 JSON/文本报告。
- `BadCaseRecorder`：Bad Case 记录器，记录推理结果与用户修正结果的差异，支持 JSON 导入导出，用于模型优化回归测试。
- `ModelNotesManager`：模型注意事项管理器，从 `model_notes.json` 加载各模型注意事项，支持默认文件 + 用户覆盖两层配置。

### UI 层组件（3 + 1 个）

- `ZeroShotPanel`：零样本配置面板，提供模型类型选择、路径浏览、阈值调整、提示词输入、PatchCore 样本管理、量化开关、稳定性配置、人工复核开关等界面。
- `ZeroShotResultPanel`：结果展示面板，展示异常分数仪表、分类结果、检测框列表、掩码与热力图预览、性能指标，支持批量结果导航与人工复核操作。
- `ModelNotesDialog`：模型注意事项对话框，展示当前模型的使用注意事项、输入格式、限制条件与使用建议。
- `AnomalyGaugeWidget`：异常分数仪表子控件（嵌入 `ZeroShotResultPanel`），绘制 270 度弧形进度条，根据阈值切换颜色。

## 架构图

```
+---------------------------------------------------------------------+
|                        集成方应用（QDVMini 等）                       |
+---------------------------------------------------------------------+
                              |
                              | Kit 门面（zsu::Kit）
                              v
+---------------------------------------------------------------------+
|                            Core 层                                  |
|  (namespace zsu, 静态库 ZeroShotKitCore, 无 UI 依赖)                 |
|                                                                     |
|  +----------+   +------------------+   +----------------+           |
|  |   Kit    |-->|  ZeroShotEngine  |-->| ORTInference   |           |
|  |  门面    |   |  零样本调度       |   |   Engine       |           |
|  +----------+   +------------------+   +----------------+           |
|       |                ^                       |                    |
|       |                |                       | ONNX Runtime       |
|       v                |                       v                    |
|  +----------+    +------------------+   +----------------+           |
|  | Pipeline |    | EvaluationEngine |   | BadCaseRecorder|          |
|  |  Engine  |    +------------------+   +----------------+           |
|  +----------+                                          |            |
|       |                                       +----------------+    |
|       |                                       |ModelNotesManager|   |
|       v                                       +----------------+    |
|  异常检测 -> 目标检测 -> 分割                                        |
+---------------------------------------------------------------------+
                              |
                              | 可选编译（ZEROSHOTKIT_BUILD_UI=ON）
                              v
+---------------------------------------------------------------------+
|                            UI 层                                    |
|  (namespace QDVMini, 静态库 ZeroShotKitUI, 依赖 Qt6::Widgets)        |
|                                                                     |
|  +----------------+    +--------------------+    +---------------+  |
|  | ZeroShotPanel  |    | ZeroShotResultPanel|    |ModelNotesDialog| |
|  |  配置面板       |    |   结果展示面板      |    |  注意事项对话框 | |
|  +----------------+    +--------------------+    +---------------+  |
|                                                                     |
|  子控件：AnomalyGaugeWidget（异常分数仪表）                          |
+---------------------------------------------------------------------+
```

## 快速开始

最简 5 行代码：加载 AnomalyCLIP 模型 -> 配置提示词 -> 同步推理 -> 输出异常分数。

```cpp
#include "ZeroShotKit/ZeroShotKit.h"
#include <QCoreApplication>
#include <opencv2/opencv.hpp>
#include <iostream>

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    zsu::Kit kit;
    // 1. 加载 AnomalyCLIP 模型（modelPath 指向模型所在目录）
    kit.loadModel(zsu::ZeroShotModelType::AnomalyCLIP, "D:/models/anomaly_clip");

    // 2. 配置文本提示词（normal: 与 anomaly: 前缀必须成对出现）
    kit.setTextPrompts({"normal:a photo of a normal product",
                        "anomaly:a photo of a damaged product"});
    kit.setAnomalyThreshold(0.5f);

    // 3. 读取图像（支持中文路径，建议用 Kit::readImage 同款逻辑）
    cv::Mat img = cv::imread("test.jpg");
    if (img.empty()) { std::cerr << "图像读取失败" << std::endl; return 1; }

    // 4. 同步推理
    zsu::ZeroShotResult result = kit.infer(img);

    // 5. 输出异常分数
    std::cout << "success=" << result.success
              << " anomaly_score=" << result.anomalyScore
              << " latency_ms=" << result.metrics.totalMs << std::endl;
    return 0;
}
```

## 构建说明

### 依赖

| 依赖 | 最小版本 | 说明 |
| --- | --- | --- |
| Qt6 | 6.2+ | Core + Concurrent 必需；UI 层额外需要 Widgets |
| OpenCV | 4.5+ | 图像处理、`cv::dnn` 后端对比 |
| ONNX Runtime | 1.14+ | 默认启用，由 `ZEROSHOTKIT_ENABLE_ORT` 控制 |
| 编译器 | C++17 | MSVC 2019+ / MinGW GCC 9+ / Clang 10+ |

### CMake 选项

| 选项 | 默认值 | 说明 |
| --- | --- | --- |
| `ZEROSHOTKIT_BUILD_UI` | `ON` | 是否编译 UI 层（ZeroShotPanel / ZeroShotResultPanel / ModelNotesDialog） |
| `ZEROSHOTKIT_BUILD_TESTS` | `OFF` | 是否编译测试（预留目录，当前未启用） |
| `ZEROSHOTKIT_ENABLE_ORT` | `ON` | 是否启用 ONNX Runtime 后端，关闭后仅可用 OpenCV DNN |

### 构建目标

| 目标 | 别名 | 类型 | 说明 |
| --- | --- | --- | --- |
| `ZeroShotKitCore` | `ZeroShotKit::Core` | STATIC | Core 层静态库，纯推理，无 UI 依赖 |
| `ZeroShotKitUI` | `ZeroShotKit::UI` | STATIC | UI 层静态库，依赖 `ZeroShotKit::Core` 与 `Qt6::Widgets` |

## 集成方式

### 方式一：add_subdirectory（推荐）

将 `ZeroShotKit` 作为子目录添加到主项目 CMake 中。

```cmake
# 主项目 CMakeLists.txt
add_subdirectory(third_party/ZeroShotKit)

target_link_libraries(MyApp PRIVATE
    ZeroShotKit::Core     # Core 层（必选）
    # ZeroShotKit::UI     # UI 层（可选，需要 Qt6::Widgets）
)
```

主项目若已配置 `ONNXRUNTIME_INCLUDE_DIRS` 与 `ONNXRUNTIME_LIBS` 变量，ZeroShotKit 会直接复用，无需重复查找。

### 方式二：find_package（预编译安装，预留）

未来版本将提供 `ZeroShotKitConfig.cmake` 安装包，支持以下写法。

```cmake
find_package(ZeroShotKit REQUIRED COMPONENTS Core UI)
target_link_libraries(MyApp PRIVATE ZeroShotKit::Core ZeroShotKit::UI)
```

> 当前版本未实现安装规则，集成方请使用 `add_subdirectory` 方式。

### 方式三：直接复制源码（不推荐）

将 `include/ZeroShotKit/` 与 `src/` 下的源文件直接复制到主项目中编译。注意事项：

- 需自行处理 `ZSU_HAS_ORT` 宏定义与 ONNX Runtime 链接。
- 需自行处理 Qt MOC（`CMAKE_AUTOMOC ON`）。
- 不会获得 `ZeroShotKit::Core` / `ZeroShotKit::UI` 别名目标，需手动配置 include 路径。
- 升级困难，不推荐。

## 目录结构

```
ZeroShotKit/
├── README.md                      # 本文件
├── CMakeLists.txt                 # 顶层 CMake，定义选项与依赖查找
├── include/                       # 公共头文件（对外暴露）
│   └── ZeroShotKit/
│       ├── ZeroShotKit.h          # Kit 门面类
│       ├── Logger.h               # 日志适配宏
│       ├── ZeroShotTypes.h        # 公共类型定义（转发到 src/）
│       ├── ZeroShotEngine.h       # 零样本引擎（转发）
│       ├── ORTInferenceEngine.h   # ORT 推理引擎（转发）
│       ├── ORTCompat.h            # ORT 兼容性宏
│       ├── PipelineEngine.h       # 流水线引擎（转发）
│       ├── EvaluationEngine.h     # 评估引擎（转发）
│       ├── BadCaseRecorder.h      # Bad Case 记录器（转发）
│       └── ModelNotesManager.h    # 模型注意事项管理器（转发）
├── src/                           # Core 层源文件
│   ├── CMakeLists.txt             # Core 层 CMake，定义 ZeroShotKitCore 目标
│   ├── ZeroShotTypes.h            # 公共类型定义（实现）
│   ├── ZeroShotEngine.h/.cpp      # 零样本推理引擎
│   ├── ORTInferenceEngine.h/.cpp  # ORT 推理引擎
│   ├── ORTCompat.h                # MinGW SAL 注解兼容性
│   ├── PipelineEngine.h/.cpp      # 流水线并行推理引擎
│   ├── EvaluationEngine.h/.cpp    # 评估体系引擎
│   ├── BadCaseRecorder.h/.cpp     # Bad Case 记录器
│   ├── ModelNotesManager.h/.cpp   # 模型注意事项管理器
│   └── ZeroShotKit.cpp            # Kit 门面类实现
├── ui/                            # UI 层源文件
│   ├── CMakeLists.txt             # UI 层 CMake，定义 ZeroShotKitUI 目标
│   ├── ZeroShotPanel.h/.cpp       # 零样本配置面板
│   ├── ZeroShotResultPanel.h/.cpp # 结果展示面板（含 AnomalyGaugeWidget）
│   └── ModelNotesDialog.h/.cpp    # 模型注意事项对话框
├── resources/                     # 资源文件
│   └── models/
│       └── model_notes.json       # 模型注意事项默认配置
├── docs/                          # 文档目录
│   ├── API.md                     # API 参考
│   ├── IntegrationGuide.md        # 集成指南
│   ├── Examples.md                # 示例代码
│   └── Models.md                  # 模型说明
└── examples/                      # 示例代码目录（预留）
```

## 许可证

<!-- 许可证占位：集成方根据实际使用场景填写 -->

TODO: 在此填写许可证信息（如 MIT / Apache-2.0 / 商业许可等）。

## 相关文档

- [API 参考](docs/API.md)
- [集成指南](docs/IntegrationGuide.md)
- [示例代码](docs/Examples.md)
- [模型说明](docs/Models.md)
