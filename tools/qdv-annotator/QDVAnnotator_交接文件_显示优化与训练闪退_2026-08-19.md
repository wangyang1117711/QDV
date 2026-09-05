# QDVAnnotator 交接文件 — 显示优化 + 训练闪退 排查与修复

> 生成时间：2026-08-19 | 会话范围：本轮会话
> 目的：交接「标训闭环窗口下拉宽度自适应、全组件显示优化」与「训练过程中软件闪退的排查修复」两项任务及其验证结论，避免接手人丢失上下文。
> 前置背景：标训闭环/OBB 整合的完整交接见 `QDVAnnotator_交接文件_标训闭环_2026-08-19.md`（不同主题，本文件未覆盖它）。工具整体说明见 `README.md`。

---

## 一、任务 A — 界面显示全面优化

### 1.1 问题
标训闭环窗口模型下拉框宽度过窄，导致"YOLOv11n-OBB (旋转矩形)"等长模型名无法完整显示；同时需全面排查下拉/表格/卡片/弹窗等所有显示组件，保证内容完整清晰展示。

### 1.2 方案与落地
- 新增自定义组件 **`WideComboBox`**：`qml/QDVAnnotator/WideComboBox.qml`
  - 弹层宽度 = `max(控件宽度, 最长选项文本宽度 + 内边距)`，且 ≤ 屏幕宽（多分辨率适配）。
  - 完全覆盖 `popup`，用 **`onOpened` 一次性 JS 赋值宽高**，消除先前反复出现的 `QML Popup: Binding loop` 警告（不要改回 width 绑定表达式）。
  - 控件内当前项被省略截断时，悬停 0.4s 弹出 `ToolTip` 显示完整文本。
- 已在 `qmldir` 与 `CMakeLists.txt` 的 qml_qrc 注册；QML 内以 `Tok.WideComboBox` 引用。

### 1.3 替换/加固组件（8 处）
| 文件 | 改动 |
|---|---|
| `qml/TrainingDialog.qml` | 数据集来源、模型、图像尺寸下拉 → WideComboBox |
| `qml/ExportDialog.qml` | 导出格式下拉 → WideComboBox（长名"YOLO 有向矩形 OBB..."整行显示） |
| `qml/Main.qml` | 任务类型下拉 → WideComboBox；激活标签/状态栏加截断检测 + 悬停 ToolTip |
| `qml/PreprocessPanel.qml` | 预处理模式下拉 → WideComboBox |
| `qml/LabelPanel.qml` | 标签名省略 + 悬停 ToolTip |
| `qml/ImageListPanel.qml` | 图像文件名省略 + 悬停 ToolTip |
| `qml/AnnotationListPanel.qml` | 标注项描述省略 + 悬停 ToolTip |

### 1.4 验证
- 编译 0 警告；冒烟启动存活且绑定循环 **0** 条；`test_annotation_session_undo.exe` exit=0；`verify/verify_export.py` 回归 **10/10**。
- 界面示意图已更新：`qml/../ui_mockup.html` 新增"标训闭环窗口 — 显示优化"展板。

---

## 二、任务 B — 训练过程中软件闪退排查与修复

### 2.1 现象
标训闭环点击训练，训练过程中 **QDVAnnotator 整个界面闪退**（非仅子进程退出）。

### 2.2 根因（环境级，非业务逻辑 bug）
1. **显存不足**：真机为 RTX 5060 Laptop，显存仅 **8150 MiB（8GB）**；默认 batch=16、imgsz=640 训练峰值显存接近/超 8GB。
2. **虚拟内存（页面文件）严重不足**：实测可用页面文件仅 **约 3~4.7GB**，加载 CUDA 库报 `[WinError 1455] 页面文件太小`，DataLoader worker 全部崩溃。
3. 两者叠加 → 系统级内存被训练进程榨干 → 前台 QDVAnnotator 被系统终止（GUI 本身无独立崩溃转储，属资源级强杀）。

