# QDVAnnotator 交接文件 — 训练能力与训练入口

> 生成时间：2026-08-19 | 会话范围：本轮会话（训练能力 / 数据集导入 / 可训练模型类型梳理）
> 目的：交接「QDVAnnotator 能导入哪些数据集、训练哪些类型的模型、GUI 上开放了哪些训练入口」，
>       避免接手人重复翻代码定位。工具整体说明见 `README.md`；显示优化与训练闪退见
>       `QDVAnnotator_交接文件_显示优化与训练闪退_2026-08-19.md`（主题不同，本文件未覆盖）。

---

## 一、结论（先给答案）

- **QDVAnnotator 支持训练（标训一键闭环）**：真实入口在主程序之外，位于标注工具自身，与主程序训练推理模块（`TrainingInferenceView`）的入口分离。
- **可导入的数据集**（三种来源）：
  1. 当前工程自动导出为 YOLO 数据集（`yolo_detect`）再训练；
  2. 直接指定已导出的 OBB 数据集 `dataset.yaml`（`yolo_obb`）训练；
  3. 导入图像文件夹 / 打开 `.qdvann` 工程 / 导入 COCO 后标注，再走导出训练。
- **可训练的模型类型**：YOLO 系列 —— 旋转矩形（OBB）与轴对齐两类：
  - OBB：`yolov8n-obb` / `yolov8s-obb` / `yolov11n-obb`；
  - 轴对齐：`yolov8n` / `yolov8s` / `yolov8m` / `yolov5n` / `yolov5s`。

---

## 二、关键背景：两套训练入口对比

