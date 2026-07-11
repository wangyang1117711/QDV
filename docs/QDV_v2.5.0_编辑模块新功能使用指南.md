# QDV v2.5.0 编辑模块新功能使用指南

> **版本**：v2.5.0
> **生成日期**：2026-07-05
> **适用范围**：QDetectVision.exe（编辑模块 EditView）

---

## 目录

1. [功能概览](#1-功能概览)
2. [连接线删除交互](#2-连接线删除交互)
3. [Halcon 风格图像预览](#3-halcon-风格图像预览)
4. [整链部署](#4-整链部署)
5. [单算子运行](#5-单算子运行)
6. [算子参数编辑器运行按钮](#6-算子参数编辑器运行按钮)
7. [测试报告](#7-测试报告)
8. [常见问题](#8-常见问题)

---

## 1. 功能概览

v2.5.0 版本针对编辑模块（EditView）完成 5 项功能开发：

| 编号 | 功能 | 入口 |
|------|------|------|
| 1 | 画布算子拖拽 | 画布节点（P1-B03 已修复，v2.5.0 沿用） |
| 2 | 连接线删除交互 | 画布连线左键/右键 |
| 3 | 整链部署 | 顶栏"▶ 部署"按钮 |
| 4 | Halcon 风格图像预览 | 效果预览区"选择输入图像"按钮 |
| 5 | 单算子运行 | 节点右键菜单"▶ 运行到此算子" / 算子编辑器"▶ 运行"按钮 |

**技术决策**（已与用户确认）：
- 部署输入源：默认 FileDialog，预留相机接口（`setCameraFrame`）
- 单算子运行：执行上游链 + 选中算子（DFS 拓扑排序）
- 部署结果展示：模态部署对话框（DeployDialog）
- Halcon 预览深度：缩放 + 平移

---

## 2. 连接线删除交互

### 2.1 操作方式

| 操作 | 效果 |
|------|------|
| 鼠标悬停连线 | 连线高亮为红色实线（线宽 3px） |
| 左键点击连线 | 直接删除该连线 |
| 右键点击连线 | 弹出删除确认菜单 |

### 2.2 命中检测算法

采用三次贝塞尔曲线采样 20 点的近似算法：
- 采样 20 个点将贝塞尔曲线离散化为 19 段线段
- 计算鼠标点到每段线段的最短距离
- 距离 < 8px 视为命中

> **设计要点**：连线 MouseArea 使用 `propagateComposedEvents: true` + `mouse.accepted = false`，确保未命中连线的事件能穿透到下层节点，不影响节点拖拽。

### 2.3 撤销支持

删除连线操作通过 `DisconnectNodesCommand` 推入 `QUndoStack`，支持 Ctrl+Z 撤销。

---

## 3. Halcon 风格图像预览

### 3.1 ImageViewer 控件

新增 [ImageViewer.qml](file:///e:/anchor/Trae/QDV/qml/EditView/ImageViewer.qml) 控件，参考 Halcon 软件的图像浏览交互。

| 操作 | 效果 |
|------|------|
| 滚轮上滚 | 以鼠标为中心放大（×1.15，上限 20×） |
| 滚轮下滚 | 以鼠标为中心缩小（÷1.15，下限 0.1×） |
| 左键拖拽 | 平移图像（光标变 ClosedHandCursor） |
| 右键点击 | 重置为"适应窗口"模式 |
| 工具栏"适应窗口" | 计算最佳缩放使图像完整显示并居中 |
| 工具栏"1:1" | 重置为原始尺寸 100% |
| 工具栏"+/−" | 以控件中心为基准放大/缩小 |

### 3.2 缩放数学原理

以鼠标为中心缩放时，保持鼠标点对应的图像坐标不变：
```
新 offset = 鼠标坐标 − 图像坐标 × 新缩放
```

### 3.3 部署结果预览

[DeployDialog.qml](file:///e:/anchor/Trae/QDV/qml/EditView/DeployDialog.qml) 模态对话框内嵌 ImageViewer，支持部署完成后对输出图像进行缩放/平移浏览。

---

## 4. 整链部署

### 4.1 操作步骤

1. 在画布上搭建算子链（添加节点 + 连接）
2. 点击顶栏 **"▶ 部署"** 按钮
3. 在弹出的 FileDialog 中选择输入图像（支持 PNG/JPG/BMP/TIFF）
4. 系统执行整链：按节点添加顺序依次执行每个算子
5. 弹出 **DeployDialog 模态对话框** 展示结果

### 4.2 DeployDialog 对话框内容

| 区域 | 内容 |
|------|------|
| 顶部摘要 | 成功/失败状态、总耗时 |
| 摘要信息 | 输入图像文件名、算子总数、成功数、失败数 |
| 算子执行明细 | ListView 列表，每行：状态图标/序号/算子名/耗时/错误信息 |
| 输出图像预览 | ImageViewer 控件，支持缩放/平移 |
| 底部按钮 | "查看大图"（打开 ImagePreviewWindow）/ "关闭" |

### 4.3 后端实现

- **入口**：`EditViewBridge::runScheme(inputImagePath)`
- **流程**：
  1. 确定输入图像路径（参数优先，其次 `m_cameraFramePath`）
  2. `cv::imread` 加载图像
  3. `buildToolChainFromNodes` 将所有节点转换为 VisionTool 列表
  4. `ToolChainExecutor.execute` 执行整链
  5. `saveMatToTempPng` 保存输出图像到临时文件
  6. 返回 `{success, totalTools, successCount, failCount, elapsedMs, outputImagePath, toolResults}`

### 4.4 相机接口预留

```cpp
// C++ 端：设置相机帧临时文件路径
editViewBridge->setCameraFrame("C:/tmp/camera_frame_001.png");

// QML 端：调用 runScheme 时传空字符串，自动使用 m_cameraFramePath
editViewBridge.runScheme("")
```

---

## 5. 单算子运行

### 5.1 操作步骤

1. 在画布上右键点击目标算子节点
2. 选择 **"▶ 运行到此算子"**
3. 在弹出的 FileDialog 中选择输入图像
4. 系统自动计算上游链（DFS 拓扑排序）
5. 按拓扑顺序执行：所有上游算子 → 目标算子
6. 弹出 DeployDialog 展示结果（标题为"运行到此算子结果"）

### 5.2 上游链拓扑排序算法

采用 DFS 递归收集上游节点：
```
collectUpstreamNodes(nodeId, result, visited):
    for each conn where conn.toId == nodeId:
        collectUpstreamNodes(conn.fromId, result, visited)
    result.append(nodeId)  // 所有上游处理完后才加入自己
```

**结果**：返回的节点 ID 列表按拓扑有序（最上游在前，目标节点在最后）。

### 5.3 后端实现

- **入口**：`EditViewBridge::runSingleOperator(nodeId, inputImagePath)`
- **返回结构**：
  ```json
  {
    "success": true,
    "upstreamCount": 2,
    "elapsedMs": 45,
    "outputImagePath": "C:/tmp/qdv_output_xxx.png",
    "upstreamResults": [{toolId, toolName, ok, elapsedMs, errorMessage}, ...],
    "targetResult": {toolId, toolName, ok, elapsedMs, errorMessage}
  }
  ```

### 5.4 使用场景

- **调试单个算子**：只想看某个中间算子的输出，无需执行整链
- **参数调优**：调整某算子参数后快速验证效果
- **问题定位**：链路某处结果异常时，逐段运行定位问题算子

---

## 6. 算子参数编辑器运行按钮

### 6.1 操作步骤

1. 双击画布上的算子节点，打开 OperatorEditorDialog
2. 调整参数
3. 点击 **"▶ 运行"** 按钮
4. 系统先应用当前参数到节点
5. 在弹出的 FileDialog 中选择输入图像
6. 执行 `runSingleOperator`（含上游链）
7. 弹出运行结果摘要对话框

### 6.2 与右键菜单"运行到此算子"的区别

| 入口 | 区别 |
|------|------|
| 右键菜单"运行到此算子" | 直接运行（使用节点当前参数） |
| 编辑器"▶ 运行"按钮 | 先应用编辑器中的参数，再运行 |

---

## 7. 测试报告

### 7.1 测试统计

| 项目 | 数量 |
|------|------|
| 单元测试总数 | 277 |
| 通过数 | 277 |
| 失败数 | 0 |
| 通过率 | 100% |
| 新增测试用例 | 7（v2.5.0） |

### 7.2 v2.5.0 新增测试用例

| 用例名 | 类型 | 验证点 |
|--------|------|--------|
| runScheme empty scheme returns failure | 边界 | 空方案返回 `success=false` + `error` |
| runScheme invalid image path returns failure | 边界 | 无效图像路径返回 `success=false` + `error` |
| runSingleOperator non-existent node returns failure | 边界 | 不存在节点 ID 返回 `success=false` + `error` |
| runSingleOperator empty image path returns failure | 边界 | 空输入图像返回 `success=false` + `error` |
| cameraFramePath initial empty and setCameraFrame | 接口 | 初始为空，`setCameraFrame` 后更新 |
| runSingleOperator ReadImage end-to-end | 集成 | 单算子端到端执行，验证输出文件实际存在 |
| runSingleOperator with upstream chain | 集成 | 双节点链 `ReadImage → Threshold`，验证上游链执行 |

### 7.3 构建验证

| 项目 | 结果 |
|------|------|
| 构建系统 | CMake + MinGW Makefiles |
| 编译器 | MinGW 13.10 64-bit |
| 构建结果 | exit code 0（成功） |
| 警告 | 仅 DirectX 12 编译器缺失警告（不影响功能） |
| 主应用冒烟测试 | QDetectVision.exe 启动成功，进程 6 秒后存活无崩溃 |

### 7.4 测试设计原则

遵循 AGENTS.md 宪法 II 阶段 1 红队测试要求：
- 每条关键路径至少 1 条对抗性测试（空输入/无效输入/边界值）
- 验证返回值结构语义（`success`/`outputImagePath`/`toolResults`）而非仅 `success=true`
- 集成测试验证输出图像文件实际存在（确定性终门）

---

## 8. 常见问题

### Q1: 部署时提示"当前方案无算子"

**原因**：画布上没有添加任何算子节点。
**解决**：从左侧算子库双击或拖拽算子到画布。

### Q2: 运行到此算子时上游算子未执行

**原因**：上游算子与目标算子之间没有连线。
**解决**：在画布上从上游算子的输出端口拖拽到下游算子的输入端口建立连线。

### Q3: 部署失败，提示"无法加载图像"

**原因**：输入图像路径无效或格式不支持。
**解决**：确认图像文件存在，支持 PNG/JPG/JPEG/BMP/TIF/TIFF 格式。

### Q4: 输出图像预览为空

**原因**：算子链执行失败，未生成输出图像。
**解决**：查看 DeployDialog 算子执行明细中的错误信息，定位失败算子。

### Q5: 连接线无法删除

**原因**：可能未精确命中连线。
**解决**：鼠标悬停连线使其高亮为红色实线后再点击。命中阈值为 8px。

### Q6: 图像预览缩放/平移失效

**原因**：图像尚未加载完成（BusyIndicator 仍在旋转）。
**解决**：等待图像加载完成（BusyIndicator 消失）后再操作。

---

## 附录：相关源文件

| 文件 | 说明 |
|------|------|
| [qml/EditView/Main.qml](file:///e:/anchor/Trae/QDV/qml/EditView/Main.qml) | 主视图，含连线删除/部署按钮/右键菜单"运行到此算子" |
| [qml/EditView/ImageViewer.qml](file:///e:/anchor/Trae/QDV/qml/EditView/ImageViewer.qml) | Halcon 风格图像浏览控件（缩放/平移） |
| [qml/EditView/DeployDialog.qml](file:///e:/anchor/Trae/QDV/qml/EditView/DeployDialog.qml) | 模态部署结果对话框 |
| [qml/EditView/PropertyPreviewPanel.qml](file:///e:/anchor/Trae/QDV/qml/EditView/PropertyPreviewPanel.qml) | 右侧面板，含"选择输入图像"按钮 |
| [qml/EditView/OperatorEditorDialog.qml](file:///e:/anchor/Trae/QDV/qml/EditView/OperatorEditorDialog.qml) | 算子参数编辑器，含"▶ 运行"按钮 |
| [include/UI/EditViewBridge.h](file:///e:/anchor/Trae/QDV/include/UI/EditViewBridge.h) | C++↔QML 桥接器接口 |
| [src/UI/EditViewBridge.cpp](file:///e:/anchor/Trae/QDV/src/UI/EditViewBridge.cpp) | 桥接器实现（runScheme/runSingleOperator/上游链算法） |
| [tests/UI/test_editview_bridge.cpp](file:///e:/anchor/Trae/QDV/tests/UI/test_editview_bridge.cpp) | 单元测试（含 v2.5.0 新增 7 例） |
