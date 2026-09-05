# 能力注册表使用手册（QDVAnnotator）

> 面向 **_后续维护者_** 的实操手册：如何只改 `capabilities.json` 就新增标注形状 / 导出格式 / 训练任务，
> 无需改动任何 C++/QML 分派代码。适用于 QDVAnnotator v-next（S0/S1 已落地）。

- 配套实现：`src/ShapeRegistry.h/.cpp`、`main.cpp`、`capabilities.json`、`capabilities.qrc`
- 本手册配套《与外部程序对接的接口规则说明》

---

## 1. 定位与设计目标

能力注册表是 QDVAnnotator 的**可插拔能力描述文件**，统一管理三类能力：

| 能力段 | 含义 | 驱动哪些 UI/逻辑 |
|------|------|------|
| `shapes` | 标注形状元数据 | 工具栏绘制按钮、画布几何分派、标注列表显示名 |
| `exports` | 导出格式 | 导出对话框格式下拉、导出形状校验 |
| `tasks` | 训练任务 | 训练对话框模型下拉、任务默认工具、任务下拉与状态文案 |

**核心约定**：加能力 = 改配置；改配置 = 生效。主体代码的分派逻辑只认注册表，不认识具体形状。

---

## 2. 配置查找与生效（加载优先级）

`ShapeRegistry` 启动时按以下顺序**首次命中即用**：

1. 环境变量 `QDV_CAPABILITIES`（指向绝对路径）
2. 可执行文件目录**向上回溯 6 层**内的 `capabilities.json`
3. 当前工作目录的 `capabilities.json`
4. `qrc:/capabilities/capabilities.json`（随 exe 打包的兜底副本）

> **生效方式**：把新版本的 `capabilities.json` 放到 exe 旁边（回溯命中），重启软件即生效，**无需重编译**。
> 开发/联调时，改源码目录那份即可（开发模式会通过主程序 `../capabilities.json` 回退命中）。

---

## 3. 文件结构逐字段说明

### 3.1 `shapes[]`（形状）

| 字段 | 必填 | 说明 |
|------|------|------|
| `key` | ✅ | 形状唯一标识（如 `rect`/`vec_rect`/`polygon`/`freeform`），存进标注对象的 `shape` |
| `label` | ✅ | 中文显示名（工具栏按钮文字、列表显示名） |
| `geo` | ✅ | 几何族：`box`（x/y/w/h）、`rotated_box`（方向矩形）、`polygon`（多边形/自由形状） |
| `overlayType` | ✅ | 覆盖层绘制类型。**决定走哪个绘制实现**（`rect`/`vec_rect`/`polygon`） |
| `props` | 建议 | 几何属性声明（`{name,type}`），作为数据契约文档，不参与逻辑 |
| `tasks` | 选填 | 适用任务（如 `["obb"]`）。当前**仅做数据记录**，不再控制按钮显隐 |
| `enabled` | 选填 | `false` 则不在工具栏出现；缺省视为 `true` |
| `comment` | 选填 | 说明注释，不影响逻辑 |

> **关键机制**：`geo` 决定数据字段映射，`overlayType` 决定绘制实现，二者解耦。
> 同一 `overlayType` 可登记多个形状（如 `polygon` 与 `freeform` 都走 `polygon` 绘制族），
> 靠 **`activeShapeKey`**（当前工具对应的形状 key）精确区分落库，避免反查串类。

### 3.2 `exports[]`（导出格式）

| 字段 | 必填 | 说明 |
|------|------|------|
| `key` | ✅ | 导出格式标识（对应 `AnnotationSession::exportDataset` 的 format 分支） |
| `label` | ✅ | 导出下拉显示文案 |
| `needsShape` | 选填 | 该格式允许的形状；**空 = 接受任意/不校验** |
| `norm` | 选填 | 归一化方式约定（`xywh`/`ccw8`/`poly_px`/`class`/`ocr`/`coco`/`qdvann`） |
| `tasks` | 选填 | 适用任务 |

> 注意：`exports` 只是"能力清单 + 展示"，**真正的导出逻辑仍由 `exports[key]` 对应 C++ 分支实现**。
> 只加 `exports` 项不会自动产生导出能力（若后端无该 format 分支会导出失败）。图标细节见 §5.2。

### 3.3 `tasks[]`（训练任务）

| 字段 | 必填 | 说明 |
|------|------|------|
| `key` | ✅ | 任务标识（对应 `session.taskType`，如 `detection`/`obb`/`segmentation`/`classification`/`ocr`） |
| `label` | ✅ | 任务下拉与状态栏显示名 |
| `defaultShape` | 选填 | 切换该任务时的默认绘制形状；空 = 不自动设默认工具 |
| `models` | 选填 | 可训练模型清单（训练对话框据此生成下拉，`obb`+`detection` 两组拼装） |

---

## 4. 对外查询接口（QML 全局属性 `capabilities`）

