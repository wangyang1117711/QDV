# 算子流程导出功能设计（Spec）

- **版本**：1.0
- **日期**：2026-07-14
- **主题**：在编辑模块中对已验证的算子流程，开发多格式（DLL/EXE/Python）导出功能
- **设计模式**：海康 MVS（VisionMaster）模式 —— 方案文件 + 精简运行时包 + 多语言 wrapper
- **状态**：已与用户逐项确认 9 项关键决策 + 1 项代码组织方案，用户全部认可

---

## 1. 背景与目标

### 1.1 背景

QDV（奇测视觉检测系统）当前编辑模块已具备算子流程的编辑、运行、保存（`.qdvz`/`.json`/`.xml`）能力，但缺少将算子流程导出为**可被外部程序调用的 DLL/EXE/Python 模块**的能力。现有导出类（[AI/ExportManager](file:///e:/anchor/Trae/QDV/include/AI/ExportManager.h)、[TrainingInference/ExportManager](file:///e:/anchor/Trae/QDV/include/TrainingInference/ExportManager.h)）分别面向数据集与推理结果，与本需求无关。

### 1.2 目标

针对已通过验证且运行稳定可靠的算子流程，开发一套完整导出功能：

1. **DLL 格式**：纯 C 接口（`extern "C" __cdecl`）wrapper DLL，C++ 程序可 `LoadLibrary` 直接调用
2. **EXE 格式**：命令行可执行文件，通过参数接收输入图、返回处理结果
3. **Python 格式**：`.py` 模块文件，通过 `ctypes` 加载 wrapper DLL，`import` 即可调用

### 1.3 成功标准（三层）

| 层级 | 标准 |
|------|------|
| 技术成功 | 导出的 DLL/EXE/Python 产物可被对应语言调用，且运行结果与编辑模块内 `runScheme` 一致 |
| 业务成功 | 一套固定运行时包 + 方案文件支持任意已验证流程导出；导出为秒级组装，无需现场编译 |
| 用户满意 | 导出对话框配置项完整、接口文档清晰、调用示例可直接编译运行 |

### 1.4 非目标（YAGNI）

- 不实现 `OperatorTester` 桩、不启用 `OperatorStatus::Published` 状态标记系统（验证机制另行立项）
- 不做按流程裁剪运行时包（固定运行时包即可复用）
- 不做 C++ 导出类接口、不做 ONNX Runtime 后端（运行时仅内置 OpenCV DNN）
- 不做现场生成源码 + 调用编译器编译（采用模板预编译）
- 不做跨平台（仅 Windows x64，MinGW 13.1 编译）

---

## 2. 架构总览

**核心思想**：导出 = 组装预编译模板，而非现场编译。QDV 主程序构建时一次性编译好 wrapper DLL/EXE/运行时 DLL 模板；导出时由 `SchemeExporter` 复制模板 + 序列化当前方案 + 生成接口文档与示例 + 写入元信息。

```
QDV 主程序构建时                       导出时（秒级，不调编译器）
┌───────────────────────┐             ┌───────────────────────────┐
│ templates/src/         │   编译 →    │ templates/bin/             │
│  ├ QDVPipeline.cpp     │             │  ├ QDVPipeline.dll (wrapper)│
│  ├ QDVPipelineRun.cpp  │             │  ├ QDVPipelineRun.exe       │
│  ├ QDVRuntime.cpp      │             │  └ QDVRuntime.dll (运行时)  │
│  └ CMakeLists.txt      │             └─────────────┬─────────────┘
└───────────────────────┘                            │ 复制
                                                     ▼
┌───────────────────────┐             ┌───────────────────────────┐
│ EditViewBridge         │   触发 →    │ SchemeExporter::export()  │
│  (导出槽收集参数)       │             │  1. 复制模板              │
│ ExportDialog.qml       │ ──────────→ │  2. 序列化方案(.qdvz)      │
└───────────────────────┘             │  3. 生成文档/示例          │
                                       │  4. 写元信息              │
                                       └─────────────┬─────────────┘
                                                     ▼
                                       <输出目录>/<导出名>/ (产物包)
```

### 2.1 三态产物关系

DLL/EXE/Python 三种格式共用同一套**运行时包 + 方案文件**，仅入口语言不同：

- DLL：wrapper DLL 暴露 C 接口，内部调用 `QDVRuntime`
- EXE：命令行可执行，内部复用 `QDVRuntime`
- Python：`ctypes` 加载 wrapper DLL

`QDVRuntime.dll` 是三者的共同运行时核心（聚合算子 + `ToolChainExecutor`）。

---

## 3. 代码组织与模块划分

### 3.1 新增模块 `src/Export/`（静态库 `QDVExport`）

| 文件 | 职责 |
|------|------|
| `include/Export/SchemeExporter.h` | 导出编排器（对外门面，异步执行） |
| `include/Export/ExportConfig.h` | 导出配置结构体 |
| `include/Export/ExportPackage.h` | 导出包目录结构定义 |
| `include/Export/InterfaceDocGenerator.h` | Markdown 接口文档生成器 |
| `include/Export/ExampleCodeGenerator.h` | C++/Python 调用示例生成器 |
| `include/Export/RuntimePackager.h` | 运行时 DLL 打包器（收集依赖） |
| `include/Export/TemplateLocator.h` | 模板路径定位（读 `config.h`） |
| `src/Export/*.cpp` | 对应实现 |
| `src/Export/CMakeLists.txt` | 构建配置，链接 `Core`/`Vision`/`UI`/`Qt6::Concurrent` |

**命名**：导出类用 `SchemeExporter`，避开已冲突的 `ExportManager`（[AI/ExportManager](file:///e:/anchor/Trae/QDV/include/AI/ExportManager.h)、[TrainingInference/ExportManager](file:///e:/anchor/Trae/QDV/include/TrainingInference/ExportManager.h)）。

### 3.2 新增模板源码 `templates/`（独立 CMake 子工程）

| 文件 | 职责 |
|------|------|
| `templates/src/QDVPipeline.cpp` | wrapper DLL 实现（`extern "C"` 接口） |
| `templates/include/QDVPipeline.h` | C 接口头文件（导出时复制到产物 `include/`） |
| `templates/src/QDVPipelineRun.cpp` | 命令行 EXE 实现 |
| `templates/src/QDVRuntime.cpp` | 运行时门面（聚合算子执行） |
| `templates/CMakeLists.txt` | 编译产出 `templates/bin/QDVPipeline.dll` + `QDVPipelineRun.exe` + `QDVRuntime.dll` |

### 3.3 UI 层修改 `src/UI/`

| 文件 | 修改 |
|------|------|
| [include/UI/EditViewBridge.h](file:///e:/anchor/Trae/QDV/include/UI/EditViewBridge.h) | 新增 `Q_INVOKABLE exportScheme(config: QVariantMap)`、`Q_INVOKABLE runExportSelfCheck()` 槽 + `exportProgress(int)`、`exportFinished(bool,QString)` 信号 |
| [src/UI/EditViewBridge.cpp](file:///e:/anchor/Trae/QDV/src/UI/EditViewBridge.cpp) | 槽实现，转发给 `SchemeExporter` |
| `qml/EditView/ExportDialog.qml` | 新增导出配置对话框 |
| [qml/EditView/Main.qml](file:///e:/anchor/Trae/QDV/qml/EditView/Main.qml) | 工具栏新增"导出"按钮 |
| [qml/EditView/qmldir](file:///e:/anchor/Trae/QDV/qml/EditView/qmldir) + [qml/EditView.qrc](file:///e:/anchor/Trae/QDV/qml/EditView.qrc) | 注册新 QML 文件 |
| [src/UI/CMakeLists.txt](file:///e:/anchor/Trae/QDV/src/UI/CMakeLists.txt) | 链接 `QDVExport` |

### 3.4 顶层构建 [CMakeLists.txt](file:///e:/anchor/Trae/QDV/CMakeLists.txt)

- 新增 `add_subdirectory(src/Export)`、`add_subdirectory(templates)`
- `templates/bin/` 绝对路径写入 [cmake/config.h.in](file:///e:/anchor/Trae/QDV/cmake/config.h.in) 宏 `QDV_TEMPLATE_BIN_DIR`，供 `TemplateLocator` 定位模板

---

## 4. 导出数据流

1. 用户在 [Main.qml](file:///e:/anchor/Trae/QDV/qml/EditView/Main.qml) 点"导出" → `ExportDialog.qml` 收集配置 → 调 `EditViewBridge.exportScheme(config)`
2. `EditViewBridge` 把 `config` + 当前 `m_currentNodes`/`m_connections`（QVariantList）传给 `SchemeExporter::export(config, nodes, connections)`
3. `SchemeExporter` 异步执行（`QtConcurrent::run` + `QFutureWatcher`，复用 [SchemeSerializer](file:///e:/anchor/Trae/QDV/src/UI/SchemeSerializer.cpp) 的异步模式）：
   1. 校验输出路径；若目录已存在导出包 → 通过 `confirmOverwriteRequested` 信号请 UI 弹确认
   2. 创建产物目录结构（见 §5）
   3. 复制所选格式对应的模板二进制到 `bin/`
   4. `RuntimePackager` 收集运行时依赖（OpenCV、Qt6、`platforms/qwindows.dll`）
   5. 用现有 [SchemeSerializer](file:///e:/anchor/Trae/QDV/include/UI/SchemeSerializer.h) 序列化方案到 `scheme/pipeline.qdvz`（外置）或嵌入 wrapper DLL 资源段（嵌入模式）
   6. 若流程含 AI 算子：复制其 `modelPath` 指向的 onnx 模型到 `models/`
   7. `InterfaceDocGenerator` 生成 `docs/接口文档.md`
   8. `ExampleCodeGenerator` 生成 `docs/examples/{cpp,python}/`
   9. 写 `manifest.json`（版本/作者/格式/接口名/导出时间/算子清单）
4. 进度通过 `exportProgress(int)` 信号驱动 `ExportDialog.qml` 进度条
5. 完成 → [Main.qml](file:///e:/anchor/Trae/QDV/qml/EditView/Main.qml) Toast 提示 + 可选自动打开产物目录

### 4.1 ExportConfig 结构

```cpp
struct ExportConfig {
    // 基础项
    bool exportDll = true;
    bool exportExe = true;
    bool exportPython = true;
    QString exportName;        // 导出包名（目录名）
    QString interfaceName;     // 接口名（影响 Python 类名/C++ 示例类名/文档命名）
    QString outputPath;        // 输出根目录（默认 D:\exports\）

    // 额外项
    bool embedScheme = true;   // 方案嵌入 DLL/EXE 还是外置
    bool generateDoc = true;   // 生成 Markdown 接口文档
    bool generateExamples = true; // 生成调用示例代码
    QString version = "1.0.0"; // SemVer
    QString author;
    QString description;

    // 自检（可选，不阻断导出）
    bool runSelfCheck = false;
    QString selfCheckSampleImage; // 示例图路径
};
```

---

## 5. 导出包目录结构

```
<outputPath>/<exportName>/
├── bin/
│   ├── QDVPipeline.dll              (wrapper DLL，仅 DLL 格式导出)
│   ├── QDVPipelineRun.exe           (命令行 EXE，仅 EXE 格式导出)
│   ├── qdv_pipeline.py              (Python wrapper，仅 Python 格式导出)
│   └── qdv_runtime/                 (运行时 DLL 包)
│       ├── QDVRuntime.dll
│       ├── opencv_world4xx.dll
│       ├── Qt6Core.dll / Qt6Gui.dll
│       ├── platforms/qwindows.dll
│       └── config/operators.json
├── scheme/
│   └── pipeline.qdvz                (方案文件，外置模式；嵌入模式则打进 bin 内)
├── include/
│   └── QDVPipeline.h                (C 接口头文件，仅 DLL 格式)
├── lib/
│   └── libQDVPipeline.a             (MinGW 导入库，仅 DLL 格式)
├── models/                           (AI 算子 onnx 模型，仅含 AI 算子时)
├── docs/
│   ├── 接口文档.md
│   ├── 使用说明.md
│   └── examples/
│       ├── cpp/main.cpp + CMakeLists.txt  (仅 DLL)
│       └── python/main.py                  (仅 Python)
└── manifest.json                     (导出元信息)
```

### 5.1 manifest.json 示例

```json
{
  "exportName": "MyPipeline",
  "interfaceName": "MyPipeline",
  "version": "1.0.0",
  "author": "qdv-user",
  "description": "...",
  "exportedAt": "2026-07-14T10:30:00",
  "formats": ["dll", "exe", "python"],
  "schemeEmbedded": true,
  "operators": ["ReadImage", "Threshold", "BlobDetect"],
  "containsAi": false,
  "qdvVersion": "1.0.0",
  "templateBuildId": "由 TemplateLocator 从模板产物的时间戳/版本元数据自动读取"
}
```

---

## 6. wrapper DLL C 接口设计（`templates/include/QDVPipeline.h`）

纯 C 接口（`extern "C" __cdecl`）、opaque handle 风格、多实例安全：

```c
#ifndef QDVPIPELINE_H
#define QDVPIPELINE_H

#include <stdint.h>

#ifdef QDVPIPELINE_EXPORTS
  #define QDVP_EXPORT __declspec(dllexport)
#else
  #define QDVP_EXPORT __declspec(dllimport)
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    QDVP_OK = 0,
    QDVP_ERR_INVALID_ARG = 1,
    QDVP_ERR_LOAD_SCHEME = 2,
    QDVP_ERR_RUN = 3,
    QDVP_ERR_NO_RESULT = 4,
    QDVP_ERR_INTERNAL = 99,
} QDVPResult;

typedef struct QDVPipelineHandle QDVPipelineHandle;

QDVP_EXPORT QDVPipelineHandle* QDVP_Pipeline_Create(void);
QDVP_EXPORT QDVPResult         QDVP_Pipeline_Load(QDVPipelineHandle* h, const char* schemePath);
QDVP_EXPORT QDVPResult         QDVP_Pipeline_SetInputImage(QDVPipelineHandle* h, const char* imagePath);
QDVP_EXPORT QDVPResult         QDVP_Pipeline_Run(QDVPipelineHandle* h);
QDVP_EXPORT const char*        QDVP_Pipeline_GetResultJson(QDVPipelineHandle* h);
QDVP_EXPORT const char*        QDVP_Pipeline_GetResult(QDVPipelineHandle* h, const char* resultName);
QDVP_EXPORT QDVPResult         QDVP_Pipeline_GetOutputImage(QDVPipelineHandle* h, const char* resultName, const char* savePath);
QDVP_EXPORT void              QDVP_Pipeline_Destroy(QDVPipelineHandle* h);
QDVP_EXPORT const char*        QDVP_GetVersion(void);
QDVP_EXPORT const char*        QDVP_GetLastError(QDVPipelineHandle* h);

#ifdef __cplusplus
}
#endif
#endif
```

### 6.1 实现要点（`templates/src/QDVPipeline.cpp`）

- 每个 `QDVPipelineHandle` 内部持有独立的 `QDVRuntime` 实例与 `VisionTool` 链（避免 [ToolChainExecutor.h](file:///e:/anchor/Trae/QDV/include/Vision/ToolChainExecutor.h) 注释中所述的非线程安全问题）
- `QDVRuntime` 内部用 [ToolFactory::createTool](file:///e:/anchor/Trae/QDV/include/Vision/ToolFactory.h) 重建算子实例，用 [ToolChainExecutor](file:///e:/anchor/Trae/QDV/include/Vision/ToolChainExecutor.h) 执行
- 字符串返回值（`GetResultJson`/`GetResult`/`GetLastError`/`GetVersion`）为内部线程局部缓冲，下次同线程调用失效；调用方需立即拷贝
- 错误信息用 thread_local `std::string` 存储，`GetLastError` 返回其 `c_str()`

---

## 7. EXE 命令行设计（`QDVPipelineRun.exe`）

```
QDVPipelineRun.exe --scheme <path.qdvz> --input <image.png>
                    [--output <result.json>] [--format json|text]
                    [--save-image <out.png>]

退出码：0 = 成功，非 0 = 失败（错误信息输出到 stderr）
```

- 内部复用 `QDVRuntime`（与 DLL 共用运行时核心）
- `--format json`：输出各算子结果的 JSON 到 stdout 或 `--output` 文件
- `--format text`：输出人类可读文本
- `--save-image`：保存指定算子的输出图

---

## 8. Python wrapper 设计（`qdv_pipeline.py`）

通过 `ctypes` 加载 `QDVPipeline.dll`，封装为 Pythonic 类。**不使用 `eval`**（用户规则）：

```python
import ctypes, os, json

class MyPipeline:                      # 类名 = 用户配置的 interfaceName
    def __init__(self, runtime_dir=None): ...   # 定位 QDVPipeline.dll 与运行时
    def load(self, scheme_path: str) -> None: ...
    def set_input_image(self, image_path: str) -> None: ...
    def set_input_ndarray(self, arr) -> None: ...  # 可选，需 numpy
    def run(self) -> None: ...
    def get_result_json(self) -> dict: ...        # 解析 JSON 为 dict
    def get_result(self, name: str): ...
    def get_output_image(self, name: str, save_path: str) -> None: ...
    def version(self) -> str: ...
    def last_error(self) -> str: ...
    def close(self) -> None: ...
    def __enter__(self): return self
    def __exit__(self, *a): self.close()
```

依赖说明（写入文档）：
- `ctypes`（标准库，必选）
- `numpy`（可选，用于 `set_input_ndarray`）
- `opencv-python`（可选，用于图像读取辅助）

---

## 9. 运行时包构成（`qdv_runtime/`）

固定精简运行时包，一次打包可复用任意方案：

| 内容 | 来源 | 说明 |
|------|------|------|
| `QDVRuntime.dll` | `templates/bin/` | 聚合 15 个内置算子 + `ToolChainExecutor` + `ToolFactory` + 必要 Core，合并为单 DLL |
| `opencv_world4xx.dll` | `D:/opencv/build_mingw/bin` | MinGW 编译版 |
| `Qt6Core.dll` / `Qt6Gui.dll` | Qt 安装目录 | 由 `RuntimePackager` 调 `windeployqt` 收集 |
| `platforms/qwindows.dll` | Qt plugins | 同上 |
| `config/operators.json` | [config/operators.json](file:///e:/anchor/Trae/QDV/config/operators.json) | 算子元数据 |
| `models/*.onnx` | AI 算子 `modelPath` | 仅当流程含 AI 算子时复制（AI 模块零侵入，仅读取路径） |

**AI 推理引擎**：运行时内置 OpenCV DNN 后端（`InferenceEngine` 的 OpenCV DNN 实现），不依赖 ONNX Runtime（保持精简）。

---

## 10. ExportDialog UI 设计

`ExportDialog.qml`（模态对话框，复用 [DeployDialog.qml](file:///e:/anchor/Trae/QDV/qml/EditView/DeployDialog.qml) 与 [ParamForm.qml](file:///e:/anchor/Trae/QDV/qml/EditView/ParamForm.qml) 风格）：

```
┌─ 导出算子流程 ─────────────────────────┐
│ 格式:   ☑ DLL   ☑ EXE   ☑ Python        │
│ 导出名称: [MyPipeline           ]       │
│ 接口名称: [MyPipeline           ]       │
│ 输出路径: [D:\exports\        ] [浏览]   │
│ 方案:   ◉ 嵌入 DLL/EXE  ○ 外置文件      │
│ 版本:   [1.0.0]  作者: [        ]       │
│ 描述:   [                       ]       │
│ [运行自检]  ← 调 ToolChainVerifier + 示例图试跑 │
│ ─────────────────────────────────────   │
│ 进度: [████████░░░░] 60%  正在生成文档… │
│                       [取消]  [导出]    │
└──────────────────────────────────────────┘
```

- 覆盖已有导出包 → 弹二次确认（用户规则：不可逆操作前必须确认）
- 取消按钮中止 `QFutureWatcher`
- 路径在 C 盘 → 警告并建议改 D 盘（用户规则：C 盘空间最小化），允许用户坚持

---

## 11. 接口文档与调用示例生成

### 11.1 `docs/接口文档.md`（Markdown）

1. **概述**：流程名、版本、作者、导出时间、包含算子清单（自 [OperatorDescriptors](file:///e:/anchor/Trae/QDV/include/UI/OperatorDescriptors.h) 取 cnName）
2. **部署说明**：运行时目录结构、依赖 DLL、环境要求（Windows x64）
3. **DLL 调用**：C 接口函数表（函数/参数/返回值/说明）、调用流程、`LoadLibrary` 示例、依赖项
4. **EXE 调用**：命令行参数表、输出格式、退出码、调用示例
5. **Python 调用**：类/方法表、参数类型、返回值、依赖安装（`pip install`）、`import` 示例
6. **注意事项**：32/64 位、编码（UTF-8）、AI 模型路径、多线程限制、方案嵌入/外置差异

### 11.2 `docs/examples/`

- `cpp/main.cpp` + `CMakeLists.txt`（仅 DLL 格式时生成）：完整可编译的 C++ 调用示例
- `python/main.py`（仅 Python 格式时生成）：完整可运行的 Python 调用示例
- `使用说明.md`：快速上手（5 分钟跑通）

---

## 12. 验证前置（可选自检）

按确认采用**不强制 + 可选自检**：

- 导出对话框"运行自检"按钮：
  1. 调用现有 [ToolChainVerifier](file:///e:/anchor/Trae/QDV/include/Vision/ToolChainVerifier.h)（已完整实现，覆盖 15 内置算子自检）→ 显示通过/失败清单
  2. 用流程绑定的示例图（或用户指定图）试跑 [ToolChainExecutor::execute](file:///e:/anchor/Trae/QDV/include/Vision/ToolChainExecutor.h)() → 显示各算子耗时与结果摘要
- 自检不通过**不阻断导出**（仅警告），由用户决定
- 不实现 `OperatorTester` 桩、不启用 `OperatorStatus::Published`（避免扩大范围）

---

## 13. 错误处理与边界

| 场景 | 处理 |
|------|------|
| 模板缺失（`templates/bin/` 无产物） | 提示"请先构建项目生成导出模板"，不继续 |
| 方案为空（无节点） | 拒绝导出并提示 |
| 输出路径在 C 盘 | 警告并建议改 D 盘，允许用户坚持 |
| 覆盖已有导出包 | 二次确认对话框 |
| 导出过程异常 | Toast + [Logger](file:///e:/anchor/Trae/QDV/include/Core/Logger.h) 记录完整堆栈，保留已生成部分产物 |
| AI 算子模型缺失 | 警告但仍导出（运行时会报模型加载错误） |
| 字符串返回缓冲失效 | 文档明确提示调用方立即拷贝 |

---

## 14. 测试策略

| 类型 | 内容 |
|------|------|
| 单元测试（Catch2，加到 [tests/](file:///e:/anchor/Trae/QDV/tests/)） | `SchemeExporter` 组装逻辑、`InterfaceDocGenerator` 文档正确性、`ExampleCodeGenerator` 代码可解析性、`RuntimePackager` 依赖收集完整性 |
| 模板构建测试 | `templates/` 能独立编译产出三个二进制 |
| 集成测试 | 导出含 3-5 算子的示例流程 → 产物被 C++ 示例与 Python 示例调用 → 验证返回结果与编辑模块内 `runScheme` 一致（轨迹比对，符合 AGENTS.md 影子验证要求） |
| 验收测试 | 截图回归（复用 `test_visual_output/` 机制）记录导出对话框与产物目录 |

---

## 15. 约束遵守清单

| 约束 | 遵守方式 |
|------|---------|
| 不可模拟（用户规则） | 模板真实编译、导出产物真实可调用，禁用任何 mock |
| 禁止 eval（用户规则） | Python wrapper 用 ctypes，不 eval |
| C 盘最小化（用户规则） | 默认输出路径 D 盘，模板产物放 D 盘构建目录 |
| MinGW `-j 1`（历史教训） | 模板 CMake 遵守 |
| Q_INIT_RESOURCE（历史教训） | 运行时不含 QML 资源，wrapper DLL 无需；若后续运行时需 QML 再补 |
| windeployqt 传 `--qmldir`（历史教训） | 运行时无 QML，仅部署基础 Qt；若扩展再传 |
| AI 零侵入 | 不改 [include/AI/](file:///e:/anchor/Trae/QDV/include/AI/)、[src/AI/](file:///e:/anchor/Trae/QDV/src/AI/)，仅读取模型路径 |
| Windows Forms 设计器禁区 | 本项目 QML，N/A |
| 不可逆操作前确认 | 覆盖导出包弹确认 |

---

## 16. 关键决策记录（与用户确认）

1. 架构方向：海康 MVS 模式（方案文件 + 精简运行时包 + 多语言 wrapper）
2. 验证前提：不强制 + 可选自检（`ToolChainVerifier` + 示例图试跑）
3. 入口 UI：工具栏"导出"按钮 + 独立 `ExportDialog.qml`
4. 配置选项：格式（多选）/路径/接口名 + 方案嵌入外置 + 接口文档 + 调用示例 + 版本/作者元信息
5. 运行时包：固定精简（全 15 算子 + 执行器 + OpenCV/Qt DLL）+ AI 随方案带 onnx + 内置 OpenCV DNN
6. 接口风格：纯 C 接口（`extern "C" __cdecl`）+ opaque handle
7. 构建方式：模板预编译 + 导出时组装（秒级，不调编译器）
8. 代码组织：方案 A —— 新增 `src/Export/` 模块 + `templates/` 模板源码目录
9. 接口名称自定义：C ABI 函数名固定（`QDVP_*`），自定义体现在 Python 类名/C++ 示例类名/文档命名

---

## 17. 后续步骤

本 spec 批准后，转入 writing-plans skill 制定原子化实现计划。
