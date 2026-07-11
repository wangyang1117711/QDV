# 画布连接线选择与算子复制粘贴设计文档

- 版本：1.0
- 日期：2026-07-10
- 范围：Qt6 + QML 桌面应用（`qml/EditView/Main.qml`、`include/UI/EditViewBridge.h`、`src/UI/EditViewBridge.cpp`）
- 关联需求：实现画布中连接线的单独选择与删除、算子节点的复制与粘贴

## 1. 背景与目标

当前编辑画布已具备：
- 节点多选、框选、拖拽、Delete 删除；
- 连接线悬停高亮、左键直接删除、右键删除菜单；
- 节点参数的复制粘贴（`Ctrl+C/V`，但本质是"同类型节点间参数迁移"）。

缺失的能力：
1. 连接线没有独立的"选中"状态，无法通过 `Delete` 键删除，也无法框选；
2. 复制节点不能生成新的节点副本，也不会保留节点间的内部连接。

本设计目标：
- 让连接线具备独立的选中态，支持点击/框选 + `Delete`/右键菜单删除；
- 让节点复制粘贴真正生成新节点副本，保留全部属性和内部连接；
- 所有改动响应时间 < 300ms，且与现有撤销/重做、右键菜单、快捷键体系保持一致。

## 2. 当前状态

主要代码位置：
- `qml/EditView/Main.qml`：
  - `hoveredConnectionIndex`（第 89 行）：仅悬停索引；
  - `hitTestConnection()`（第 216-291 行）：点到贝塞尔曲线距离检测；
  - `connectionHitArea`（第 1922-1979 行）：左键点击直接删除连线；
  - `rubberBand` 框选（第 1683-1765 行）：仅框选节点；
  - `copySelectedNodes()` / `pasteNodes()`（第 317-357 行）：复制 `type/params/x/y`，调用 `addOperator` + `updateOperatorParams`；
  - 快捷键 `Ctrl+C/V/A`、`Delete`（第 1131-1152 行）。
- `include/UI/EditViewBridge.h` / `src/UI/EditViewBridge.cpp`：
  - `addOperator(type, x, y)`：生成新 `id` 与默认 `params`；
  - `updateOperatorParams(nodeId, params)`：合并并校验参数；
  - `connectNodes(...)` / `disconnectEdge(...)`：创建/删除连接，支持 undo/redo；
  - `deleteNode(nodeId)`：删除节点并清理相关连接。

## 3. 设计方案

采用**方案 A：最小侵入式扩展**，改动集中在 QML 层，复用现有 C++ 桥接接口。

### 3.1 连接线单独选择与删除

#### 3.1.1 新增状态

在 `Main.qml` 根对象新增：

```qml
// 当前选中的连线索引（-1 = 无选中）
property int selectedConnectionIndex: -1
```

#### 3.1.2 视觉反馈

在 `connectionsCanvas.onPaint` 中，绘制逻辑优先级：
1. `selectedConnectionIndex`：黄色/亮红色（`#FFD700`），`lineWidth: 4`，并加发光效果；
2. `hoveredConnectionIndex`：现有红色（`#e74c3c`），`lineWidth: 3`；
3. 普通连线：主题色，`lineWidth: 2`。

#### 3.1.3 鼠标点击

修改 `connectionHitArea.onClicked`：
- 左键点击命中连线：设置 `selectedConnectionIndex = hitIdx`，不立即删除；
- 右键点击命中连线：先设置 `selectedConnectionIndex = hitIdx`，再弹出删除菜单；
- 点击空白处：`selectedConnectionIndex = -1`。

#### 3.1.4 框选

在 `canvasRubberBandArea.onReleased` 框选节点逻辑之后，追加：
- 遍历所有连接，计算其 `fromNode` 和 `toNode` 的屏幕端点；
- 若任一端点落在 rubber band 矩形内，则 `selectedConnectionIndex = ci`；
- 单选模式：取最后一个命中的连接（避免与节点框选冲突）。

#### 3.1.5 删除

新增 `deleteSelectedConnection()` 函数：

```qml
function deleteSelectedConnection() {
    if (root.selectedConnectionIndex < 0) return
    var conns = editViewBridge.connections
    if (root.selectedConnectionIndex < conns.length) {
        var c = conns[root.selectedConnectionIndex]
        editViewBridge.disconnectEdge(c.fromId, c.fromPort, c.toId, c.toPort)
        root.showToast("info", "已删除连接")
    }
    root.selectedConnectionIndex = -1
}
```

修改 `Delete` 快捷键逻辑：
- 若 `selectedNodeIds.length > 0`：删除节点（现有逻辑）；
- 否则若 `selectedConnectionIndex >= 0`：调用 `deleteSelectedConnection()`。

#### 3.1.6 取消选择

