# QDV 数据标注工具（QDV Annotator）

> 面向 **QDV（QDetectVision）工业机器视觉平台** 的数据标注工具，
> 与项目现有 **70 个视觉算子 / 6 个 AI 算子** 无缝对接，覆盖其全部监督学习任务的数据生产需求。

---

## 1. 设计理念（对标 Label Studio / CVAT / Prodigy）

| 成熟工具理念 | 本工具落点 |
|------|------|
| **Label Studio**：多模态标注 + 可配置模板 + 灵活导出 | 任务类型可切换（检测/分类/分割/OCR），导出器插件化，支持 COCO 与 Label Studio/CVAT 互通 |
| **CVAT**：服务端协作 + 自动化 + 视频/图像任务 | 单机轻量版；`dataset.yaml` + YOLO 目录结构与 CVAT/YOLO 训练管线一致；支持从 COCO 回灌 |
| **Prodigy**：脚本化、以"数据流"为核心、边标边训 | 导出即训练数据：标注结果直接生成 `category_labels.json`（填算子 `categoryLabels` 参数）与 `labels.json`（填模型注册），闭环进 `TrainServerThread` |

**与 QDV 项目"无缝集成"的三层保证：**
1. **视觉/交互复用**：直接 `import QDV.EditView 3.0`，复用项目的 `AnnotateOverlay.qml`（矩形/多边形绘制层）与 `DesignTokens.qml`（设计令牌），界面与主程序零差异、零重复代码。
2. **数据契约对齐**：标签格式 = 算子消费的 `categoryLabels` 数组；模型注册格式 = `{"labels":[...]}`（同 `models/labels.json`）；坐标统一以**图像像素整数**存储，导出时按需归一化。
3. **零新增重依赖**：纯 Qt6（Core/Gui/Qml/Quick），**不引入 OpenCV**，工业 8bpp 灰度 BMP 由 `QImage` 原生加载，避免破坏项目既有 MinGW/OpenCV 构建链。

---

## 2. 支持的标注任务 ↔ AI 算子映射

| 标注任务（taskType） | 绘制的几何 | 导出格式 key | 对接的 QDV 算子 |
|------|------|------|------|
| **目标检测** `detection` | 矩形框 | `yolo_detect` | `YoloDetect` / `DetectObjectsDl` / `ZeroShotDetect` |
| **实例分割** `segmentation` | 多边形 / 自由形状 | `yolo_seg` | `SegmentDl` |
| **旋转矩形 OBB** `obb` | 方向矩形（中心 + 半长宽 + 角度 + 根数） | `yolo_obb` | `RotatedDetectDl` / 旋转矩形算子 |
| **图像分类** `classification` | 无（图像级打标） | `classification` | `AiClassify` |
| **OCR 文字** `ocr` | 矩形框 + 转写文本 | `ocr` | `DLOCR` / `Ocr` |
| （扩展位）`keypoint` | 点 | — | 预留给未来姿态/关键点算子 |

> 即：项目总结报告中 "DLOCR/AiClassify/YoloDetect 后端为桩（P3）" 的算子，
> 其**训练数据生产环节**现在已具备完整工具链支撑。

---

## 3. 目录结构

```
tools/qdv-annotator/
├── CMakeLists.txt            # 独立构建（Qt6 only）；也可 add_subdirectory 并入主工程
├── main.cpp                  # 入口：注册 AnnotationSession + 复用 QDV.EditView 模块
├── capabilities.json         # 能力注册表：形状/导出/训练任务 统一元数据（改配置即扩展，无需改代码）
├── capabilities.qrc          # 将 capabilities.json 打包进 exe（qrc:/capabilities 兜底）
├── src/
│   ├── AnnotationSession.h/.cpp   # 后端会话控制器（数据模型 + 导入导出调度）
│   ├── ShapeRegistry.h/.cpp       # 能力注册表只读加载器，暴露为 QML 全局属性 `capabilities`
│   └── Exporters.h/.cpp          # 导出/导入辅助（YOLO/分类/OCR/COCO/原生）
├── qml/
│   ├── Main.qml               # 三栏主窗口（图像列表 / 画布 / 标签+标注项）
│   ├── AnnotationCanvas.qml  # 画布：缩放/平移 + 复用 AnnotateOverlay（坐标变换）
│   ├── ImageListPanel.qml    # 图像列表（缩略图 + 标注状态）
│   ├── LabelPanel.qml        # 标签管理（增删改 + 激活标签 / 分类勾选）
│   ├── AnnotationListPanel.qml # 当前图标注列表（删除 / OCR 转写编辑）
│   ├── ExportDialog.qml      # 导出格式与参数
│   └── ImportWizard.qml      # 导入文件夹 / 打开工程 / 导入 COCO
└── verify/
    └── verify_export.py      # 导出格式兼容性验证器（在真实 测试图/ 上实跑）
```