### 2.3 修复内容
- **`E:\anchor\Trae\QDV\training\yolo_train.py`**
  - 新增 `_pagefile_available_mb()`：Windows 下用 `GlobalMemoryStatusEx` 探测可用页面文件。
  - 新增 `_tune_resources()`：训练前按 GPU 显存自动 clamp 批大小（8GB → ≤8）；按可用页面文件自动降档（<4GB → batch 减半）、强制 `workers=0` 单进程；输出明确降档原因与"扩大虚拟内存"建议。
  - 注意：`_tune_resources` 内 **自行 `import torch`**（曾因依赖调用方已导入 torch 导致 NameError 被吞、探测静默失效）。
- **`qml/TrainingDialog.qml`**：默认批大小 `16 → 8`（8GB 卡默认即安全，已加注释）。

### 2.4 验证
- `yolo_train.py` 语法编译通过；用训练 venv 实测：`batch16@640` → 自动降 **batch=4**（探测到 8150MiB 显存 + 页面文件仅 3.1GB），降档生效且有提示。
- QDVAnnotator 重新构建通过、冒烟启动存活、TrainingDialog QML 无解析错误。

### 2.5 环境根治建议（需用户处理）
在 Windows「系统属性 → 高级 → 性能 → 虚拟内存 → 更改」中将页面文件设为 **系统管理**（或 ≥16GB）。扩容后训练更稳，批大小可回提。此步骤软件无法代做，请务必转告用户。

---

## 三、待办 / 未决（接手后优先）

| # | 事项 | 优先级 | 说明 |
|---|---|---|---|
| R1 | **真机验收**：运行新版 `build/QDVAnnotator.exe`，默认参数重跑"标训闭环"，确认不再闪退、且踢日志可见资源自动降档提示（`type=resource`） | 高 | 需用户关闭旧 exe 后再开新 exe |
| R2 | 若仍偶发闪退，优先确认是否页面文件未扩容（查踢日志 `建议在 Windows 扩大页面文件` 提示） | 高 | 见 §2.5 |
| R3 | 回归：`verify/verify_export.py` 10/10 与 undo 单测仍须通过后再合交接 | 中 | 改动已满足 |
| R4 | （承接旧交接）标训闭环 OBB 端到端真机跑通、以及"部署回灌 8765 服务"一键化 | 中 | 详见旧交接文件 T1/T2/T6 |

---

## 四、关键文件清单

- `qml/QDVAnnotator/WideComboBox.qml` —— 自适应下拉组件（onOpened 赋值，勿改回绑定）
- `qml/TrainingDialog.qml` — 标训闭环窗口（batch 默认 8、下拉 WideComboBox）
- `qml/ExportDialog.qml` / `qml/Main.qml` / `qml/PreprocessPanel.qml` — 显示优化
- `qml/LabelPanel.qml` / `qml/ImageListPanel.qml` / `qml/AnnotationListPanel.qml` — 悬停 ToolTip
- `training/yolo_train.py` — 训练脚本（新增资源自适应 `_tune_resources`)
- `ui_mockup.html` — 界面示意图（含显示优化展板）
- `src/TrainingBridge.cpp` / `src/AnnotationSession.cpp` — 训练桥与会话（未在本会话改动，了解即可）

## 五、改动边界 / 红线（延续项目约束）
- 所有改动限定在 `tools/qdv-annotator` 独立工具目录与 `training/yolo_train.py`（训练脚本，与 QDV 主程序隔离）；**QDV 主程序零改动**。
- QML 组件需在 `qmldir` + qrc + 引用处三处登记，否则静态库链接/加载会失败。
- 不得把 `WideComboBox` 的 popup 宽度改回绑定表达式（会回归绑定循环）。
- 合规要求：命令仅 PowerShell；页面/布局变动后需更新 `ui_mockup.html` 示意图。