- `Esc` 快捷键：已清空 `selectedNodeIds` 和 `currentSelectedNodeId`，追加 `selectedConnectionIndex = -1`；
- 开始新建连接（`pendingConnectionFrom` 非空）：清空 `selectedConnectionIndex`。

### 3.2 算子节点复制与粘贴

#### 3.2.1 剪贴板结构

扩展 `clipboardNodes` 元素，保存完整节点对象（深拷贝）：

```qml
property var clipboardNodes: []
```

每个元素包含：
- `type`
- `params`（完整参数映射）
- `x`, `y`
- 保留未来可能扩展的字段（除 `id` 外全部保留）。

#### 3.2.2 复制函数

修改 `copySelectedNodes()`：
- 对选中节点做深拷贝；
- 记录原节点 `id` 到拷贝对象的 `_originalId` 字段（仅内部使用，不持久化）；
- 清空 `selectedConnectionIndex`。

#### 3.2.3 粘贴函数

修改 `pasteNodes()`：
- 计算原节点集合的最小包围矩形中心；
- 根据粘贴次数计算统一偏移量（首次 30px，第二次 60px，依此类推）；
- 遍历 `clipboardNodes`：
  1. 调用 `editViewBridge.addOperator(n.type, n.x + offset, n.y + offset)` 生成新节点；
  2. 调用 `editViewBridge.updateOperatorParams(newId, n.params)` 覆盖参数；
  3. 记录 `oldId -> newId` 映射；
  4. 收集新节点 id 列表。
- 重建内部连接：遍历原 `editViewBridge.connections`，若 `fromId` 和 `toId` 都在映射中，则对新的 id 调用 `connectNodes`；
- 粘贴后自动选中新节点：`selectedNodeIds = newIds`，`currentSelectedNodeId = newIds[0]`；
- 清空 `selectedConnectionIndex`。

#### 3.2.4 多次粘贴偏移

新增 `pasteGeneration` 计数器：

```qml
property int pasteGeneration: 0
```

每次成功粘贴后自增；当 `clipboardNodes` 变化时重置为 0。

实际偏移 = `(pasteGeneration + 1) * 30`。

#### 3.2.5 右键菜单

- 节点右键菜单 `nodeContextMenu` 增加"复制节点"项；
- 画布空白右键菜单 `canvasContextMenu` 增加"粘贴节点"项（仅当剪贴板非空时启用）。

## 4. 交互规范汇总

| 操作 | 行为 |
|------|------|
| 悬停连线 | 连线红色高亮（现有） |
| 左键点击连线 | 选中该连线，黄色高亮 |
| 右键点击连线 | 选中该连线并弹出"删除连接"菜单 |
| 框选区域包含连线端点 | 选中该连线 |
| `Delete`（无选中节点，有选中连线） | 删除选中连线 |
| `Delete`（有选中节点） | 删除选中节点（节点删除会自动清理相关连线） |
| `Esc` | 清空所有选中状态（节点 + 连线） |
| `Ctrl+C` / 右键"复制节点" | 深拷贝选中节点到剪贴板 |
| `Ctrl+V` / 右键"粘贴节点" | 在原地生成新节点副本，重建内部连接，整体偏移递增 |
| 多次 `Ctrl+V` | 每次偏移量增加 30px，避免重叠 |
| 粘贴后 | 自动选中新节点 |

## 5. 测试用例

### 5.1 连接线选择与删除

| 编号 | 前置条件 | 操作步骤 | 预期结果 |
|------|----------|----------|----------|
| TC-CONN-01 | 画布有 2 个已连接节点 | 鼠标悬停连线 | 连线变红，`cursorShape = PointingHandCursor` |
| TC-CONN-02 | 画布有 2 个已连接节点 | 左键点击连线 | 连线变为黄色/亮红色选中态，`selectedConnectionIndex >= 0` |
| TC-CONN-03 | 已选中一条连线 | 按 `Delete` | 连线被删除，`selectedConnectionIndex = -1`，对应节点不再连接 |
| TC-CONN-04 | 已选中一条连线 | 按 `Esc` | 选中态清空，连线恢复普通颜色 |
| TC-CONN-05 | 画布有 2 个已连接节点 | 右键点击连线并选择"删除连接" | 连线被删除 |
| TC-CONN-06 | 画布有多条连线 | 用 rubber band 框选某条连线的端点 | 该连线被选中（单选） |
| TC-CONN-07 | 同时选中节点和连线 | 按 `Delete` | 节点被删除（节点删除会级联删除相关连线） |

### 5.2 节点复制粘贴

