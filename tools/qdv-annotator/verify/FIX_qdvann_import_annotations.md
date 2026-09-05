# QDVAnnotator 导入 qdvann 标注不显示 — 修复记录

日期：2026-08-18
问题来源：`C:\Users\wangy\Downloads\rect2_qdv_project_fixed.qdvann` 导入后图像列表有名称，
但画布无标注、无法标注、右侧标注列表空白。

## 根因（已实验验证）

| # | 位置 | 问题 | 证据 |
|---|------|------|------|
| 1 | `qml/AnnotationCanvas.qml` Image.source | `"file:///" + path` 直接拼接**反斜杠+中文**路径，QML 会把反斜杠丢弃 → 图片加载失败 (Image.Error) | 实验：反斜杠路径 `Cannot open: file:///E:anchorWorkBuddy...` status=3；正斜杠路径 READY 3072x2048 |
| 2 | 同上 | 图片加载失败 → `onStatusChanged` 的 `status===Image.Ready` 不成立 → `fit()` 不执行 → scale=1 → 标注坐标（如 x=1693）全部在可视区外 | 代码路径分析 |
| 3 | `qml/AnnotationListPanel.qml` | `model: session.currentAnnotations()` 是**函数调用**，无 NOTIFY 信号，QML 只在创建时求值一次，列表永不刷新 | QML 绑定机制 |
| 4 | `qml/ImageListPanel.qml` | 缩略图同样拼接反斜杠路径，缩略图加载失败 | 与 #1 相同 |

**数据文件本身完好**：77 张图 / 346 个 rect 标注（labelId 0-1），路径全部有效、无越界、字段完整。

## 修复内容

1. `AnnotationCanvas.qml`
   - 新增 `toFileUrl()`：路径反斜杠 → 正斜杠后再拼 `file:///`
   - `onStatusChanged` 同时处理 `Image.Ready` 和 `Image.Error` 都触发 `fit()`，保证布局可预期
   - 新增 `onScaleChanged/onPanXChanged/onPanYChanged` 触发 `rebuild()`，fit 缩放后标注坐标正确重算
2. `ImageListPanel.qml` — 缩略图 `source` 加 `.replace(/\\/g, "/")`
3. `AnnotationListPanel.qml` — `model` 改为 `property var anns` + `Connections` 监听
   `currentImageChanged/imagesChanged/currentImageIndexChanged` 手动刷新

## 验证

- ✅ 重新编译通过：`cmake --build build` → QDVAnnotator.exe（1.48MB）
- ✅ 实验验证：正斜杠路径 QML Image 加载成功 `READY 3072x2048`
- ✅ 程序冒烟启动正常（offscreen，无 QML 错误）
- ✅ YOLO 数据集导出端到端逻辑验证（8 张有标注图 → dataset.yaml + labels）

## 数据质量提示

fixed.qdvann 中 **77 张图仅 8 张有标注**（37~75 个/张），69 张无标注。
若直接训练，69 张无标注图会被当作背景图参与训练，可能干扰收敛。
建议：先在标注工具中确认这 8 张的标注质量，或补充更多标注后再训练。

## 训练前置检查（用户环境）

- Python：`D:/Program Files/Python314/python.exe`
- torch 2.11.0+cu128 ✓（RTX 5060 Blackwell sm_120 匹配）
- ultralytics 8.4.70 ✓，yaml/cv2/onnx ✓
- 训练脚本：`E:/anchor/Trae/QDV/training/yolo_train.py` ✓（TrainingBridge 会自动定位）

## 修复后训练路径

1. 重新打开 `rect2_qdv_project_fixed.qdvann` → 画布应显示图片+标注框，右侧列表有标注项
2. 顶栏「标训闭环」→ 输出目录选一个空文件夹 → Python 填 `D:/Program Files/Python314/python.exe`
3. 模型默认 YOLOv8n、轮数 50、batch 16、imgsz 640、lr 0.01 → 「开始标训闭环」
4. 等待训练完成，产物 ONNX 在输出目录