| 维度 | 主程序训练推理模块 (`TrainingInferenceView`) | QDVAnnotator 标训闭环 |
|---|---|---|
| 入口 | 工具栏「启动训练」，见 [TrainingInferenceView.cpp](file:///e:/anchor/Trae/QDV/src/TrainingInference/TrainingInferenceView.cpp#L599) | 主界面「标训闭环」按钮 → `TrainingDialog`，见 [Main.qml L191-192](file:///e:/anchor/Trae/QDV/tools/qdv-annotator/qml/Main.qml#L191-L192)、[Main.qml L328](file:///e:/anchor/Trae/QDV/tools/qdv-annotator/qml/Main.qml#L328) |
| 任务类型 | 仅**图像分类** | **目标检测**（含旋转矩形 OBB） |
| 模型选择 | 分类 CNN：`resnet18/50`、`efficientnet_b0`、`mobilenet_v3_small`（[TrainingInferenceView.cpp L674](file:///e:/anchor/Trae/QDV/src/TrainingInference/TrainingInferenceView.cpp#L674)） | YOLO 系列（见上文），走 [TrainingDialog.qml L157-166](file:///e:/anchor/Trae/QDV/tools/qdv-annotator/qml/TrainingDialog.qml#L157-L166) |
| 训练脚本 | `training/train.py`（分类） | `training/yolo_train.py`（检测/OBB） |
| 是否接 GUI 入口 | ✅ 已开放 | ✅ 已开放 |

> 说明：`feature_svm`（特征分类）仅 `train.py` 支持，**两个 GUI 均未暴露**。

---

## 三、GUI 训练入口链路（QDVAnnotator）

```
主界面「标训闭环」按钮(Main.qml)
  └─> TrainingDialog.qml（配置：数据集来源 / 输出目录 / Python / 模型 / 轮数 / 批大小 / 尺寸 / 学习率）
        ├─ 数据集来源 "自动导出当前工程(yolo_detect)"  → session.exportAndTrain(outDir, opt)
        ├─ 数据集来源 "使用已导出的OBB数据集(yolo_obb)" → session.trainFromDataYaml(dataYaml, outDir, opt)
        └─ 前置校验：有向矩形(vec_rect)工程 → 弹窗提示改导 yolo_obb；无标签则提示先加标签
  └─> TrainingBridge.cpp：写 config → QProcess 拉起 training/yolo_train.py → 解析 JSON-Lines 进度
```

关键接口（[AnnotationSession.h](file:///e:/anchor/Trae/QDV/tools/qdv-annotator/src/AnnotationSession.h)）：
- `exportAndTrain(outDir, options)`：自动导出 `yolo_detect` 数据集并启动训练，返回是否成功，失败读 `lastError`；
- `trainFromDataYaml(dataYaml, outDir, options)`：用已导出 `yolo_obb` 的 `dataset.yaml` 直接训练（跳过导出）；
- `hasVecRectAnnotations()`：检测有向矩形标注，用于提前拦停避免产出空标签；
- `cancelTraining()`、`isTraining`、`checkPythonEnv(py)`：取消 / 状态 / 真实校验 ultralytics+torch+CUDA。

训练桥协议（[TrainingBridge.h](file:///e:/anchor/Trae/QDV/tools/qdv-annotator/src/TrainingBridge.h)）：
- 启动命令：`python yolo_train.py --config <cfg> --output_dir <out>`；
- 配置 schema：`{model_type, num_epochs, batch_size, image_size, learning_rate, data_yaml, output_dir}`；
- 信号：`trainingProgress / trainingCompleted / trainingError / logOutput`，UI 不阻塞。

---

## 四、数据导入 / 导出格式（数据集侧）

QDVAnnotator 导出（`ExportDialog.qml`），数据集最终喂给训练：

| 格式 key | 产物 | 说明 |
|---|---|---|
| `yolo_detect` | `images/{train,val}/`、`labels/{train,val}/*.txt`、`dataset.yaml`、`labels.json`、`category_labels.json` | 轴对齐检测训练数据（标训闭环默认） |
| `yolo_obb`（有向矩形 OBB） | OBB 数据集 + `dataset.yaml` | 旋转矩形检测训练，供 `trainFromDataYaml` |
| `yolo_seg` | 上述 + `masks/` | 实例分割（`SegmentDl`） |
| `classification` | `train/<类>/`、`val/<类>/`、`labels.json` | 图像分类（喂主程序分类训练） |
| `ocr` | `images/`、`labels/<图>.json`、`gt.txt` | OCR（`DLOCR`/`Ocr`） |
| `coco` | `annotations/instances_{train,val}.json` | 与 Label Studio/CVAT 互通，可回灌再导入 |
| `qdvann` | 单工程文件 | 原生态：再次「打开」续标 |

导入入口（`ImportWizard.qml`）：导入图像文件夹 / 打开工程(`.qdvann`) / 导入 COCO。

---

## 五、训练环境与资源约束（务必知悉）

- **Python 解释器**：训练用独立 venv `E:/anchor/Trae/QDV/training/venv/Scripts/python.exe`（CUDA12.8+PyTorch2.7.1+ultralytics）。`TrainingDialog` 有「检测」可实体验证（非仅 `--version`）。
  - 注意备注：全局 `D:/Program Files/Python314` 的 torch import 会死锁，**不可**用于训练。
- **显存/页面文件自适应**（`training/yolo_train.py` `_tune_resources`）：
  - 真机 RTX 5060 Laptop（8GB 显存）默认 batch=8 已较安全；训练脚本会按显存自动 clamp 批大小、按可用页面文件自动降档并强制 `workers=0`；
  - 若页面文件过小（约 <4GB），训练易因资源被系统强杀 → GUI 闪退；根治需用户把 Windows 虚拟内存改为系统管理（或 ≥16GB）。
- 训练产物：`best<时间戳>.onnx` + 配套 `labels.json`，写入所选输出目录。

---

## 六、待办 / 后续建议

| # | 事项 | 优先级 |
|---|---|---|
| T1 | 真机用新版 exe 默认参数重跑「标训闭环」，确认不再闪退、日志可见资源降档提示 | 高 |
| T2 | OBB（旋转矩形）端到端真机训练跑通验证 | 中 |
| T3 | 如需分类训练，需通过主程序训练推理模块而非本工具（本工具标训闭环聚焦检测/OBB） | 信息 |
| T4 | 主程序训练模块尚未接通 `yolo_train.py` 检测训练入口（未来若需统一，可在 `TrainingInferenceView` 添加） | 低 |

---

## 七、关键文件清单

- `qml/Main.qml` — 「标训闭环」按钮入口（约 L184-192）
- `qml/TrainingDialog.qml` — 标训闭环配置/进度对话框（模型下拉、数据集来源、超参）
- `src/AnnotationSession.h/.cpp` — 会话调度：`exportAndTrain` / `trainFromDataYaml` / `exportDataset`
- `src/TrainingBridge.h/.cpp` — 训练桥：QProcess 拉起 `yolo_train.py` 并解析进度协议
- `training/yolo_train.py` — 检测训练脚本（含资源自适应 `_tune_resources`）
- `verify/verify_export.py` — 导出格式回归校验（10/10）

## 八、改动边界 / 红线

- 标注工具改动限定在 `tools/qdv-annotator` 独立目录，与 QDV 主程序隔离。
- QML 组件须在 `qmldir` + qrc + 引用处三处登记。
- 布局/界面变动后需同步更新 `ui_mockup.html` 示意图。