---

## 4. 构建

工具仅依赖 Qt6，**无需 OpenCV**，可在不改动主工程的前提下独立构建：

```powershell
cd E:\anchor\Trae\QDV\tools\qdv-annotator
cmake -S . -B build -DCMAKE_PREFIX_PATH="D:\Qt_new\6.11.1\mingw_64"
cmake --build build -j 8
.\build\QDVAnnotator.exe
```

> 若希望并入 QDV 主工程统一构建，在主 `CMakeLists.txt` 添加 `add_subdirectory(tools/qdv-annotator)` 即可。
> 运行时通过 `QML_IMPORT_PATH` 或相对路径自动定位 `../../qml` 复用 `QDV.EditView` 模块。

---

## 5. 使用流程

1. **导入**：顶栏「导入」→ 选择图像文件夹（如 `测试图/`），可同时建立初始标签（逗号分隔）。
2. **标注**：
   - 检测/分割/OCR：在画布拖框/点多变形；左侧标签面板选中「激活标签」决定类别。
   - 分类：在右侧标签面板勾选当前图像所属类别（支持多标签）。
   - OCR：画框后在右侧列表选中该框，填写转写文本。
   - 快捷键：`←/→` 切换图，`0` 选择编辑模式，`1` 矩形 / `2` 多边形 / `3` 方向矩形（具体工具随注册表解析），`Ctrl+S` 保存，`Ctrl+E` 导出。
3. **导出**：顶栏「导出」→ 选格式 → 设置 train/val 拆分与随机种子 → 生成数据集。

> 绘制工具按 `capabilities.json` 中「启用」的形状**动态生成**，同一 `overlayType` 的多个形状（如 `polygon` 与 `freeform`）复用同一绘制实现，由 `activeShapeKey` 精确区分。

---

## 6. 导出格式 ↔ 算子对接明细

| 格式 key | 产物 | 如何喂给 QDV |
|------|------|------|
| `yolo_detect` | `images/{train,val}/`、`labels/{train,val}/*.txt`（YOLO 归一化）、`dataset.yaml`、`labels.json`、`category_labels.json` | `category_labels.json` 直接填算子 `categoryLabels`；`dataset.yaml` + `labels/` 用于 Ultralytics/YOLO 训练 |
| `yolo_seg` | 同上 + `masks/{train,val}/*.png`（可选） | `SegmentDl` 训练数据 |
| `classification` | `train/<类名>/`、`val/<类名>/`、`labels.json` | `AiClassify` 训练数据 |
| `ocr` | `images/`、`labels/<图>.json`（bbox+text）、`gt.txt` | `DLOCR` / `Ocr` 训练数据 |
| `coco` | `annotations/instances_{train,val}.json` | 与 **Label Studio / CVAT** 互通（可回灌再导入） |
| `qdvann` | 单个 `.qdvann` 工程文件 | 原生闭环：再次「打开」即可续标 |

**关键约定**：YOLO 归一化坐标为 `(cx,cy,w,h)` 绝对比例；`category_labels.json` 为
纯字符串数组，与 `operators.json` 中算子 `categoryLabels` 字段同序；`labels.json`
与 `models/labels.json` 同构。

---

## 7. 能力注册表（capabilities.json）—— 改配置即扩展

新增形状 / 导出格式 / 训练任务，**只需编辑 `capabilities.json`**，无需改动任何 C++/QML 分派代码。
`ShapeRegistry` 启动时加载（优先级：环境变量 `QDV_CAPABILITIES` → exe 目录回溯 6 层 → 当前工作目录 → `qrc:/capabilities`），暴露为 QML 全局属性 `capabilities`。

三种“入口均读注册表”：

| 能力段 | 数组 | 被驱动的入口 |
|------|------|------|
| `shapes` | 形状元数据 | 工具栏绘制按钮（`enabled`+`overlayType`+`label`）、画布几何分派（`geo`）、标注列表显示名（`label`） |
| `exports` | 导出格式 | `ExportDialog` 导出格式下拉（`label`）、导出形状校验（`needsShape`） |
| `tasks` | 训练任务 | `TrainingDialog` 模型下拉（`taskModels`）、任务默认工具（`defaultShape`）、任务选择下拉与状态文案（`label`） |

