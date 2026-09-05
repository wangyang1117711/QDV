# 单丝计数「标注 ⇄ 训练」闭环整合设计方案（QDVAnnotator 版）

> 生成时间：2026-08-19 | 状态：方案版（可执行）
> 依据：3 份交接文件 ——
> ① `E:\anchor\WorkBuddy\moren\单丝计数V2.0\交接文件_2026-08-19.md`（OBB 自动标注链路）
> ② `E:\anchor\Trae\QDV\tools\qdv-annotator\QDVAnnotator_交接文件_显示优化与训练闪退_2026-08-19.md`
> ③ `E:\anchor\Trae\QDV\tools\qdv-annotator\QDVAnnotator_交接文件_训练能力与入口_2026-08-19.md`
> 目的：把「annotator_rect2.html 标注 → .qdvann → QDVAnnotator 导入训练 → 重训数据回灌 → 自动标注升级」做成完整、可执行的零代码闭环。

---

## 0. 结论速览（先给答案）

- **可行，且主路径已具备 ~90% 能力**。三份交接里的遗留缺口收敛为 **3 件关键事**：
  1. **R1** 真机验收新版 `QDVAnnotator.exe`（资源自适应后不再闪退）；
  2. **R4/T2** OBB（有向矩形）端到端真机训练跑通验证；
  3. **T6** 训练产物「一键部署回灌」到 8765 自动标注服务（当前唯一缺的自动化环节）。
- **推荐方案**：路径 A —— 全部走 QDVAnnotator GUI（打开 .qdvann → 导出 `yolo_obb` → 标训闭环训练）+ **单丝项目侧一键回灌脚本**（QDV 零改动）。
- **三条红线**（违反即翻车）：
  - 训练 Python 必须用 **QDV venv**：`E:/anchor/Trae/QDV/training/venv/Scripts/python.exe`（全局 `D:/Program Files/Python314` 的 torch import 会死锁，不可用于 QDVAnnotator 训练）；
  - Windows **页面文件须 ≥16GB**（否则训练被系统强杀 → GUI 闪退）；
  - **`数据集qdv/` 已废**（标签全空、模型 mAP=0），任何环节不得再作为数据源。

---

## 1. 目标与约束

| 用户目标 | 约束要求 | 本方案落地口径 |
|---|---|---|
| 标注数据 → `.qdvann`/其他格式 | ① **零代码操作** | 全程 GUI 点选 + 双击 `.bat`；新改动只藏在单丝项目侧脚本内，使用者不接触命令 |
| QDVAnnotator 导入并训练 → 重训数据 | ② **项目整合优化** | §3 数据契约唯一真相表 + §4 兼容性分析（含 `数据集qdv` 空标签事故教训） |
| 重训数据回灌 → 自动标注升级 | ③ **QDV 安全性保障** | QDVAnnotator 改动限 `tools/qdv-annotator` 独立目录；**QDV 主程序零改动**；回灌逻辑放单丝项目侧 |

---

## 2. 现状盘点（三份交接汇总）

### 2.1 单丝项目侧（交接①）
| 能力 | 状态 | 位置 |
|---|---|---|
| 有向矩形(OBB)自动标注按钮 | ✅ 已内置 | `annotator_rect2.html` `btnAutoLabelRect2`→`autoLabelRect2()`→`makeOBBBox()` |
| 服务端 `/api/auto_label_rect2`（OBB 推理，懒加载） | ✅ 已内置 | `scripts/auto_label_server.py`（端口 8765） |
| OBB 训练管线（noshim + workers=0 双坑修复） | ✅ 已验证 | `scripts/train_obb_noshim.py` / `train_obb_worker.py` |
| OBB 首轮模型 | ✅ 已产出（21:20，60 epochs） | `runs/obb/obb_it1/weights/best.pt`（6.5MB） |
| `.qdvann` 5346 条人工精标 | ⏳ 待接入 | `C:\Users\wangy\Downloads\rect2_qdv_project (177).qdvann` |
| **服务当前未加载新 OBB 权重**（旧实例 16:54 启动） | ⚠️ 高优先 | PID 17604/29216，health `obb_loaded=false` |

