# 与外部程序对接的接口规则说明

> 规范 QDVAnnotator 与外部程序（当前为**单丝项目**：`annotator_rect2.html` 标注页 +
> `auto_label_server.py` 的 8765 自动标注服务）之间的**数据契约与接口规则**。
> 两端任何一处改动必须回到本文档校验，否则会破坏「训练—标注闭环」。
>
> "唯一真相表"：以下字段、单位、目录结构、HTTP 路由为最低共识，禁止单方面偏离。

- 本地端：`E:\anchor\Trae\QDV\tools\qdv-annotator\`
- 外部端：`E:\anchor\WorkBuddy\moren\单丝计数V2.0\`

---

## 1. 数据契约总览

跨程序流转三类数据：`vec_rect` 标注、`.qdvann` 工程、`yolo_obb` 数据集。它们共同保证
**坐标模型、类别语义、路径解析**三方对齐。

### 1.1 `vec_rect` 标注（annotator_rect2 ⇄ QDVAnnotator）

方向矩形（有向矩形）：存**中心** + 半长/半宽 + 方向角 + 根数。

| 字段 | 来源 | 精度/约束 |
|---|---|---|
| `shape` | 恒 `"vec_rect"` | — |
| `labelId` | labels 数组下标（按 `count` 匹配，无匹配=0） | int |
| `x`,`y` | 中心像素坐标 | `round()`，int |
| `angle` | 方向角（**度**） | `round(×10)/10` |
| `length1`,`length2` | 半长 / 半宽（像素） | `round(×10)/10`，`clamp≥1` |
| `count` | 根数（1/2/3/4），决定类别与配色 | int |

> 单位红线：`angle` 是"度"非弧度；`length1/2` 是像素半长。不可改单位，历史曾因角度单位偏移导致
> 自动标注箭头方向对不上。

### 1.2 `.qdvann` 工程文件

```
{version, app:"QDV Annotator", taskType,
 labels[], labelColors[],
 images[{id, path, fileName, width, height, subset, annotations[], classLabels[]}]}