`main.cpp` 将 `ShapeRegistry` 注入为根上下文属性 `capabilities`，QML 任一处可直接使用：

| 方法 | 返回 | 说明 |
|------|------|------|
| `capabilities.shapes` / `exports` / `tasks` | `QVariantList` | 原始三段数组 |
| `loaded` / `source` | bool / String | 是否加载成功 / 来源路径 |
| `hasShape(key)` | bool | 是否登记某形状 |
| `overlayTypeOf(key)` | String | 某形状的绘制类型（未登记回退 `"rect"`） |
| `isShapeEnabled(key)` | bool | 是否启用（缺省 true） |
| `shapeInfo(key)` | QVariantMap | 单形状详情（未登记回退空 map） |
| `supportsShape(format, shape)` | bool | 某导出格式是否接受某形状 |
| `exportNeedsShapes(format)` | StringList | 某导出格式需要的形状清单 |
| `taskDefaultShape(task)` | String | 某任务默认形状 |
| `taskModels(task)` | StringList | 某任务可训练模型 |

---

## 5. 实操：三步扩展

### 5.1 新增一个标注形状

例：启用复用 `polygon` 绘制的「自由形状」做分割（当前已存在，作为模板）：

```json
{ "key": "freeform", "label": "自由形状", "geo": "polygon", "overlayType": "polygon",
  "props": [{"name":"points","type":"points"}], "tasks": ["segmentation"], "enabled": true }
```

- 该形状立即出现在工具栏（默认所有已启用且可绘制形状**常显**，不做任务过滤）。
- 画它时 `activeTool="polygon"`、`activeShapeKey="freeform"`，画布将其存为 `shape:"freeform"`、几何 `points[]`。
- 列表显示名自动用 `label`（"自由形状"）。

**新增一种全新几何族**（如 `box`/`rotated_box`/`polygon` 之外的曲线）：需在
`AnnotateOverlay.qml` 实现该 `overlayType` 的绘制，并在 `AnnotationCanvas.qml` 的几何分派中
补该 `geo` 的字段映射（属"更深层扩展"，见 README §8；超出改配置范围）。

### 5.2 新增一个导出格式

若后端已实现该 `format` 分支，只登记即可让其出现在下拉：

```json
{ "key": "yolo_obb", "label": "YOLO 有向矩形 OBB (yolo_obb) → 旋转矩形/RotatedDetectDl",
  "needsShape": ["vec_rect"], "norm": "ccw8", "tasks": ["obb"] }
```

> 后端暂未实现时，`ExportDialog` 会列出但导出会失败——建议此时用 `comment` 标注"待后端实现"，
> 或直接不登记，避免误选。

### 5.3 新增一个训练任务

在 `tasks[]` 增加：

```json
{ "key": "pose", "label": "姿态估计", "defaultShape": "keypoint", "models": ["yolov8n-pose"] }
```

- 任务下拉、状态栏文案自动出现；切换后 `applyTaskDefaults()` 会设默认工具为 `keypoint`。
- 该任务的可训练模型出 `models`。
- 若该任务的新形状/导出也要配，再在 `shapes`/`exports` 补记录即可。

---

## 6. 构建与验证

能力注册表改动**无需重新编译**（运行时外置加载优先）。如需重新打包 exe 内的兜底副本：
`cmake --build build -j 8`（`capabilities.qrc` 会重新 rcc）。

快速验证：

```powershell
cd E:\anchor\Trae\QDV\tools\qdv-annotator
cmake --build build -j 8
.\build\QDVAnnotator.exe ..\verify\project.qdvann   # 观察 stderr 出现
#  [ShapeRegistry] 已从文件加载能力注册表: ...capabilities.json
```

若 stderr 出现 `ReferenceError: capabilities is not defined` 或 `[ShapeRegistry] 未找到...`，
说明配置未命中（检查加载优先级）或 Q_INVOKABLE 接口名对不上。

---

## 7. 注意事项（红线）

1. `key` 必须全局唯一；勿与既有形状重名，否则 `shapeInfo`/`hasShape` 会覆盖。
2. 改 `overlayType` 前确认 `AnnotateOverlay.qml` 有对应绘制分支；否则该形状无法绘制。
3. QC 数据契约：`vec_rect` 的 `angle` 为**度**、`length1/2` 为像素半长（可小数），勿改单位，防止与
   外部 `annotator_rect2.html`、`auto_label_server.py` 二次偏移（历史事故）。
4. `defaultShape` 指向的 `key` 必须存在于 `shapes` 且可绘制，否则 `applyTaskDefaults` 会自动退化为
   第一个可绘制形状。
5. `tasks` 段仅作记录，不等于"只有该任务才能画该形状"——工具栏形状按钮常显。
6. **删除形状需谨慎**：已存在的 `.qdvann` 标注若引用已删 `key`，列表显示名会回退为原始 `key` 字符串。