### 2.2 QDVAnnotator 侧（交接②③）
| 能力 | 状态 | 位置 |
|---|---|---|
| 导入 `.qdvann` 工程（vec_rect 读回） | ✅ | `ImportWizard.qml` / `openProject()` |
| 导出 `yolo_obb` 数据集（4 角点 CCW 归一化，class=count-1） | ✅ | `ExportDialog.qml` / `AnnotationSession::exportDataset` |
| 空标签防护（yolo_detect 遇 vec_rect 拒导并提示走 yolo_obb） | ✅ | `AnnotationSession.cpp` L598-610 |
| 标训闭环入口（exportAndTrain / trainFromDataYaml） | ✅ | `TrainingDialog.qml` / `TrainingBridge.cpp` |
| 可训练模型（OBB + 轴对齐） | ✅ | yolov8n-obb / yolov8s-obb / yolov11n-obb；yolov8n/s/m；yolov5n/s |
| 界面显示优化（WideComboBox 等 8 处） | ✅ | `qml/QDVAnnotator/WideComboBox.qml` |
| 训练资源自适应（显存/页面文件降档、workers=0） | ✅ | `training/yolo_train.py` `_tune_resources()` |
| **训练产物** | ✅ | 输出目录：`best<时间戳>.onnx` + `labels.json` |

### 2.3 关键遗留待办（合并去重）
| # | 事项 | 优先级 | 来源 |
|---|---|---|---|
| R1 | 真机用新版 exe 重跑「标训闭环」，确认不再闪退 | 高 | 交接② |
| R4/T2 | OBB 端到端真机训练跑通（含页面文件根治） | 高 | 交接②③ |
| T6 | **训练产物一键部署回灌 8765 服务**（本方案重点补齐） | 高 | 交接② |
| T1(单丝) | 重启 8765 服务加载新 best.pt，验证 OBB 按钮 | 高 | 交接① |
| T4(单丝) | 决策 `.qdvann` 精标接入（替换 vs 追加） | 低 | 交接① |

---

## 3. 数据契约（唯一真相表，两端不得偏离）

### 3.1 `vec_rect` 标注（annotator_rect2 ⇄ QDVAnnotator）
| 字段 | 来源 | 精度/约束 |
|---|---|---|
| `shape` | 恒 `"vec_rect"` | — |
| `labelId` | labels 数组下标（按 count 匹配，无匹配=0） | int |
| `x`,`y` | 中心像素 | round()，int |
| `angle` | 方向角（度） | round(×10)/10 |
| `length1`,`length2` | 半长/半宽（像素） | round(×10)/10，clamp≥1 |
| `count` | 根数（1/2/3/4） | int，决定类别与配色 |

### 3.2 `.qdvann` 工程文件
`{version, app:"QDV Annotator", taskType, labels[], labelColors[], images[{id, path, fileName, width, height, subset, annotations[], classLabels[]}]}`
- `path` 必须是完整 Windows 路径（反斜杠）。`exportQdv()` 已强制用户填写「图像目录路径」，未填则阻止导出。

### 3.3 `yolo_obb` 训练数据集
- `images/{train,val}/` + `labels/{train,val}/*.txt`
- 每行 `class x1 y1 x2 y2 x3 y3 x4 y4`（CCW、0–1 归一化、clamp）；`class = count - 1`
- 附带 `dataset.yaml`（nc/names）、`labels.json`、`category_labels.json`、`dataset_report.json`
- **与 `obb_dataset/` 及 `auto_label_server.py` 的 OBB 推理算法规格完全一致**（回灌可行的前提）。

### 3.4 回灌接口
- `POST :8765/api/auto_label_rect2` → 返回 `vec_rect` 列表（`{cx,cy,w,h,angle,count,score,corners}`）
- 服务端权重加载：`ultralytics.YOLO(OBB_MODEL_PATH)`（支持 `.pt`；`.onnx` 亦可行，需环境含 onnxruntime）

---

## 4. 接口差异与兼容性分析（含事故教训）