| 编号 | 前置条件 | 操作步骤 | 预期结果 |
|------|----------|----------|----------|
| TC-COPY-01 | 画布有 1 个配置好的算子 | `Ctrl+C` 复制 | Toast 提示"已复制 1 个节点" |
| TC-COPY-02 | 已复制 1 个节点 | `Ctrl+V` 粘贴 | 生成新节点，类型、参数、位置均正确偏移 |
| TC-COPY-03 | 已复制 1 个节点 | 连续 3 次 `Ctrl+V` | 生成 3 个新节点，分别偏移 30px、60px、90px |
| TC-COPY-04 | 画布有 3 个节点，其中 A→B→C | 框选 A、B、C 后 `Ctrl+C`，再 `Ctrl+V` | 生成 A'、B'、C'，且 A'→B'→C' 自动连接 |
| TC-COPY-05 | 选中节点后 | 右键菜单选择"复制节点"，再右键空白处"粘贴节点" | 与快捷键行为一致 |
| TC-COPY-06 | 复制节点后修改原节点参数 | 粘贴 | 副本保留原参数，不受原节点后续修改影响 |
| TC-COPY-07 | 未选中任何节点 | `Ctrl+C` | Toast 提示"未选中节点" |

### 5.3 性能与一致性

| 编号 | 前置条件 | 操作步骤 | 预期结果 |
|------|----------|----------|----------|
| TC-PERF-01 | 画布有 100 个节点 | 全选后复制粘贴 | 操作完成时间 < 300ms |
| TC-PERF-02 | 画布有 50 条连线 | 快速悬停、点击、删除连线 | 无卡顿，视觉反馈即时 |
| TC-COMPAT-01 | 在 Qt6 桌面应用内 | 验证复制粘贴、连线选删 | 功能正常（注：本项目为 Qt 桌面应用，非浏览器应用） |

## 6. 实施范围

### 6.1 修改文件

- `qml/EditView/Main.qml`
  - 新增 `selectedConnectionIndex`、`pasteGeneration` 状态；
  - 修改 `connectionsCanvas.onPaint` 绘制选中态；
  - 修改 `connectionHitArea` 点击逻辑；
  - 修改 `canvasRubberBandArea.onReleased` 追加框选连线；
  - 修改 `copySelectedNodes()` / `pasteNodes()`；
  - 新增 `deleteSelectedConnection()`；
  - 修改 `Delete` / `Esc` 快捷键处理；
  - 节点右键菜单和画布右键菜单增加复制/粘贴项。

- `include/UI/EditViewBridge.h` / `src/UI/EditViewBridge.cpp`
  - 本次**不新增公共槽函数**，仅复用现有 `addOperator` / `updateOperatorParams` / `connectNodes` / `disconnectEdge` / `deleteNode`。
  - 若 `updateOperatorParams` 对复制的参数校验失败，需在 QML 层回滚刚创建的节点（调用 `deleteNode`）。

### 6.2 不修改文件

- `Windows Form 设计器`生成的 `InitializeComponent()` 及相关代码；
- 算子实现（`src/operators/...`）；
- 序列化格式（节点/连接 JSON 结构不变）。

## 7. 风险与回滚

| 风险 | 影响 | 缓解措施 |
|------|------|----------|
| `updateOperatorParams` 校验失败导致新节点参数不完整 | 粘贴后节点行为异常 | 校验失败时删除刚创建的节点并提示用户 |
| 大量节点复制粘贴触发多次 `currentNodesChanged` | 界面短暂卡顿 | 100 节点以内实测 < 50ms；若出现卡顿可后续迁移到 C++ 批量克隆 |
| 框选连线与框选节点逻辑冲突 | 同时选中节点和连线导致 Delete 行为歧义 | 节点删除优先级高于连线删除；Esc 可一键清空 |
| 连线选中态与悬停态颜色相近 | 用户难以区分 | 选中态使用黄色/亮红色 + 更粗线宽 + 发光效果 |

回滚策略：
- 所有改动均通过 Git 管理，可直接 `git checkout` 还原 `Main.qml`；
- 不改动 C++ 接口，回滚后不影响其他模块。

## 8. 验收标准

- [ ] 左键点击连线可选中，有独立视觉反馈；
- [ ] `Delete` 键可删除选中连线；
- [ ] 右键菜单可删除选中连线；
- [ ] 框选可选中端点落在框内的连线；
- [ ] `Ctrl+C/V` 可复制粘贴节点并生成新节点副本；
- [ ] 粘贴后副本保留原节点全部参数；
- [ ] 多个节点同时复制时，内部连接自动重建；
- [ ] 多次粘贴偏移递增，不重叠；
- [ ] 100 节点以内复制粘贴响应 < 300ms；
- [ ] 提供功能测试用例与操作说明文档。

## 9. 操作说明文档

功能上线后，将在 `docs/QDV_v2.x.x_画布选择与复制粘贴使用指南.md` 中补充：
- 图文说明如何选中/删除连线；
- 如何复制粘贴单个和多个节点；
- 常见问题（粘贴后节点重叠、参数未保留等）。