> `geo` 取值 `box`（矩形族，`x/y/w/h`）、`rotated_box`（方向矩形族，`x/y/angle/length1/length2/count`）、`polygon`（多边形/自由形状族，`points`）。
> 同一 `overlayType` 的多个形状共享绘制实现，靠 `activeShapeKey` 精确落 key（如 `freeform` 与 `polygon` 都走 `polygon` 绘制，但存成不同形状）。

**新增一个形状的最小配置**（例如启用自由形状实现分割）：
```json
{ "key": "freeform", "label": "自由形状", "geo": "polygon", "overlayType": "polygon",
  "props": [{"name":"points","type":"points"}], "enabled": true }
```

---

## 8. 更深层扩展（改代码才需要）

- **新增绘制实现**：`AnnotateOverlay.qml` 的 `activeTool` 分派增加一种全新的 `overlayType` 绘制（现有 `rect/polygon/vec_rect` 之外的几何）。
- **新增导出器**：在 `AnnotationSession::exportDataset` 增加 `format` 分支，复用 `Qdv::` 命名空间辅助函数（`normRect` / `planSubsets` / `writeLabelsJson` 等）。
- **新增导入器**：实现同类 `importXxx()`，统一写入 `images` 列表（坐标均为图像像素）。
- **协作/自动化**：导出的 COCO 可被 Label Studio/CVAT 多人标注后，用「导入 COCO」回灌，实现"单机标注 + 云端协作"混合工作流。

---

## 9. 格式验证

`verify/verify_export.py` 在真实项目图像（`测试图/*.bmp`，1200×1200 8bpp 灰度工业件）上
1:1 移植导出逻辑并断言格式正确性，结果 **10/10 通过**：

```powershell
python tools/qdv-annotator/verify/verify_export.py
```

覆盖：labels.json 同构、category_labels 纯数组、YOLO 坐标归一化、分割多边形、
分类目录布局、OCR json/gt.txt、COCO 结构、`.qdvann` 闭环。

---

## 10. 验证状态

| 验证项 | 方法 | 结果 |
|------|------|------|
| **独立构建** | `cmake -S . -B build` + `cmake --build` | 零 error、零 warning；产出 `build/QDVAnnotator.exe`（约 1.0 MB） |
| **QML 解析** | `qmllint`（含 `QML_IMPORT_PATH=项目 qml/`） | 7 个 QML 文件无解析错误（修复了 `LabelPanel.qml:90` 字符损坏） |
| **缺失依赖修复** | 构建报错回溯 | 补 `Qt6::Widgets`（`QApplication` 所在模块）；`remove_definitions(-DQT_NO_CAST_FROM_ASCII)` 抵消主工程宏 |
| **运行时冒烟** | `QT_QPA_PLATFORM=offscreen` 启动 + `windeployqt` 部署 | 进程正常进入 Qt 事件循环（`Main.qml` 加载、`QDV.EditView` 模块解析成功，未触发 `exit(-1)` 失败路径） |
| **主工程集成** | 根 `CMakeLists.txt` 追加 `add_subdirectory(tools/qdv-annotator)`，整工程 configure + 仅构建 `QDVAnnotator` 目标 | 整工程 configure 通过；子目标在主工程上下文（继承 `QT_NO_DEBUG` 等）下编译/链接零错误，无目标名冲突 |
| **导出格式兼容** | `verify/verify_export.py` 在真实 `测试图/` 上实跑 | **10/10 通过**（格式可被现有算子直接消费） |
| **能力注册表（S0/S1）** | `cmake --build` + 冒烟加载 `capabilities.json` | 构建 exit 0；注册表从文件加载；工具栏按注册表动态生成「矩形/方向矩形/多边形/自由形状」；冒烟无 QML 报错 |

> 注：构建产物 `build/` 含 `windeployqt` 部署的 Qt DLL（约 120 MB），属可再生成产物；
> 若需重新生成运行包：`windeployqt build/QDVAnnotator.exe --qmldir qml`。
> 运行时复用 `QDV.EditView` 模块：**从可执行文件目录向上回溯自动定位仓库根 `qml/`**，
> 因此从任意目录启动均可命中；亦可用环境变量 `QDV_QML_IMPORT=E:/anchor/Trae/QDV/qml` 强制指定。