| # | 差异点 | 兼容问题 | 处置（现状） |
|---|---|---|---|
| 1 | 坐标模型：HTML「中心+半长半宽+角」 vs QDV 轴对齐 x,y,w,h | **事故**：曾走 yolo_detect 导出 → 177 个 0 字节空标签 → mAP=0 → 废掉 `数据集qdv` | 已用 `vec_rect` + `yolo_obb` 桥接；yolo_detect 遇 vec_rect 拒导 |
| 2 | 路径：浏览器不暴露完整路径 vs QDV 需绝对路径 | `.qdvann` path 裸文件名 → 图找不到 | `exportQdv()` 强制填目录 + 反斜杠；QDV 侧 `resolveImagePath()` 有子目录回退 |
| 3 | 类别语义：count(根数) / labelId / 训练 class | 三者需同构 | `labelId` 按 count 匹配；`yolo_obb` `class=count-1`；`category_labels.json` 同序 |
| 4 | 训练任务：QDV 主程序仅分类 | 主程序无法训检测/OBB | 绕开：QDVAnnotator 经 TrainingBridge 拉起 `training/yolo_train.py`（工具级闭环，不侵入主程序） |
| 5 | 训练环境：QDVAnnotator 用 QDV venv | 全局 Python314 torch import 死锁；页面文件不足强杀闪退 | 已由 `_tune_resources` 自动降档 + workers=0；**根治需用户扩页面文件** |
| 6 | 产物格式：QDVAnnotator 出 `.onnx`，单丝服务加载 `.pt` | 回灌需格式/路径衔接 | **T6 一键回灌**（§7）：复制 onnx → 更新 `OBB_MODEL_PATH` → 重启服务（ultralytics.YOLO 可载 onnx） |
| 7 | 图像重叠：`.qdvann` 177 图与 `obb_dataset` mvtec 100% 重合 | 追加=重复训练 | 作「人工精标替代标注」，替换而非增量（待用户拍板 T4） |

---

## 5. 闭环总图（端到端 5 阶段）

```
┌─────────────────────────── 单丝项目（业务主场）───────────────────────────┐
│ 阶段1  标注 annotator_rect2.html                                          │
│        加载图 → 人工标注有向矩形（可先用自动标注预标）                        │
│        「导出 QDV 工程」 → rect2_qdv_project.qdvann（vec_rect）            │
└───────────────┬──────────────────────────────────────────────────────────┘
                │ 下载到用户目录
                ▼
┌─────────────────────────── QDVAnnotator.exe ─────────────────────────────┐
│ 阶段2  打开工程(.qdvann) → 可视化校验/补标 → 保存回 .qdvann                 │
│ 阶段3  导出数据集：ExportDialog 选「YOLO 有向矩形 OBB」→ yolo_obb 数据集     │
│        标训闭环：数据集来源「使用已导出的OBB数据集」→ dataset.yaml           │
│                 模型 yolov8n-obb → Python=QDV venv → 训练                   │
│        产物：best<时间戳>.onnx + labels.json（输出目录）                    │
└───────────────┬──────────────────────────────────────────────────────────┘
                │ 一键回灌（T6）
                ▼
┌─────────────────────────── 单丝项目 ─────────────────────────────────────┐
│ 阶段4  sync_obb_weight.bat：复制最新 best*.onnx → 单丝 runs/…/weights/     │
│        更新 OBB_MODEL_PATH（约定文件名）→ 重启 8765 服务                    │
│ 阶段5  annotator_rect2.html 点「🟦 有向矩形自动标注(OBB)」                  │
│        → 升级后自动标注 → 人工修正 → 导出 → 进入下一轮训练 → 循环           │
└──────────────────────────────────────────────────────────────────────────┘
```

---

## 6. 可执行分步方案（零代码操作手册）

### 阶段 0 — 环境就绪（一次性，负责人：用户）
1. **扩页面文件**：系统属性 → 高级 → 性能 → 虚拟内存 → 设为「系统管理」或 ≥16GB。（根治训练闪退）
2. 确认 QDV venv 存在：`E:/anchor/Trae/QDV/training/venv/Scripts/python.exe`（含 torch 2.7.1+cu128 / ultralytics）。
3. 确认 8765 无残留实例：任务管理器结束旧 `auto_label_server`（PID 17604/29216 等）。
4. 双击 `启动自动标注.bat` 启动服务，浏览器验证 `http://localhost:8765/` 打开标注页，health 中 `obb_loaded=true`。