```

- `path` **必须是完整 Windows 路径（反斜杠）**。浏览器不暴露完整路径，故由 `exportQdv()`
  强制用户填写「图像目录路径」，未填则阻止导出；QDV 侧 `resolveImagePath()` 有子目录回退。
- `annotations[]` 内的标注按 1.1 的 `vec_rect`（或 `rect`/`polygon` 族）形状字段存储。

### 1.3 `yolo_obb` 训练数据集

```
images/{train,val}/          labels/{train,val}/*.txt
```
- 每行：`class x1 y1 x2 y2 x3 y3 x4 y4` —— **逆时针(CCW)、0–1 归一化、clamp**。
- `class = count - 1`（根数 1→class 0，根数 4→class 3）。
- 附带：`dataset.yaml`（`nc`/`names`）、`labels.json`、`category_labels.json`、`dataset_report.json`。
- **与 `obb_dataset/` 及 `auto_label_server.py` 的 OBB 推理算法规格完全一致**（回灌可行的前提）。

---

## 2. 外部 HTTP 接口（8765 自动标注服务）

服务：`auto_label_server.py`，HTTP 监听 `127.0.0.1:8765`（`ThreadingHTTPServer`）。
所有 POST 请求体为 **UTF-8 JSON**。

### 2.1 `GET /api/health` — 健康检查

- 返回：服务状态、`obb_model_path`（当前生效权重路径）、`obb_weights_found`（是否存在）。
- 用途：判断服务是否就绪、权重是否在位。

### 2.2 `POST /api/auto_label` — 单类端点自动标注（已有）

- 对图像执行单类目标自动标注，返回标注结果（轴对齐框）。

### 2.3 `POST /api/auto_label_rect2` — 有向矩形(OBB)自动标注

- body 与 `/api/auto_label` 相同（含置信度、图像尺寸等参数；`conf` 默认 `0.25`，`imgsz` 默认 `960`）。
- 返回 **`vec_rect` 列表**，每项：`{cx, cy, w, h, angle, count, score, corners}`。
  - `angle` 单位为**度**（服务端 `math.degrees()`，修复自弧度 bug）；
  - `count` 由推理类别经阈值判定映射回根数；
  - `corners` 为四角点数组（便于前端绘制）。
- 供 `annotator_rect2.html` 的「🟦 有向矩形自动标注(OBB)」按钮调用。

### 2.4 `POST /api/pseudo_label` — 伪标签

- 将服务端推理结果作为伪标签回写/合并到当前标注（弱监督辅助）。

### 2.5 `POST /api/merge_labels` — 合并标签

- 合并/清洗标签集合（类别语义对齐用）。

### 2.6 `POST /api/load_obb_weight` — 权重热加载（回灌关键）

```
POST /api/load_obb_weight
Content-Type: application/json
{ "path": "E:/anchor/WorkBuddy/.../runs/obb/current/weights/obb_current.onnx" }
```

- 服务端据 `path` **显式**加载该权重（超参热更，不重启即可切换），并刷新 `OBB_MODEL_PATH`
  与 `/api/health` 展示。
- `path` 使用**正斜杠**（脚本内 `%DST:\=/%` 转换）。
- 若服务未运行，请求失败，脚本仅告警不中断（权重落盘后下次启动自动加载）。

> 约定：所有接口统一 JSON、UTF-8；失败用 HTTP 非 2xx + 错误体；无自定义 token/鉴权（内网工具）。

---

## 3. 权重定位与热切换

### 3.1 `OBB_MODEL_PATH` 探测优先级（`_resolve_obb_model_path`）

1. 环境变量显式指定的 OBB 权重路径
2. 专用回灌目录 `runs/obb/current/weights/obb_current.*`（**最高优先**, QDVAnnotator 重训产物的落点）
3. 历史迭代目录 `runs/obb/obb_it{1,2}/weights/obb_current.*`（按**修改时间倒序取最新**，非字符串排序）
4. 默认路径兜底

> 历史事故：早期按路径字符串排序（`obb_it2 > obb_it1`），新回灌到 `obb_it1` 的权重永不生效。
> 故固定专用目录 `current/weights/` 为回灌落点，规避排序歧义。

### 3.2 一键回灌 `sync_obb_weight.bat`

```
用法：sync_obb_weight.bat  [QDVAnnotator训练输出目录]
      （无参则拖拽目录到窗口）
流程：
  1. 从源目录取最新的 best*.onnx（无 onnx 回退 best*.pt）
  2. copy 到 runs\obb\current\weights\obb_current.<ext>
  3. curl POST /api/load_obb_weight {path:".../obb_current.<ext>"} 热加载
  4. 服务未运行 → 告警并结束，权重留待下次启动自动加载
```

---

## 4. 本地端对外持久化（capabilities 只读加载）

外部程序（如脚本/巡检）如需读取 QDVAnnotator 的能力清单，无需解析内部结构，
直接按**能力注册表加载优先级**读外置 `capabilities.json`：
`QDV_CAPABILITIES` 环境变量 → exe 目录回溯 6 层 → 当前工作目录。字段含义见《能力注册表使用手册》。

---

## 5. 兼容性红线（防事故清单）

1. **保持 `vec_rect` / `yolo_obb` 桥接**：别改用 `yolo_detect` 导出方向矩形（曾致 177 个 0 字节空标签、
   mAP=0、废掉 `数据集qdv`）。检测类导出遇 `vec_rect` 应明确拒绝并提示用 `yolo_obb`。
2. **类别三角同构**：`count`(根数) / `labelId` / 训练 `class` 必须一致——`class = count - 1`，
   `labelId` 按 `count` 匹配，`category_labels.json` 同序。
3. **角度单位统一为「度」**，服务端已修复弧度→度。
4. **Windows 路径**：`path` 用反斜杠完整路径；回灌 API 的 `path` 服务端按正斜杠友好解析。
5. **路径排序**：权重取最新必须按修改时间，禁用字符串排序。
6. **格式衔接**：QDVAnnotator 出 `.onnx`，服务 `ultralytics.YOLO` 支持 `.onnx`（需环境含 `onnxruntime`）
   与 `.pt`。切换后以 `/api/health` 的 `obb_weights_found` 确认加载。
7. **改动校验**：任何一端改了字段/路由/单位/目录，先在此文档对齐，再回归 `verify/verify_export.py`
   与 `_verify_exported_obb.py`。

---

## 6. 相关实现文件索引

| 端 | 文件 | 角色 |
|---|---|---|
| 本地 | `tools/qdv-annotator/src/AnnotationSession.cpp` | 导入导出调度（yolo_obb / vec_rect / qdvann） |
| 本地 | `tools/qdv-annotator/src/ShapeRegistry.{h,cpp}` | 能力只读加载器 |
| 本地 | `tools/qdv-annotator/verify/verify_export.py` / `_verify_exported_obb.py` | 导出/回馈校验 |
| 外部 | `scripts/auto_label_server.py` | 8765 服务（全部 `/api/*` 端点） |
| 外部 | `scripts/sync_obb_weight.bat` | 一键回灌 + 热加载 |
| 外部 | `annotator_rect2.html` | 标注页（exportQdv / OBB 自动标注） |