### 阶段 1 — 标注与导出（annotator_rect2.html，纯 GUI）
1. 「🖼️ 选择图片文件夹」加载新图（或本地测试图集）。
2. 人工标注：拖拽有向矩形（方向 + 根数 1/2/3/4），或点「🟦 有向矩形自动标注(OBB)」预标后人工修正。
3. 在「图像目录路径」输入框填写图片所在目录（**必须**，例如 `E:\anchor\WorkBuddy\moren\单丝计数V2.0\images\训练图\`）。
4. 点「导出 QDV 工程」→ 得到 `rect2_qdv_project.qdvann`。

### 阶段 2 — QDVAnnotator 导入与补标（纯 GUI）
1. 双击 `build\QDVAnnotator.exe`。
2. 顶栏「导入 → 打开工程」选 `.qdvann` → 画布应显示图片 + 有向矩形（旋转 + 箭头 + 根数配色）。
3. 人工校验/补标 → `Ctrl+S` 保存回 `.qdvann`。

### 阶段 3 — 导出 OBB 数据集并训练（纯 GUI）
1. 顶栏「导出」→ 格式选 **「YOLO 有向矩形 OBB (yolo_obb)」** → 选输出目录 → 导出。
   - 产物：`images/ labels/ dataset.yaml labels.json category_labels.json dataset_report.json`。
   - 若曾误选轴对齐格式，会看到提示「检测到旋转(vec_rect)标注，请改用 yolo_obb」——照做即可。
2. 顶栏「标训闭环」→ 对话框配置：
   - 数据集来源：**「使用已导出的 OBB 数据集 (yolo_obb)」** → 浏览选上一步的 `dataset.yaml`；
   - 输出目录：新建空目录（模型放这）；
   - Python 路径：**`E:/anchor/Trae/QDV/training/venv/Scripts/python.exe`**（先点「检测」确认通过）；
   - 模型：`yolov8n-obb`（8GB 显存首选，快；追求精度可 `yolov8s-obb`）；
   - 批大小默认 8（脚本会按显存/页面文件自动再降档）、轮数 60、图像尺寸 640。
3. 点「开始标训闭环」→ 观察进度/日志；**不要关窗口**（资源被系统强杀会闪退，页面文件已扩容则安全）。
4. 完成 → 输出目录出现 `best<时间戳>.onnx` + `labels.json`。

### 阶段 4 — 一键部署回灌（见 §7）
### 阶段 5 — 增量迭代（纯 GUI）
1. 回到 `annotator_rect2.html`（服务已重启）→ 加载图 → 点「🟦 有向矩形自动标注(OBB)」→ 升级后的模型自动出带角度框（φ + 根数）。
2. 人工修正 → 「合并到训练集」或导出 `.qdvann` → 进入下一轮训练 → 循环。

---

## 7. 部署回灌一键化设计（关键补齐项 T6）

> 设计原则：**不改 QDVAnnotator 内部**，回灌逻辑全部放单丝项目侧，使用者只需双击一个 `.bat`。

### 7.1 方案 A（推荐）：`sync_obb_weight.bat`（单丝项目侧，约 10 行）
```bat
@echo off
REM 一键回灌：把 QDVAnnotator 最新训练产物部署到 8765 自动标注服务
set SRC_DIR=%1
if "%SRC_DIR%"=="" set SRC_DIR=E:\anchor\Trae\QDV\tools\qdv-annotator\build\_train_out
set DST=%~dp0runs\obb\obb_it1\weights\obb_current.onnx
REM 1) 找到最新的 best*.onnx
for /f "delims=" %%f in ('dir /b /od "%SRC_DIR%\best*.onnx" 2^>nul') do set NEWEST=%%f
if not defined NEWEST (echo [错误] 未找到 best*.onnx & pause & exit /b 1)
REM 2) 复制为约定文件名（服务端 OBB_MODEL_PATH 指向它）
copy /y "%SRC_DIR%\%NEWEST%" "%DST%" >nul
echo [OK] 已部署: %NEWEST%
REM 3) 提示重启服务（或调用重启小脚本）
echo 请关闭旧标注服务后重新双击「启动自动标注.bat」
pause
```
- 配套一次小改：`auto_label_server.py` 的 `OBB_MODEL_PATH` 从硬编码 `best.pt` 改为 **自动探测** `runs/obb/**/weights/obb_current.*`（优先 `.onnx`，回退 `.pt`），约 5 行，属单丝项目内部改动（不违反 QDV 安全）。
- 依赖提示：`.onnx` 推理需单丝环境装有 `onnxruntime`（`pip install onnxruntime-gpu`）；若无，服务端 `ultralytics.YOLO(onnx)` 会报错——作为 bat 输出提示。

### 7.2 方案 B（可选增强）：标注页加「导入 QDV 重训权重」按钮
- 在 `annotator_rect2.html` 增加一个按钮，POST 到服务端 `/api/load_obb_weight`，服务端完成「复制 + 热切换模型 + 自检样例预测」。更贴近零代码，但需改两个文件。
- **结论**：先落方案 A（改动最小、当日可用）；方案 B 作为二期。

### 7.3 回灌后自检（防废权重）
- 服务端 `load_obb_model()` 加载后对 1 张样例预测，0 检出 → 打醒目告警「该权重可能为无效空标签训练产物」。
- 人工确认：标注页点自动标注，应出合理数量、带角度的框。

---

## 8. QDV 安全性保障（红线清单）

1. **QDV 主程序零改动**：所有 QDVAnnotator 改动限 `E:\anchor\Trae\QDV\tools\qdv-annotator` 独立目录 + `training/yolo_train.py`（训练脚本，与主程序隔离）。
2. **回灌不触碰 QDV**：部署脚本/按钮全部放单丝项目侧；QDVAnnotator 只负责"打开/导出/训练"。
3. **构建隔离**：QDVAnnotator 独立 `cmake -S . -B build`；改动后回归 `verify/verify_export.py`（10/10）与 `test_annotation_session_undo.exe`。
4. **QML 三处登记**：任何新 QML 组件须在 `qmldir` + qrc + 引用处登记；不得把 `WideComboBox` 的 popup 宽度改回绑定表达式（回归绑定循环）。
5. **勿动 InitializeComponent 类自动生成区**（Windows Forms 规则，若涉 Qt 同理：设计器生成代码不手改）。
6. **动代码先备份**：若后续需改 `Exporters.cpp` 等，先备份原文件。

---

## 9. 风险与应对

| 风险 | 影响 | 应对 |
|---|---|---|
| 页面文件不足 → 训练被强杀 → GUI 闪退 | 高 | 用户扩虚拟内存 ≥16GB（根治）；脚本已自动降档+workers=0 兜底 |
| 用错 Python（全局 Python314）→ 训练死锁 | 高 | TrainingDialog 填 QDV venv 并先点「检测」 |
| `数据集qdv` 被误当数据源 → mAP=0 | 高 | 文档显著标注"已废勿用"；训练前置数据集体检（非空标签/配对率，任一 0 中止） |
| 8765 多实例 / 旧实例未加载新权重 | 中 | `_port_in_use` 防双起；回灌后必须重启服务；health 看 `obb_loaded` |
| `.onnx` 回灌缺 onnxruntime | 中 | bat 输出提示；或回退 `.pt` 回灌（QDVAnnotator 训练目录保留 best.pt 时） |
| `.qdvann` 177 图与 obb_dataset 重叠 → 重复训练 | 低 | 走"替代标注"而非追加（T4 用户拍板） |
| 沙箱禁跑 torch（WinError 1455） | 低（仅开发期） | 一切训练/推理验证在真机；沙箱只做语法/逻辑校验 |

---

## 10. 验收标准（可量化）

1. **数据链路**：annotator_rect2 导出 `.qdvann` 的 annotation 全部 `shape=vec_rect` 且 `angle/length1/length2/count` 非空；QDVAnnotator 打开后旋转矩形完整显示（方向箭头 + 根数配色）。
2. **训练产出**：QDVAnnotator 标训闭环完整跑完不闪退，日志可见资源降档提示（`type=resource`）；输出 `best<时间戳>.onnx` + `labels.json`。
3. **回灌生效**：`sync_obb_weight.bat` 一键复制 → 服务重启后 `health.obb_loaded=true` → 标注页「🟦 有向矩形自动标注(OBB)」出带角度框（φ + 根数合理）。
4. **闭环回归**：人工修正 → 导出 → 再训练 → 再回灌，循环 ≥1 轮无人工干预代码。
5. **QDV 无回归**：`verify/verify_export.py` 10/10；undo 单测 exit=0；冒烟启动无绑定循环。

---

## 11. 里程碑与待办清单（合并 T/R，含负责人）

| 阶段 | 事项 | 负责人 | 优先级 |
|---|---|---|---|
| 立即 | R1+T1：新版 exe 真机验收标训闭环；重启 8765 加载新 OBB best.pt | 用户 | 高 |
| 立即 | R4/T2：OBB 端到端真机训练跑通（含扩页面文件） | 用户 | 高 |
| 方案落地 | T6：`sync_obb_weight.bat` + `auto_label_server.py` 的 OBB_MODEL_PATH 探测小改 | AI+用户 | 高 |
| 方案落地 | 回灌后自检样例预测告警 | AI | 中 |
| 决策 | T4：`.qdvann` 5346 条精标「替换」mvtec 标签（qdvann_to_obb.py）或保持现状 | 用户拍板 | 低 |
| 可选二期 | 方案 B：标注页「导入 QDV 重训权重」按钮 | AI | 低 |

---

## 12. 附录：关键文件路径索引

**QDVAnnotator（`E:\anchor\Trae\QDV\tools\qdv-annotator\`）**
- `build\QDVAnnotator.exe` — 可执行
- `src\AnnotationSession.h/.cpp` — 会话（yolo_obb / trainFromDataYaml / vec_rect）
- `src\TrainingBridge.h/.cpp` — 训练桥
- `qml\TrainingDialog.qml` / `qml\ExportDialog.qml` / `qml\Main.qml` — GUI 入口
- `qml\QDVAnnotator\WideComboBox.qml` — 自适应下拉
- `verify\verify_export.py` / `_verify_exported_obb.py` — 回归/OB B 校验
- `../training\yolo_train.py` — 训练脚本（`_tune_resources` 资源自适应）
- 三份交接文件（同目录 + 单丝项目）—— 本方案依据

**单丝项目（`E:\anchor\WorkBuddy\moren\单丝计数V2.0\`）**
- `annotator_rect2.html` — 标注工具（exportQdv→vec_rect；OBB 自动标注按钮）
- `scripts\auto_label_server.py` — 8765 服务（OBB 懒加载；`OBB_MODEL_PATH` 待小改探测）
- `scripts\train_obb_noshim.py` / `train_obb_worker.py` — OBB 训练（noshim+workers=0）
- `runs\obb\obb_it1\weights\best.pt` — 首轮 OBB 权重
- `obb_dataset\` — 既有 OBB 训练集（306 图/11054 标注）
- `启动自动标注.bat` — 服务启动入口
- `交接文件_2026-08-19.md` / `数据集qdv_软件修改建议.md` — 背景依据

---

## 13. 附加能力扩展：能力注册表（S0/S1 已落地）

> 兑现「统一配置文件、改配置即扩展」的增强诉求。闭环主线方案不变，此节为独立附加能力。

**目标**：新增标注形状 / 导出格式 / 训练任务时**只改 `capabilities.json`**，不动 C++/QML 分派代码。

**落地文件**（`E:\anchor\Trae\QDV\tools\qdv-annotator\`）
- `capabilities.json` — 统一元数据（`shapes` / `exports` / `tasks` 三段）
- `capabilities.qrc` — 打包进 exe（`qrc:/capabilities` 兜底）
- `src\ShapeRegistry.h/.cpp` — 只读加载器，暴露为 QML 全局属性 `capabilities`
- `main.cpp` — 创建 `ShapeRegistry` 并注入根上下文

**加载优先级**：环境变量 `QDV_CAPABILITIES` → exe 目录回溯 6 层 → 当前工作目录 → `qrc:/capabilities`。

**已由注册表驱动的入口**：工具栏绘制按钮、画布几何分派（`geo`）、导出格式下拉（`exports`）、训练模型下拉（`taskModels`）、任务下拉与状态文案（`tasks`）、标注列表显示名（`label`）。

**关键机制**：同一 `overlayType` 多形状（`polygon`/`freeform`）共享绘制实现，靠 `activeShapeKey` 精确落 key；编辑时保留原形状 key 防串类。

**验证状态**：构建 exit 0；冒烟启动注册表从文件加载成功；工具栏按注册表动态生成「矩形 / 方向矩形 / 多边形 / 自由形状」五个工具；冒烟无 QML 报错。
