// =====================================================================
// QDV EditView 主视图（v3.0.0 方案B 设计系统驱动重构）
//
// 三栏布局：左 算子库（搜索+筛选）/ 中央 画布 / 右 算子详情
// 顶栏：撤销/重做/新建/保存/加载 + 缩放 + 部署
//
// v3.0.0 变更：
//   - 全局 Design Token 体系替代硬编码配色（WCAG AA 通过）
//   - 响应式断点：≥1200px 三栏 / 900-1199 两栏 / <900 单栏+抽屉
//   - 画布滚轮缩放 + Delete 删除 + 右键菜单 + 框选多选
//   - 真实小地图（节点缩略）+ 键盘导航
// =====================================================================

import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import Qt.labs.settings
import QDV.EditView 3.0 as Tok

Rectangle {
    id: root
    // v3.2.1 修复首次未铺满：根元素必须跟随 QQuickWidget viewport 自动拉伸。
    // 原固定 width:1280/height:800 在 QQuickWidget::SizeRootObjectToView 模式下，
    // 首次加载时若 viewport 尚未完成布局，会导致界面只显示 1280x800 区域，
    // 窗口最大化后右侧/底部出现大片空白。改为 anchors.fill: parent 让 root
    // 自动占满 QQuickWidget 的视口，彻底消除该问题。
    anchors.fill: parent
    color: Tok.DesignTokens.bgCanvas

    property string currentSelectedNodeId: ""
    property string toastMessage: ""
    property string toastLevel: "info"
    property bool   toastVisible: false
    property string searchKeyword: ""
    property string activeFilter: "all"
    property real   canvasZoom: 1.0
    property var    selectedNodeIds: []    // v3.0.0：多选

    // spec: editor-output-connection-optimization Task7：冲突数量角标
    property int    conflictCount: 0

    // P1-B01 修复：画布平移偏移量（节点世界坐标 → 屏幕坐标 = world * zoom + offset）
    // 之前 canvasOffsetX/Y 未定义，小地图视口框绘制为 NaN，点击导航设置属性后画布不响应
    property real   canvasOffsetX: 0
    property real   canvasOffsetY: 0
    // P1-B02 修复：画布平移交互状态（中键拖拽 或 空格+左键拖拽）
    property bool   spacePressed: false
    property bool   isPanning: false
    property real   panStartX: 0
    property real   panStartY: 0
    property real   panStartOffsetX: 0
    property real   panStartOffsetY: 0

    // v4.0 P0 修复：端口建连状态
    // pendingConnectionFrom 非空表示正在建连（已点击输出端口，等待点击输入端口）
    property string pendingConnectionFrom: ""
    property real   pendingConnMouseX: 0   // 鼠标在画布中的位置（用于绘制临时连线）
    property real   pendingConnMouseY: 0

    // v4.0: 网格模式 ('lines' | 'dots')
    property string gridMode: "lines"

    // v4.0: 框选状态
    property bool rubberBandActive: false
    property real rbStartX: 0
    property real rbStartY: 0
    property real rbEndX: 0
    property real rbEndY: 0

    property bool   rightPanelVisible: true
    property bool   leftPanelVisible: true
    property bool   rightPanelFloating: false   // v3.1.0: 右侧面板浮动模式
    // v5.1：右侧信息列状态
    property bool   rightInfoVisible: true       // 右侧信息列可见性（图片预览+变量）
    property bool   variablePanelExpanded: true   // 变量管理面板展开状态
    // v3.2.0：算子参数编辑器 — pin 状态
    // true = 编辑器被锁定保持显示，不会因取消选择等操作自动收起
    property bool   rightPanelPinned: false
    property url    previewSourceImage: ""       // v3.1.0: 原图路径
    property url    previewProcessedImage: ""    // v3.1.0: 处理后图像路径
    property var    analysisResults: []          // v3.1.0: 分析结果数据
    // 异步部署执行状态（修复部署期间点选无响应）
    property bool   deployBusy: false
    property string deployInputPath: ""

    // v2.6.0 底部工作区（Halcon 风格：预览 + 变量管理）
    property bool   bottomPanelVisible: true     // 底部工作区可见性
    // property bool bottomPanelPinned 已移除（v5.1：预览固定功能已迁出底部工作区）
    property int    bottomPanelHeight: 180       // v5.1：底部工作区高度 220→180（变量管理面板）
    property bool   logOverlayVisible: false     // 右下角日志浮窗展开状态
    // property int bottomPanelTab 已移除（v5.1：预览/变量管理 Tab 已迁出底部工作区）

    // v2.5.0 功能 2：连接线悬停/删除状态
    property int    hoveredConnectionIndex: -1   // 当前悬停的连线索引（-1=无悬停）
    // v2.5.0 功能 2 增强：连接线独立选中状态（-1=无选中）
    property int    selectedConnectionIndex: -1

    // v2.7.0 Phase 3.1：分组容器折叠状态字典
    // key=分组ID（loopToolId / branchToolId / branchId），value=true 表示已折叠
    // 折叠时通过 nodeRepeater.itemAt(idx).visible=false 隐藏对应子节点
    property var    groupCollapsed: ({})

    // v2.7.0 Phase 3.1：计算一组节点 ID 在画布上的屏幕坐标包围盒
    // 返回 {minX, minY, maxX, maxY, valid}（valid=false 表示无节点匹配）
    // 屏幕坐标 = 世界坐标 * canvasZoom + canvasOffset
    function computeNodeBBox(nodeIds) {
        var nodes = editViewBridge.currentNodes
        var minX = Infinity, minY = Infinity, maxX = -Infinity, maxY = -Infinity
        var found = 0
        var nw = Tok.DesignTokens.nodeCardWidth
        var nh = Tok.DesignTokens.nodeCardHeight
        for (var i = 0; i < nodeIds.length; i++) {
            var id = nodeIds[i]
            for (var j = 0; j < nodes.length; j++) {
                if (nodes[j].id === id) {
                    var nx = nodes[j].x
                    var ny = nodes[j].y
                    if (nx < minX) minX = nx
                    if (ny < minY) minY = ny
                    if (nx + nw > maxX) maxX = nx + nw
                    if (ny + nh > maxY) maxY = ny + nh
                    found++
                    break
                }
            }
        }
        if (found === 0) return {minX: 0, minY: 0, maxX: 0, maxY: 0, valid: false}
        var z = root.canvasZoom
        return {
            minX: minX * z + root.canvasOffsetX,
            minY: minY * z + root.canvasOffsetY,
            maxX: maxX * z + root.canvasOffsetX,
            maxY: maxY * z + root.canvasOffsetY,
            valid: true
        }
    }

    // v2.7.0 Phase 3.1：切换分组折叠状态，并同步设置子节点 delegate.visible
    // 不修改节点 Repeater 的 model/delegate，仅通过 itemAt() 访问实例改 visible
    function toggleGroupCollapse(groupId, childIds) {
        var st = root.groupCollapsed
        var newCollapsed = !(st[groupId] === true)
        // 复制字典以触发属性变更通知
        var newSt = {}
        for (var k in st) newSt[k] = st[k]
        newSt[groupId] = newCollapsed
        root.groupCollapsed = newSt
        // 遍历节点 Repeater，设置匹配 ID 的 delegate.visible
        for (var i = 0; i < childIds.length; i++) {
            var id = childIds[i]
            for (var j = 0; j < nodeRepeater.count; j++) {
                var item = nodeRepeater.itemAt(j)
                if (item && item.nodeData && item.nodeData.id === id) {
                    item.visible = !newCollapsed
                    break
                }
            }
        }
    }

    // v5.3.2 修复 QRhi 跨实例错误：
    // 根因：QQuickWidget 嵌入 QStackedWidget 时，隐藏/显示或 GPU device loss 会重建
    // QRhi 上下文；若 QML 场景保持常驻，旧 QSG 纹理仍引用已销毁的 QRhi，导致
    // "Texture belongs to QRhi A but client code attempted to use it with QRhi B"。
    // 修复已在 EditView.cpp 中实现：隐藏/QRhi 失效前调用 setSource("") 卸载 QML，
    // 释放所有旧纹理；显示/QRhi 重建后重新加载 QML，确保纹理在新上下文创建。
    // 同时保留环境变量 QSG_RHI_BACKEND=d3d11、QSG_RENDER_LOOP=basic、
    // QSG_NO_TEXTURE_CACHE=1、QSG_NO_DEPTH_BUFFER=1 作为辅助。

    // ============ 响应式状态判断 ============
    readonly property bool isWide:   root.width >= Tok.DesignTokens.breakpointLg   // ≥1200 三栏
    readonly property bool isMedium: root.width >= Tok.DesignTokens.breakpointMd
                                      && root.width < Tok.DesignTokens.breakpointLg  // 900-1199 两栏
    readonly property bool isNarrow: root.width < Tok.DesignTokens.breakpointMd      // <900 单栏+抽屉

    onWidthChanged: {
        if (isWide) {
            leftPanelVisible = true
            rightPanelVisible = true
        } else if (isMedium) {
            leftPanelVisible = true
            rightPanelVisible = false
        } else {
            leftPanelVisible = false
            rightPanelVisible = false
        }
    }

    // ============ 工具函数 ============
    function openEditorForNode(nodeId) {
        root.currentSelectedNodeId = nodeId
        // v3.3.0：双击算子打开交互式调参对话框（滑块/下拉/勾选 + 实时预览）
        // 替代原 OperatorEditorDialog（模态详编，无实时预览）
        var comp = Qt.createComponent("qrc:/qml/EditView/OperatorTunerDialog.qml")
        if (comp.status === Component.Ready) {
            comp.createObject(root, {
                bridge: editViewBridge,
                nodeId: nodeId
            }).open()
        } else {
            // 兜底：Tuner 加载失败时回退到原详编对话框
            console.warn("[Main] OperatorTunerDialog 加载失败，回退到 OperatorEditorDialog:", comp.errorString())
            var fallback = Qt.createComponent("qrc:/qml/EditView/OperatorEditorDialog.qml")
            if (fallback.status === Component.Ready) {
                fallback.createObject(root, {
                    bridge: editViewBridge,
                    nodeId: nodeId
                }).open()
            }
        }
    }

    // v2.8.0：打开算子帮助弹窗（通过 operatorHelpDialogLoader 延迟加载单例）
    function openOperatorHelp(operatorType) {
        if (!operatorType || operatorType === "") return
        if (!operatorHelpDialogLoader.item) {
            operatorHelpDialogLoader.active = true
        }
        if (operatorHelpDialogLoader.item) {
            operatorHelpDialogLoader.item.operatorType = operatorType
            operatorHelpDialogLoader.item.open()
        }
    }

    function showToast(level, msg) {
        root.toastLevel = level
        root.toastMessage = msg
        root.toastVisible = true
        toastTimer.restart()
    }

    function selectNode(nodeId) {
        editViewBridge.selectNode(nodeId)
        // P0 修复：同步 currentSelectedNodeId，使属性面板随单击刷新
        root.currentSelectedNodeId = nodeId
        selectedNodeIds = [nodeId]
        if (isNarrow || isMedium) {
            rightPanelVisible = true  // 自动展开右侧面板
        }
        // 修复图像累积显示：选中节点时，预览仅显示该节点的输出图（单张），
        // 无输出图则清空，避免上一节点残留图叠加。
        syncPreviewToNode(nodeId)
    }

    // 将主预览同步为指定节点的输出图（单张）；无输出图则清空预览
    function syncPreviewToNode(nodeId) {
        var iv = editViewBridge ? editViewBridge.imageVariableManager : null
        if (!iv || !nodeId) {
            root.previewSourceImage = ""
            root.previewProcessedImage = ""
            return
        }
        var path = iv.imagePath(nodeId)
        if (path && path.length > 0) {
            root.previewSourceImage = "file:///" + path
            root.previewProcessedImage = ""
        } else {
            // v5.2 修复"看不到图像输入源"：
            // 当节点尚未执行（ImageVariableManager 无缓存图）时，
            // 对 ReadImage 节点直接显示其 filePath 参数指向的图像，
            // 让用户能立即看到选中的图像输入源。
            var node = null
            var nodes = editViewBridge.currentNodes || []
            for (var i = 0; i < nodes.length; ++i) {
                if (nodes[i].id === nodeId) {
                    node = nodes[i]
                    break
                }
            }
            if (node && node.type === "ReadImage") {
                var fp = node.params ? node.params.filePath : ""
                if (fp && fp.length > 0) {
                    root.previewSourceImage = "file:///" + fp
                    root.previewProcessedImage = ""
                } else {
                    root.previewSourceImage = ""
                    root.previewProcessedImage = ""
                }
            } else {
                root.previewSourceImage = ""
                root.previewProcessedImage = ""
            }
        }
    }

    // v3.1.0: 打开图像预览浮窗
    function openImagePreview(sourceUrl, processedUrl, title) {
        previewWindow.sourceImage = sourceUrl
        previewWindow.processedImage = processedUrl
        previewWindow.imageTitle = title || "效果预览"
        previewWindow.open()
    }

    // ============ spec: editor-output-connection-optimization Task5 ============
    // 分层处理 + 连线标签 + 交叉避让 的几何计算（O(n)）支持
    // buildConnectionMaps：一次性构建所有连线的分组计数/索引/节点映射，
    // 供绘制层与命中检测层共用，保证「绘制曲线」与「点击命中」几何完全一致。
    function buildConnectionMaps() {
        var conns = editViewBridge.connections
        var n = conns.length
        // 节点 id → 节点对象 映射（避免绘制时 O(n²) 遍历查找）
        var nodeMap = {}
        var curNodes = editViewBridge.currentNodes
        for (var ni = 0; ni < curNodes.length; ni++) {
            nodeMap[curNodes[ni].id] = curNodes[ni]
        }
        var pairCount = {}, fromCount = {}, toCount = {}
        var pairIdx = [], fromIdx = [], toIdx = []
        var pairSeen = {}, fromSeen = {}, toSeen = {}
        for (var i = 0; i < n; i++) {
            var c = conns[i]
            var pk = c.fromId + "|" + c.toId
            pairCount[pk] = (pairCount[pk] || 0) + 1
            fromCount[c.fromId] = (fromCount[c.fromId] || 0) + 1
            toCount[c.toId] = (toCount[c.toId] || 0) + 1
        }
        for (var j = 0; j < n; j++) {
            var cc = conns[j]
            var pk2 = cc.fromId + "|" + cc.toId
            pairIdx[j] = pairSeen[pk2] || 0
            pairSeen[pk2] = pairIdx[j] + 1
            fromIdx[j] = fromSeen[cc.fromId] || 0
            fromSeen[cc.fromId] = fromIdx[j] + 1
            toIdx[j] = toSeen[cc.toId] || 0
            toSeen[cc.toId] = toIdx[j] + 1
        }
        return {nodeMap: nodeMap, pairCount: pairCount, fromCount: fromCount, toCount: toCount,
                pairIdx: pairIdx, fromIdx: fromIdx, toIdx: toIdx}
    }

    // connectionCurve：计算第 idx 条连线的屏幕端点/控制点/中点（含分层与交叉避让偏移）
    // 分层：同一对节点间多条连线按索引垂直错开（平行连线免重叠）
    // 扇出/扇入：同源/同目标多条连线轻微散开，避免汇聚重叠
    function connectionCurve(idx, maps) {
        var conns = editViewBridge.connections
        if (idx < 0 || idx >= conns.length) return null
        var conn = conns[idx]
        var fromNode = maps.nodeMap[conn.fromId]
        var toNode = maps.nodeMap[conn.toId]
        if (!fromNode || !toNode) return null
        var z = root.canvasZoom
        var ox = root.canvasOffsetX
        var oy = root.canvasOffsetY
        var nodeW = Tok.DesignTokens.nodeCardWidth
        var nodeH = Tok.DesignTokens.nodeCardHeight
        var livePos = connectionsCanvas.livePositions
        function getNodePos(node) {
            var live = livePos[node.id]
            if (live !== undefined) return {x: live.x, y: live.y}
            return {x: node.x, y: node.y}
        }
        var fromPos = getNodePos(fromNode)
        var toPos = getNodePos(toNode)
        // 屏幕坐标 = world * zoom + offset
        var fx = (fromPos.x + nodeW) * z + ox
        var fy = (fromPos.y + nodeH / 2) * z + oy
        var tx = toPos.x * z + ox
        var ty = (toPos.y + nodeH / 2) * z + oy

        var pk = conn.fromId + "|" + conn.toId
        var pairCount = maps.pairCount[pk] || 1
        var pairIdx = maps.pairIdx[idx] || 0
        var fromCount = maps.fromCount[conn.fromId] || 1
        var fromIdx = maps.fromIdx[idx] || 0
        var toCount = maps.toCount[conn.toId] || 1
        var toIdx = maps.toIdx[idx] || 0

        // 分层偏移：同一对节点间多条连线垂直错开（主机制）
        var offsetY = 0
        if (pairCount > 1) {
            offsetY += (pairIdx - (pairCount - 1) / 2) * (16 * z)
        }
        // 扇出/扇入弯曲：同源/同目标多条连线轻微散开
        if (fromCount > 1) {
            offsetY += (fromIdx - (fromCount - 1) / 2) * (6 * z)
        }
        if (toCount > 1) {
            offsetY += (toIdx - (toCount - 1) / 2) * (6 * z)
        }

        var absDx = Math.abs(tx - fx)
        var cx1 = fx + absDx * 0.4
        var cy1 = fy + offsetY
        var cx2 = tx - absDx * 0.4
        var cy2 = ty + offsetY
        // 三次贝塞尔中点（t=0.5）：(P0 + 3P1 + 3P2 + P3)/8，用于绘制连线标签
        var ux = (fx + 3 * cx1 + 3 * cx2 + tx) / 8
        var uy = (fy + 3 * cy1 + 3 * cy2 + ty) / 8
        return {fx: fx, fy: fy, tx: tx, ty: ty, cx1: cx1, cy1: cy1, cx2: cx2, cy2: cy2, ux: ux, uy: uy, fromPort: conn.fromPort, toPort: conn.toPort}
    }

    // v2.5.0 功能 2：连线命中检测
    // 计算屏幕坐标 (px, py) 到第 idx 条连线的最短距离
    // 连线为贝塞尔曲线，采样 20 个点近似为线段，计算点到线段最短距离
    // 返回值：距离 < threshold 时返回最近的那条连线索引，-1 表示未命中
    function hitTestConnection(px, py) {
        // 修复：节点/端口排除保护。
        // 若点击点落在任一节点卡片屏幕矩形内，一律不判定为连线命中，
        // 把点击还给节点及其端口。否则已连线的节点（如 A→B 曲线从 A 输出端口出发）
        // 其端口会被曲线命中检测拦截，导致无法再次点击 A 输出端口建立第二条连接。
        var curNodes = editViewBridge.currentNodes
        var zz = root.canvasZoom
        var ox = root.canvasOffsetX
        var oy = root.canvasOffsetY
        var nW = Tok.DesignTokens.nodeCardWidth * zz
        var nH = Tok.DesignTokens.nodeCardHeight * zz
        var live = connectionsCanvas.livePositions
        for (var ni = 0; ni < curNodes.length; ni++) {
            var nd = curNodes[ni]
            var livePos = live[nd.id]
            var nx = (livePos !== undefined ? livePos.x : nd.x) * zz + ox
            var ny = (livePos !== undefined ? livePos.y : nd.y) * zz + oy
            if (px >= nx && px <= nx + nW && py >= ny && py <= ny + nH) {
                return -1
            }
        }

        var conns = editViewBridge.connections
        var threshold = 12  // 命中阈值（像素），适当放宽以提高点击成功率

        var bestIdx = -1
        var bestDist = threshold
        // spec: 复用与绘制一致的几何（含分层/交叉避让偏移），保证点击命中与实际显示相符
        var maps = root.buildConnectionMaps()
        for (var ci = 0; ci < conns.length; ci++) {
            var g = root.connectionCurve(ci, maps)
            if (!g) continue
            var fx = g.fx, fy = g.fy, tx = g.tx, ty = g.ty
            var cx1 = g.cx1, cy1 = g.cy1, cx2 = g.cx2, cy2 = g.cy2

            // 采样 20 个点，计算点到每段线段的最短距离
            var prevX = fx, prevY = fy
            var connMinDist = threshold
            for (var t = 1; t <= 20; t++) {
                var u = t / 20
                var oneMinusU = 1 - u
                // 三次贝塞尔曲线: B(u) = (1-u)³P0 + 3(1-u)²uP1 + 3(1-u)u²P2 + u³P3
                var x = oneMinusU * oneMinusU * oneMinusU * fx
                      + 3 * oneMinusU * oneMinusU * u * cx1
                      + 3 * oneMinusU * u * u * cx2
                      + u * u * u * tx
                var y = oneMinusU * oneMinusU * oneMinusU * fy
                      + 3 * oneMinusU * oneMinusU * u * cy1
                      + 3 * oneMinusU * u * u * cy2
                      + u * u * u * ty

                // 点 (px,py) 到线段 (prevX,prevY)-(x,y) 的最短距离
                // 修复：跳过端口附近的端点段（t==1 起点段、t==20 终点段）。
                // 连线曲线从源输出端口中心出发、在目标输入端口中心结束，
                // 若不跳过，点击端口会被判为命中连线并被 connectionHitArea(z:1) 拦截，
                // 导致无法点击输出端口启动第二条连接 / 无法点击输入端口完成建连。
                if (t >= 2 && t <= 19) {
                    var dx = x - prevX
                    var dy = y - prevY
                    var lenSq = dx * dx + dy * dy
                    var proj = 0
                    if (lenSq > 0.0001) {
                        proj = ((px - prevX) * dx + (py - prevY) * dy) / lenSq
                        proj = Math.max(0, Math.min(1, proj))
                    }
                    var closestX = prevX + proj * dx
                    var closestY = prevY + proj * dy
                    var distX = px - closestX
                    var distY = py - closestY
                    var dist = Math.sqrt(distX * distX + distY * distY)
                    if (dist < connMinDist) {
                        connMinDist = dist
                    }
                }
                prevX = x
                prevY = y
            }
            // 在阈值范围内选择距离最近的连接，避免多条连线靠近时误选
            if (connMinDist < bestDist) {
                bestDist = connMinDist
                bestIdx = ci
            }
        }
        return bestIdx
    }

    // v3.1.0: 切换右侧面板浮动/停靠
    function toggleRightPanelFloating() {
        rightPanelFloating = !rightPanelFloating
    }

    function deleteSelectedNodes() {
        for (var i = 0; i < selectedNodeIds.length; ++i) {
            editViewBridge.deleteNode(selectedNodeIds[i])
        }
        selectedNodeIds = []
        // P1 修复：清空 currentSelectedNodeId，避免顶栏"编辑"按钮对已删除节点可点击
        root.currentSelectedNodeId = ""
    }

    // v2.5.0 功能 2 增强：删除当前选中的连线
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

    // P1-B12 修复：重置画布缩放和平移到默认状态
    function resetCanvasView() {
        root.canvasZoom = 1.0
        root.canvasOffsetX = 0
        root.canvasOffsetY = 0
    }

    // P1-B07 修复：复制/粘贴/全选 — 节点剪贴板
    property var clipboardNodes: []
    // 多次粘贴代数，每次复制后重置，用于递增偏移避免副本重叠
    property int pasteGeneration: 0

    function copySelectedNodes() {
        if (root.selectedNodeIds.length === 0) {
            root.showToast("info", "未选中节点")
            return
        }
        var nodes = editViewBridge.currentNodes
        var copied = []
        for (var i = 0; i < nodes.length; ++i) {
            if (root.selectedNodeIds.indexOf(nodes[i].id) >= 0) {
                // 深拷贝完整节点对象，保留原 id 用于后续重建内部连接
                var clone = JSON.parse(JSON.stringify(nodes[i]))
                clone._originalId = nodes[i].id
                copied.push(clone)
            }
        }
        root.clipboardNodes = copied
        root.pasteGeneration = 0
        root.selectedConnectionIndex = -1
        root.showToast("success", "已复制 " + copied.length + " 个节点")
    }

    function pasteNodes() {
        if (root.clipboardNodes.length === 0) {
            root.showToast("info", "剪贴板为空")
            return
        }
        // 计算原节点集合的最小包围矩形中心
        var minX = Infinity, minY = Infinity, maxX = -Infinity, maxY = -Infinity
        for (var i = 0; i < root.clipboardNodes.length; ++i) {
            var n = root.clipboardNodes[i]
            minX = Math.min(minX, n.x)
            minY = Math.min(minY, n.y)
            maxX = Math.max(maxX, n.x + Tok.DesignTokens.nodeCardWidth)
            maxY = Math.max(maxY, n.y + Tok.DesignTokens.nodeCardHeight)
        }
        // 粘贴偏移：每次粘贴整体向右下移动，多次粘贴递增避免重叠
        var offset = (root.pasteGeneration + 1) * 30
        var pastedCount = 0
        var idMap = {}       // oldId -> newId
        var newIds = []      // 新节点 id 列表
        for (var j = 0; j < root.clipboardNodes.length; ++j) {
            var src = root.clipboardNodes[j]
            var newX = src.x + offset
            var newY = src.y + offset
            var newId = editViewBridge.addOperator(src.type, newX, newY)
            if (newId && newId !== "") {
                // 覆盖参数（深拷贝，避免引用污染）
                if (src.params) {
                    editViewBridge.updateOperatorParams(newId, JSON.parse(JSON.stringify(src.params)))
                }
                idMap[src._originalId || src.id] = newId
                newIds.push(newId)
                pastedCount++
            }
        }
        // 重建被复制节点之间的内部连接
        if (pastedCount > 0) {
            var conns = editViewBridge.connections
            for (var k = 0; k < conns.length; ++k) {
                var c = conns[k]
                var newFrom = idMap[c.fromId]
                var newTo = idMap[c.toId]
                if (newFrom && newTo) {
                    editViewBridge.connectNodes(newFrom, c.fromPort, newTo, c.toPort)
                }
            }
            // 粘贴后选中新节点
            root.selectedNodeIds = newIds
            root.currentSelectedNodeId = newIds.length > 0 ? newIds[0] : ""
            root.selectedConnectionIndex = -1
            root.pasteGeneration++
            root.showToast("success", "已粘贴 " + pastedCount + " 个节点")
        }
    }

    function selectAllNodes() {
        var nodes = editViewBridge.currentNodes
        var ids = []
        for (var i = 0; i < nodes.length; ++i) ids.push(nodes[i].id)
        root.selectedNodeIds = ids
        if (ids.length > 0) root.currentSelectedNodeId = ids[0]
    }

    // P1-B12 修复：适应画布（计算所有节点边界，自动调整 zoom/offset 使全部节点可见）
    function fitCanvasToNodes() {
        var nodes = editViewBridge.currentNodes
        if (nodes.length === 0) {
            root.resetCanvasView()
            return
        }
        var minX = Infinity, minY = Infinity, maxX = -Infinity, maxY = -Infinity
        for (var i = 0; i < nodes.length; ++i) {
            minX = Math.min(minX, nodes[i].x)
            minY = Math.min(minY, nodes[i].y)
            maxX = Math.max(maxX, nodes[i].x + Tok.DesignTokens.nodeCardWidth)
            maxY = Math.max(maxY, nodes[i].y + Tok.DesignTokens.nodeCardHeight + 4)  // v5.0：8→4
        }
        var pad = 40  // 边距
        var contentW = maxX - minX + pad * 2
        var contentH = maxY - minY + pad * 2
        var zoomX = canvasFrame.width / contentW
        var zoomY = canvasFrame.height / contentH
        var newZoom = Math.min(zoomX, zoomY, 2.0)  // 最大 2.0
        newZoom = Math.max(0.25, newZoom)  // 最小 0.25
        root.canvasZoom = newZoom
        // 居中：使内容中心对准画布中心
        var centerX = (minX + maxX) / 2
        var centerY = (minY + maxY) / 2
        root.canvasOffsetX = canvasFrame.width / 2 - centerX * newZoom
        root.canvasOffsetY = canvasFrame.height / 2 - centerY * newZoom
    }

    // P1-B08 修复：方向键键盘导航（基于当前选中节点找最近邻）
    function navigateNode(direction) {
        var nodes = editViewBridge.currentNodes
        if (nodes.length === 0) return
        var currentId = root.currentSelectedNodeId
        if (!currentId) {
            // 无选中时选第一个节点
            root.selectNode(nodes[0].id)
            return
        }
        var curNode = null
        for (var i = 0; i < nodes.length; ++i) {
            if (nodes[i].id === currentId) { curNode = nodes[i]; break }
        }
        if (!curNode) {
            root.selectNode(nodes[0].id)
            return
        }
        // 按 direction 找最近邻节点
        var best = null
        var bestDist = Infinity
        for (var j = 0; j < nodes.length; ++j) {
            if (nodes[j].id === currentId) continue
            var dx = nodes[j].x - curNode.x
            var dy = nodes[j].y - curNode.y
            var valid = false
            if (direction === "right" && dx > 0) valid = true
            else if (direction === "left" && dx < 0) valid = true
            else if (direction === "down" && dy > 0) valid = true
            else if (direction === "up" && dy < 0) valid = true
            if (!valid) continue
            // 距离平方（优先同方向）
            var dist = dx * dx + dy * dy
            if (dist < bestDist) { bestDist = dist; best = nodes[j] }
        }
        if (best) root.selectNode(best.id)
    }

    // 获取当前筛选后的算子列表
    function getFilteredOperators() {
        var list = []
        if (root.activeFilter === "favorites") {
            list = editViewBridge.favorites()
        } else if (root.activeFilter === "recents") {
            list = editViewBridge.recents()
        } else if (root.activeFilter === "commons") {
            list = editViewBridge.commons()
        } else {
            list = editViewBridge.searchOperators(root.searchKeyword)
        }
        if (root.activeFilter !== "all" && root.searchKeyword.trim() !== "") {
            var kw = root.searchKeyword.trim().toLowerCase()
            var filtered = []
            for (var i = 0; i < list.length; ++i) {
                var m = list[i]
                if ((m.cnName || "").toLowerCase().indexOf(kw) >= 0 ||
                    (m.type || "").toLowerCase().indexOf(kw) >= 0 ||
                    (m.category || "").toLowerCase().indexOf(kw) >= 0 ||
                    (m.subGroup || "").toLowerCase().indexOf(kw) >= 0) {
                    filtered.push(m)
                }
            }
            return filtered
        }
        return list
    }

    // ============ 定时器 ============
    Timer { id: toastTimer; interval: 2500; onTriggered: root.toastVisible = false }

    // ============ 算子参数编辑器 — 状态持久化 ============
    // v3.2.0：编辑器的显示/隐藏 + 固定状态在软件重启后保持
    // 存储位置：build/bin/appData/editorState.ini
    Settings {
        id: editorState
        category: "EditView/Layout"
        property bool rightPanelVisible: true
        property bool rightPanelPinned:  false
        property int  rightPanelWidth:  280  // v5.0：300→280
    }

    // 初始化：从 Settings 加载
    Component.onCompleted: {
        if (editorState.rightPanelVisible !== undefined)
            root.rightPanelVisible = editorState.rightPanelVisible
        if (editorState.rightPanelPinned !== undefined)
            root.rightPanelPinned = editorState.rightPanelPinned
        // P1-B02：让 root 获取焦点以接收空格键 Keys 事件
        root.forceActiveFocus()
        // spec: editor-output-connection-optimization Task7：初始化冲突角标
        root.conflictCount = (editViewBridge && editViewBridge.conflictDetector)
                             ? editViewBridge.conflictDetector.detectAll().length : 0
    }

    // 变化时写回
    onRightPanelVisibleChanged: editorState.rightPanelVisible = rightPanelVisible
    onRightPanelPinnedChanged:  editorState.rightPanelPinned  = rightPanelPinned

    // ============ P1-A1 修复：连接 bridge 信号到 Toast（错误/保存/加载提示） ============
    // 之前 errorRaised 在 9 处 emit 但 QML 端零 handler，所有错误静默丢失
    Connections {
        target: editViewBridge
        function onErrorRaised(phase, message) {
            root.showToast("error", "[" + phase + "] " + message)
        }
        function onSaveFinished(filePath, success, message) {
            root.showToast(success ? "success" : "error", message)
        }
        function onLoadFinished(filePath, success, message, jsonText) {
            root.showToast(success ? "success" : "error", message)
            // P1-B10 修复：加载新方案后清空选中状态，避免属性面板残留已不存在的节点
            if (success) {
                root.selectedNodeIds = []
                root.currentSelectedNodeId = ""
                root.pendingConnectionFrom = ""
            }
        }
        function onFavoritesChanged() {
            // 收藏变化时刷新算子库面板（v-spec: 重建分类列表模型）
            if (root.activeFilter === "favorites") {
                operatorList.model = null
                operatorList.model = operatorList.buildModel()
            }
        }
        // 异步部署完成 → 处理结果
        function onSchemeDeployFinished(result) {
            root.onDeployFinished(result)
        }
        // v5.2 修复"看不到图像输入源"：
        // 参数变化（如 ReadImage 选择文件后）触发 currentNodesChanged，
        // 此时刷新预览，让用户立即看到选中的图像。
        function onCurrentNodesChanged() {
            if (root.currentSelectedNodeId) {
                root.syncPreviewToNode(root.currentSelectedNodeId)
            }
        }
        // spec: editor-output-connection-optimization Task7：冲突变化时刷新角标
        function onConflictsChanged() {
            root.conflictCount = (editViewBridge && editViewBridge.conflictDetector)
                                 ? editViewBridge.conflictDetector.detectAll().length : 0
        }
    }

    // v2.7.0 O1a：监听算子导入/删除信号，刷新算子库面板
    // 导入成功后新算子需出现在左侧算子库列表中
    Connections {
        target: editViewBridge && editViewBridge.operatorLibraryBridge
                ? editViewBridge.operatorLibraryBridge : null
        function onImportedChanged(type, action) {
            // 复用 L1383-1384 模式：重建算子列表
            operatorList.model = null
            operatorList.model = operatorList.buildModel()
        }
    }

    // ============ 文件对话框 ============
    // v5.3.1：修复保存失败 bug —— String(selectedFile) 不解码百分号编码，
    // 中文/空格路径会变成 %20 等编码，导致 QSaveFile 找不到目录。
    // 正确做法：用 decodeURIComponent 解码 QUrl 的百分号编码。
    // 同时增加多格式支持：JSON / 压缩包 .qdvz / XML

    // 导出对话框（Loader 延迟加载，避免资源浪费）
    Loader {
        id: exportDialogLoader
        source: "qrc:/qml/EditView/ExportDialog.qml"
        onLoaded: {
            item.parent = root
            item.bridge = editViewBridge
        }
    }

    // v2.7.0 O1a：算子导入对话框 + 已导入算子面板（Loader 延迟加载）
    Loader {
        id: operatorImportDialogLoader
        source: "qrc:/qml/EditView/OperatorImportDialog.qml"
        onLoaded: {
            item.parent = root
            item.bridge = editViewBridge
        }
    }
    Loader {
        id: importedOperatorsPanelLoader
        source: "qrc:/qml/EditView/ImportedOperatorsPanel.qml"
        onLoaded: {
            item.parent = root
            item.bridge = editViewBridge
        }
    }

    // v2.8.0：算子帮助弹窗（延迟加载，首次调用 openOperatorHelp 时激活）
    Loader {
        id: operatorHelpDialogLoader
        active: false
        source: "qrc:/qml/EditView/OperatorHelpDialog.qml"
        onLoaded: {
            item.parent = root
        }
    }

    FileDialog {
        id: saveFileDialog
        title: "保存方案"
        fileMode: FileDialog.SaveFile
        nameFilters: [
            "JSON 方案 (*.json)",
            "压缩方案包 (*.qdvz)",
            "XML 方案 (*.xml)",
            "所有文件 (*)"
        ]
        defaultSuffix: "json"
        onAccepted: {
            var rawUrl = selectedFile.toString()
            // 剥离 file:/// 前缀（Windows: file:///E:/... → E:/...）
            var path = rawUrl
            if (path.startsWith("file:///")) path = path.substring(8)
            else if (path.startsWith("file://")) path = path.substring(7)
            // 解码百分号编码（中文/空格 → 可读字符）
            path = decodeURIComponent(path)
            editViewBridge.saveToFile(path)
        }
    }
    FileDialog {
        id: loadFileDialog
        title: "加载方案"
        fileMode: FileDialog.OpenFile
        nameFilters: [
            "所有方案文件 (*.json *.qdvz *.xml)",
            "JSON 方案 (*.json)",
            "压缩方案包 (*.qdvz)",
            "XML 方案 (*.xml)",
            "所有文件 (*)"
        ]
        defaultSuffix: ""
        onAccepted: {
            var rawUrl = selectedFile.toString()
            var path = rawUrl
            if (path.startsWith("file:///")) path = path.substring(8)
            else if (path.startsWith("file://")) path = path.substring(7)
            path = decodeURIComponent(path)
            editViewBridge.loadFromFile(path)
        }
    }

    // v2.5.0 功能 3c：部署输入图像选择对话框
    // 用户选择输入图像后，调用 editViewBridge.runScheme() 执行整链，
    // 结果交给 DeployDialog 模态展示（进度/算子状态/输出预览）。
    FileDialog {
        id: deployInputFileDialog
        title: "选择部署输入图像"
        fileMode: FileDialog.OpenFile
        nameFilters: ["图像文件 (*.png *.jpg *.jpeg *.bmp *.tif *.tiff)", "所有文件 (*)"]
        onAccepted: {
            var path = String(selectedFile)
            if (path.startsWith("file:///")) path = path.substring(8)
            else if (path.startsWith("file://")) path = path.substring(7)
            root.runDeploy(path)
        }
    }

    // 部署执行入口：调用 bridge.runSchemeAsync 异步执行（避免阻塞主线程导致点选无响应）
    function runDeploy(inputPath) {
        if (!editViewBridge) return
        root.deployInputPath = inputPath
        root.deployBusy = true
        editViewBridge.runSchemeAsync(inputPath)
    }

    // 处理异步部署完成
    function onDeployFinished(result) {
        root.deployBusy = false
        if (!result || !result.success) {
            root.showToast("error", "部署失败：" + (result && result.error ? result.error : "未知错误"))
            return
        }
        // 同步预览属性（原图 + 输出图）
        var actualInputPath = root.deployInputPath
        if ((!actualInputPath || actualInputPath === "") && result.toolResults) {
            for (var i = 0; i < result.toolResults.length; ++i) {
                if (result.toolResults[i].outputImagePath) {
                    actualInputPath = result.toolResults[i].outputImagePath
                    break
                }
            }
        }
        if (actualInputPath && actualInputPath !== "") {
            root.previewSourceImage = "file:///" + actualInputPath
        }
        if (result.outputImagePath && result.outputImagePath !== "") {
            root.previewProcessedImage = "file:///" + result.outputImagePath
        }
        // 弹出模态部署结果对话框
        var comp = Qt.createComponent("qrc:/qml/EditView/DeployDialog.qml")
        if (comp.status === Component.Ready) {
            var dlg = comp.createObject(root, {
                inputImagePath: actualInputPath,
                result: result
            })
            dlg.open()
        } else {
            root.showToast("success",
                "部署完成：成功 " + result.successCount + "/" + result.totalTools
                + "，耗时 " + result.elapsedMs + " ms")
        }
    }

    // v2.5.0 功能 5c：运行到此算子 — 输入图像选择对话框
    // targetNodeId 由右键菜单触发时设置，onAccepted 后调用 runToNode
    FileDialog {
        id: runToNodeFileDialog
        title: "选择运行到此算子的输入图像"
        fileMode: FileDialog.OpenFile
        nameFilters: ["图像文件 (*.png *.jpg *.jpeg *.bmp *.tif *.tiff)", "所有文件 (*)"]
        property string targetNodeId: ""
        onAccepted: {
            var path = String(selectedFile)
            if (path.startsWith("file:///")) path = path.substring(8)
            else if (path.startsWith("file://")) path = path.substring(7)
            root.runToNode(path, targetNodeId)
        }
    }

    // v2.5.0 功能 5c：执行上游链 + 目标算子，结果用 DeployDialog 展示
    // v5.3.4：改为异步执行，避免阻塞 UI
    property string _runToNodeInputPath: ""
    function runToNode(inputPath, nodeId) {
        root._runToNodeInputPath = inputPath
        root.showToast("info", "正在运行...")
        editViewBridge.runSingleOperatorAsync(nodeId, inputPath)
    }

    // v5.3.4：接收异步单算子执行结果，展示 DeployDialog
    Connections {
        target: editViewBridge
        function onSingleOperatorFinished(result) {
            if (!result || !result.success) {
                root.showToast("error",
                    "运行失败：" + (result && result.error ? result.error : "未知错误"))
                return
            }
            var inputPath = root._runToNodeInputPath
            // 同步预览属性（仅当存在输入图像路径时设置；相机/数据源算子可能无输入图像）
            if (inputPath && inputPath !== "") {
                root.previewSourceImage = "file:///" + inputPath
            }
            if (result.outputImagePath && result.outputImagePath !== "") {
                root.previewProcessedImage = "file:///" + result.outputImagePath
            }
            // 复用 DeployDialog 展示结果
            var comp = Qt.createComponent("qrc:/qml/EditView/DeployDialog.qml")
            if (comp.status === Component.Ready) {
                var displayResult = {
                    success: result.success,
                    totalTools: result.upstreamCount + 1,
                    successCount: result.upstreamCount + (result.targetResult && result.targetResult.ok ? 1 : 0),
                    failCount: result.targetResult && !result.targetResult.ok ? 1 : 0,
                    elapsedMs: result.elapsedMs,
                    outputImagePath: result.outputImagePath,
                    toolResults: result.upstreamResults.concat(result.targetResult ? [result.targetResult] : [])
                }
                var dlg = comp.createObject(root, {
                    inputImagePath: inputPath,
                    result: displayResult
                })
                dlg.title = "运行到此算子结果"
                dlg.open()
            } else {
                root.showToast("success",
                    "运行完成：上游 " + result.upstreamCount + " 步，耗时 " + result.elapsedMs + " ms")
            }
        }
    }

    // ============ 主布局 ============
    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ============ 顶部工具栏 ============
        Rectangle {
            id: headerBar
            Layout.fillWidth: true
            Layout.preferredHeight: Tok.DesignTokens.headerBarHeight
            color: Tok.DesignTokens.bgHeader

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Tok.DesignTokens.space2
                anchors.rightMargin: Tok.DesignTokens.space1
                spacing: 2

                // 品牌标题
                Label {
                    text: "QDV · " + (editViewBridge.currentSchemeName || "未命名方案")
                           + (editViewBridge.isDirty ? " *" : "")
                    color: Tok.DesignTokens.textPrimary
                    font.pixelSize: Tok.DesignTokens.fontSizeBase
                    font.bold: true
                    font.family: Tok.DesignTokens.fontFamilyCJK
                    Layout.leftMargin: Tok.DesignTokens.space1
                    Layout.minimumWidth: 160
                    elide: Text.ElideRight
                }
                Item { Layout.fillWidth: true }

                // 撤销/重做
                ToolButton {
                    text: "\u21A9"  // ↩
                    ToolTip.text: "撤销 (Ctrl+Z)"
                    ToolTip.visible: hovered
                    enabled: editViewBridge.canUndo
                    onClicked: editViewBridge.undo()
                    implicitWidth: 28
                    implicitHeight: 28
                    contentItem: Label {
                        text: parent.text
                        color: Tok.DesignTokens.textSecondary
                        font.pixelSize: Tok.DesignTokens.fontSizeLg
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        color: parent.hovered ? Tok.DesignTokens.bgHover : "transparent"
                        radius: Tok.DesignTokens.radiusSm
                    }
                }
                ToolButton {
                    text: "\u21AA"  // ↪
                    ToolTip.text: "重做 (Ctrl+Y)"
                    ToolTip.visible: hovered
                    enabled: editViewBridge.canRedo
                    onClicked: editViewBridge.redo()
                    implicitWidth: 28
                    implicitHeight: 28
                    contentItem: Label {
                        text: parent.text
                        color: Tok.DesignTokens.textSecondary
                        font.pixelSize: Tok.DesignTokens.fontSizeLg
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        color: parent.hovered ? Tok.DesignTokens.bgHover : "transparent"
                        radius: Tok.DesignTokens.radiusSm
                    }
                }

                // v5.0：缩放控件（紧凑图标风格）
                ToolButton {
                    id: zoomOutBtn
                    text: "\u2212"  // −
                    ToolTip.text: "缩小"
                    ToolTip.visible: hovered
                    implicitWidth: 26; implicitHeight: 26
                    contentItem: Label {
                        text: zoomOutBtn.text
                        color: Tok.DesignTokens.textSecondary
                        font.pixelSize: Tok.DesignTokens.fontSizeSm
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        color: zoomOutBtn.hovered ? Tok.DesignTokens.bgHover : "transparent"
                        radius: Tok.DesignTokens.radiusSm
                    }
                    onClicked: { root.canvasZoom = Math.max(0.25, root.canvasZoom - 0.1) }
                }
                // v5.0：缩放百分比标签
                Label {
                    text: Math.round(root.canvasZoom * 100) + "%"
                    color: Tok.DesignTokens.textSecondary
                    font.pixelSize: Tok.DesignTokens.fontSizeXs
                    font.family: Tok.DesignTokens.fontMono
                    horizontalAlignment: Text.AlignHCenter
                    Layout.preferredWidth: 36
                }
                ToolButton {
                    id: zoomInBtn
                    text: "+"
                    ToolTip.text: "放大"
                    ToolTip.visible: hovered
                    implicitWidth: 26; implicitHeight: 26
                    contentItem: Label {
                        text: zoomInBtn.text
                        color: Tok.DesignTokens.textSecondary
                        font.pixelSize: Tok.DesignTokens.fontSizeSm
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        color: zoomInBtn.hovered ? Tok.DesignTokens.bgHover : "transparent"
                        radius: Tok.DesignTokens.radiusSm
                    }
                    onClicked: { root.canvasZoom = Math.min(3.0, root.canvasZoom + 0.1) }
                }
                // v5.0：适应画布按钮，调用 fitCanvasToNodes 而非重置缩放
                ToolButton {
                    id: fitCanvasBtn
                    text: "\u229E"  // ⊞
                    ToolTip.text: "适应画布"
                    ToolTip.visible: hovered
                    implicitWidth: 26; implicitHeight: 26
                    contentItem: Label {
                        text: fitCanvasBtn.text
                        color: Tok.DesignTokens.textSecondary
                        font.pixelSize: Tok.DesignTokens.fontSizeSm
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        color: fitCanvasBtn.hovered ? Tok.DesignTokens.bgHover : "transparent"
                        radius: Tok.DesignTokens.radiusSm
                    }
                    onClicked: { root.fitCanvasToNodes() }
                }
                // v5.0：竖线分隔
                Rectangle { width: 1; height: 18; color: Tok.DesignTokens.borderDefault; Layout.leftMargin: 2; Layout.rightMargin: 2 }

                // v5.0：左右面板切换（紧凑图标风格）
                ToolButton {
                    id: leftPanelToggle
                    text: leftPanelVisible ? "\u25C0" : "\u25B6"  // ◀/▶
                    ToolTip.text: leftPanelVisible ? "隐藏算子库" : "显示算子库"
                    ToolTip.visible: hovered
                    implicitWidth: 26; implicitHeight: 26
                    contentItem: Label {
                        text: leftPanelToggle.text
                        color: leftPanelVisible ? Tok.DesignTokens.accentPrimary : Tok.DesignTokens.textSecondary
                        font.pixelSize: Tok.DesignTokens.fontSizeSm
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        color: leftPanelToggle.hovered ? Tok.DesignTokens.bgHover : "transparent"
                        radius: Tok.DesignTokens.radiusSm
                    }
                    onClicked: leftPanelVisible = !leftPanelVisible
                }
                ToolButton {
                    id: rightPanelToggle
                    text: rightPanelVisible ? "\u25B6" : "\u25C0"  // ▶/◀
                    ToolTip.text: rightPanelVisible ? "隐藏详情" : "显示详情"
                    ToolTip.visible: hovered
                    implicitWidth: 26; implicitHeight: 26
                    contentItem: Label {
                        text: rightPanelToggle.text
                        color: rightPanelVisible ? Tok.DesignTokens.accentPrimary : Tok.DesignTokens.textSecondary
                        font.pixelSize: Tok.DesignTokens.fontSizeSm
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        color: rightPanelToggle.hovered ? Tok.DesignTokens.bgHover : "transparent"
                        radius: Tok.DesignTokens.radiusSm
                    }
                    onClicked: rightPanelVisible = !rightPanelVisible
                }
                // v5.0：底部工作区切换（紧凑图标风格，去掉 highlighted）
                ToolButton {
                    id: bottomPanelToggle
                    text: bottomPanelVisible ? "\u25BC" : "\u25B2"  // ▼/▲
                    ToolTip.text: bottomPanelVisible ? "隐藏底部工作区（变量管理）" : "显示底部工作区（变量管理）"
                    ToolTip.visible: hovered
                    implicitWidth: 26; implicitHeight: 26
                    contentItem: Label {
                        text: bottomPanelToggle.text
                        color: bottomPanelVisible ? Tok.DesignTokens.accentPrimary : Tok.DesignTokens.textSecondary
                        font.pixelSize: Tok.DesignTokens.fontSizeSm
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        color: bottomPanelToggle.hovered ? Tok.DesignTokens.bgHover : "transparent"
                        radius: Tok.DesignTokens.radiusSm
                    }
                    onClicked: bottomPanelVisible = !bottomPanelVisible
                }
                // v5.0：竖线分隔
                Rectangle { width: 1; height: 18; color: Tok.DesignTokens.borderDefault; Layout.leftMargin: 2; Layout.rightMargin: 2 }

                // spec: editor-output-connection-optimization Task5：连接管理面板入口
                ToolButton {
                    id: connManagerBtn
                    text: "连"
                    ToolTip.text: "连接管理（查看/筛选/删除所有连线）"
                    ToolTip.visible: hovered
                    implicitWidth: 26; implicitHeight: 26
                    contentItem: Label {
                        text: connManagerBtn.text
                        color: Tok.DesignTokens.textSecondary
                        font.pixelSize: Tok.DesignTokens.fontSizeXs
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        color: connManagerBtn.hovered ? Tok.DesignTokens.bgHover : "transparent"
                        radius: Tok.DesignTokens.radiusSm
                    }
                    onClicked: {
                        connectionManagerPanel.refresh()
                        connectionManagerPanel.open()
                    }
                }
                // spec: editor-output-connection-optimization Task7：输入/输出项冲突指定面板入口
                ToolButton {
                    id: conflictBtn
                    text: "冲"
                    ToolTip.text: "输入/输出冲突（查看并指定主方案）"
                    ToolTip.visible: hovered
                    implicitWidth: 26; implicitHeight: 26
                    contentItem: Label {
                        text: conflictBtn.text
                        color: root.conflictCount > 0 ? Tok.DesignTokens.accentError : Tok.DesignTokens.textSecondary
                        font.pixelSize: Tok.DesignTokens.fontSizeXs
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        color: conflictBtn.hovered ? Tok.DesignTokens.bgHover : "transparent"
                        radius: Tok.DesignTokens.radiusSm
                    }
                    onClicked: {
                        conflictPanel.refresh()
                        conflictPanel.open()
                    }
                    // 冲突数量角标（红色数字）
                    Rectangle {
                        visible: root.conflictCount > 0
                        anchors.top: parent.top
                        anchors.right: parent.right
                        width: 16; height: 16
                        radius: 8
                        color: Tok.DesignTokens.accentError
                        border.color: Tok.DesignTokens.bgHeader
                        border.width: 1
                        z: 10
                        Label {
                            anchors.centerIn: parent
                            text: root.conflictCount > 99 ? "99+" : root.conflictCount
                            color: "#FFFFFF"
                            font.pixelSize: 9
                            font.bold: true
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }
                // v5.0：竖线分隔
                Rectangle { width: 1; height: 18; color: Tok.DesignTokens.borderDefault; Layout.leftMargin: 2; Layout.rightMargin: 2 }

                // v5.0：算子编辑（紧凑图标风格，去掉文字）
                ToolButton {
                    id: editNodeBtn
                    text: "\u270E"  // ✎
                    ToolTip.text: "编辑选中节点"
                    ToolTip.visible: hovered
                    enabled: root.currentSelectedNodeId !== ""
                    implicitWidth: 26; implicitHeight: 26
                    contentItem: Label {
                        text: editNodeBtn.text
                        color: editNodeBtn.enabled ? Tok.DesignTokens.textSecondary : Tok.DesignTokens.textDisabled
                        font.pixelSize: Tok.DesignTokens.fontSizeSm
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        color: editNodeBtn.hovered ? Tok.DesignTokens.bgHover : "transparent"
                        radius: Tok.DesignTokens.radiusSm
                    }
                    onClicked: {
                        if (root.currentSelectedNodeId)
                            root.openEditorForNode(root.currentSelectedNodeId)
                    }
                }
                // v5.0：部署按钮（紧凑图标风格，accentSuccess 颜色，去掉文字）
                ToolButton {
                    id: deployBtn
                    text: "\u25B6"  // ▶
                    ToolTip.text: "运行当前方案（自动用 ReadImage/相机数据源，无则需选择图像）"
                    ToolTip.visible: hovered
                    implicitWidth: 26; implicitHeight: 26
                    contentItem: Label {
                        text: deployBtn.text
                        color: Tok.DesignTokens.accentSuccess
                        font.pixelSize: Tok.DesignTokens.fontSizeSm
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        color: deployBtn.hovered ? Tok.DesignTokens.bgHover : "transparent"
                        radius: Tok.DesignTokens.radiusSm
                    }
                    // v2.5.0 功能 3：部署按钮接入 runScheme
                    // v5.3.9 修复：统一通过 C++ resolveSchemeRunInput 判断是否需要输入图像。
                    // 若方案含 ReadImage（有效 filePath）或相机类算子，直接执行；否则弹窗选图。
                    onClicked: {
                        if (editViewBridge.currentNodes.length === 0) {
                            root.showToast("warn", "当前方案无算子，请先添加算子")
                            return
                        }
                        var runInput = editViewBridge.resolveSchemeRunInput()
                        // v5.3.9 诊断日志：确认运行按钮收到的输入源决策
                        console.log("[Main] resolveSchemeRunInput result: required=" + runInput.required
                                    + " path=" + (runInput.path || "")
                                    + " hint=" + (runInput.hint || ""))
                        if (!runInput.required) {
                            root.runDeploy("")
                        } else {
                            var autoPath = runInput.path
                            if (autoPath && autoPath !== "") {
                                root.runDeploy(autoPath)
                            } else {
                                console.warn("[Main] 打开选择部署输入图像对话框")
                                deployInputFileDialog.open()
                            }
                        }
                    }
                }
                // v5.0：竖线分隔
                Rectangle { width: 1; height: 18; color: Tok.DesignTokens.borderDefault; Layout.leftMargin: 2; Layout.rightMargin: 2 }

                // v5.0：文件操作（单字标签，紧凑风格）
                ToolButton {
                    id: newSchemeBtn
                    text: "新"
                    ToolTip.text: "新建方案"
                    ToolTip.visible: hovered
                    implicitWidth: 26; implicitHeight: 26
                    contentItem: Label {
                        text: newSchemeBtn.text
                        color: Tok.DesignTokens.textSecondary
                        font.pixelSize: Tok.DesignTokens.fontSizeXs
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        color: newSchemeBtn.hovered ? Tok.DesignTokens.bgHover : "transparent"
                        radius: Tok.DesignTokens.radiusSm
                    }
                    onClicked: editViewBridge.newScheme()
                }
                ToolButton {
                    id: saveSchemeBtn
                    text: "存"
                    ToolTip.text: "保存方案"
                    ToolTip.visible: hovered
                    enabled: editViewBridge.isDirty
                    implicitWidth: 26; implicitHeight: 26
                    contentItem: Label {
                        text: saveSchemeBtn.text
                        color: saveSchemeBtn.enabled ? Tok.DesignTokens.textSecondary : Tok.DesignTokens.textDisabled
                        font.pixelSize: Tok.DesignTokens.fontSizeXs
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        color: saveSchemeBtn.hovered ? Tok.DesignTokens.bgHover : "transparent"
                        radius: Tok.DesignTokens.radiusSm
                    }
                    onClicked: saveFileDialog.open()
                }
                ToolButton {
                    id: loadSchemeBtn
                    text: "开"
                    ToolTip.text: "加载方案"
                    ToolTip.visible: hovered
                    implicitWidth: 26; implicitHeight: 26
                    contentItem: Label {
                        text: loadSchemeBtn.text
                        color: Tok.DesignTokens.textSecondary
                        font.pixelSize: Tok.DesignTokens.fontSizeXs
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        color: loadSchemeBtn.hovered ? Tok.DesignTokens.bgHover : "transparent"
                        radius: Tok.DesignTokens.radiusSm
                    }
                    onClicked: loadFileDialog.open()
                }

                // 导出算子流程按钮
                ToolButton {
                    id: exportBtn
                    text: "导"
                    ToolTip.text: "导出算子流程为 DLL/EXE/Python"
                    ToolTip.visible: hovered
                    implicitWidth: 26; implicitHeight: 26
                    contentItem: Label {
                        text: exportBtn.text
                        color: Tok.DesignTokens.textSecondary
                        font.pixelSize: Tok.DesignTokens.fontSizeXs
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        color: exportBtn.hovered ? Tok.DesignTokens.bgHover : "transparent"
                        radius: Tok.DesignTokens.radiusSm
                    }
                    onClicked: exportDialogLoader.item.open()
                }

                // 网格模式切换（v4.0）
                ToolButton {
                    id: gridToggleBtn
                    text: root.gridMode === "lines" ? "\u25A6" : "\u2022\u2022"
                    ToolTip.text: "切换网格模式（线条/点阵）"
                    ToolTip.visible: gridToggleArea.containsMouse
                    ToolTip.delay: 500
                    implicitWidth: 28
                    implicitHeight: 28
                    contentItem: Label {
                        text: gridToggleBtn.text
                        color: Tok.DesignTokens.textSecondary
                        font.pixelSize: Tok.DesignTokens.fontSizeLg
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        color: gridToggleArea.containsMouse ? Tok.DesignTokens.bgHover : "transparent"
                        radius: Tok.DesignTokens.radiusSm
                    }
                    MouseArea {
                        id: gridToggleArea
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: root.gridMode = root.gridMode === "lines" ? "dots" : "lines"
                    }
                }
            }
        }

        // ============ 快捷键 ============
        Shortcut {
            sequences: [StandardKey.Undo]
            onActivated: { if (editViewBridge.canUndo) editViewBridge.undo() }
        }
        Shortcut {
            sequences: [StandardKey.Redo]
            onActivated: { if (editViewBridge.canRedo) editViewBridge.redo() }
        }
        // P1-C13 修复（交互评估 UI-016）：补充常用文件操作快捷键
        // 之前顶栏有"保存/加载"按钮但无快捷键，用户需鼠标点击，效率低
        Shortcut {
            sequences: [StandardKey.Save]
            onActivated: {
                if (editViewBridge.isDirty) saveFileDialog.open()
            }
        }
        Shortcut {
            sequences: [StandardKey.Open]
            onActivated: loadFileDialog.open()
        }
        Shortcut {
            sequences: [StandardKey.New]
            onActivated: editViewBridge.newScheme()
        }
        Shortcut {
            sequences: ["F2"]
            enabled: root.currentSelectedNodeId !== ""
            onActivated: {
                if (root.currentSelectedNodeId)
                    root.openEditorForNode(root.currentSelectedNodeId)
            }
        }
        Shortcut {
            sequences: ["Ctrl+F"]  // 适应画布
            onActivated: root.fitCanvasToNodes()
        }
        Shortcut {
            sequences: ["Ctrl+0"]  // 重置缩放
            onActivated: root.resetCanvasView()
        }
        Shortcut {
            sequences: ["Ctrl+="]  // 放大
            onActivated: root.canvasZoom = Math.min(3.0, root.canvasZoom + 0.1)
        }
        Shortcut {
            sequences: ["Ctrl+-"]  // 缩小
            onActivated: root.canvasZoom = Math.max(0.25, root.canvasZoom - 0.1)
        }
        Shortcut {
            sequences: [StandardKey.Quit]  // Ctrl+Q 退出（Qt 标准）
            onActivated: Qt.quit()
        }
        Shortcut {
            sequences: ["Ctrl+K"]
            onActivated: { searchField.forceActiveFocus(); searchField.selectAll() }
        }
        // v3.0.0：Delete 键删除选中节点或选中连线
        // P1-B06 修复：条件改为 selectedNodeIds.length > 0（框选后 currentSelectedNodeId 可能为空）
        Shortcut {
            sequences: [StandardKey.Delete]
            enabled: root.selectedNodeIds.length > 0 || root.selectedConnectionIndex >= 0
            onActivated: {
                if (root.selectedNodeIds.length > 0) {
                    root.deleteSelectedNodes()
                } else if (root.selectedConnectionIndex >= 0) {
                    root.deleteSelectedConnection()
                }
            }
        }
        // P1-B07 修复：Ctrl+C/V/A 复制/粘贴/全选
        Shortcut {
            sequences: [StandardKey.Copy]
            enabled: root.selectedNodeIds.length > 0
            onActivated: root.copySelectedNodes()
        }
        Shortcut {
            sequences: [StandardKey.Paste]
            enabled: root.clipboardNodes.length > 0
            onActivated: root.pasteNodes()
        }
        Shortcut {
            sequences: [StandardKey.SelectAll]
            onActivated: root.selectAllNodes()
        }
        // P1-B02 修复：空格键切换平移模式（按住空格 + 左键拖拽 = 平移画布）
        // 用 Keys 处理 press/release（Shortcut 不支持 release）
        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Space && !root.spacePressed) {
                root.spacePressed = true
                event.accepted = true
            }
            // P1-B08 修复：方向键键盘导航
            else if (event.key === Qt.Key_Left) {
                root.navigateNode("left"); event.accepted = true
            } else if (event.key === Qt.Key_Right) {
                root.navigateNode("right"); event.accepted = true
            } else if (event.key === Qt.Key_Up) {
                root.navigateNode("up"); event.accepted = true
            } else if (event.key === Qt.Key_Down) {
                root.navigateNode("down"); event.accepted = true
            }
        }
        Keys.onReleased: function(event) {
            if (event.key === Qt.Key_Space) {
                root.spacePressed = false
                root.isPanning = false
                event.accepted = true
            }
        }
        // v3.0.0：Escape 取消选择
        // v4.0 P0 修复：建连中优先取消建连，否则取消选择
        Shortcut {
            sequences: [StandardKey.Cancel]
            onActivated: {
                if (root.pendingConnectionFrom !== "") {
                    root.pendingConnectionFrom = ""
                    root.showToast("info", "已取消连接")
                    return
                }
                root.currentSelectedNodeId = ""
                root.selectedNodeIds = []
                root.selectedConnectionIndex = -1
            }
        }

        // ============ 主体三栏 SplitView ============
        SplitView {
            id: splitView
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Horizontal

            // ============ 左侧：算子库 ============
            Rectangle {
                id: leftPanel
                SplitView.preferredWidth: Tok.DesignTokens.leftPanelDefault
                SplitView.minimumWidth: Tok.DesignTokens.leftPanelMin
                color: Tok.DesignTokens.bgPanel
                visible: root.leftPanelVisible

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Tok.DesignTokens.space2
                    spacing: Tok.DesignTokens.space1

                    // v5.0：标题
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Label {
                            text: "算子库"
                            color: Tok.DesignTokens.accentPrimary
                            font.bold: true
                            font.pixelSize: Tok.DesignTokens.fontSizeBase  // v5.0：Lg 14→Base 13
                            font.family: Tok.DesignTokens.fontFamilyCJK
                            Layout.fillWidth: true
                        }
                        // v5.1：折叠左侧面板按钮
                        Rectangle {
                            width: 24; height: 24; radius: 4
                            color: collapseLeftArea.containsMouse
                                ? Tok.DesignTokens.bgHover : "transparent"
                            Label {
                                anchors.centerIn: parent
                                text: "\u25C0"
                                color: Tok.DesignTokens.textSecondary
                                font.pixelSize: 12
                            }
                            MouseArea {
                                id: collapseLeftArea
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.leftPanelVisible = false
                            }
                        }
                    }

                    // 搜索栏
                    Rectangle {
                        Layout.fillWidth: true
                        height: Tok.DesignTokens.searchBarHeight
                        color: Tok.DesignTokens.bgSurface
                        radius: Tok.DesignTokens.radiusMd
                        border.color: searchField.activeFocus
                            ? Tok.DesignTokens.borderFocus
                            : Tok.DesignTokens.borderDefault
                        border.width: Tok.DesignTokens.borderWidth

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: Tok.DesignTokens.space2
                            anchors.rightMargin: Tok.DesignTokens.space1
                            spacing: Tok.DesignTokens.space1

                            Label {
                                text: "\uD83D\uDD0D"  // 🔍
                                font.pixelSize: Tok.DesignTokens.fontSizeSm
                                color: Tok.DesignTokens.textTertiary
                            }
                            TextField {
                                id: searchField
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                color: Tok.DesignTokens.textPrimary
                                // v3.0.0：Sm 12→Base 13，搜索文字更清晰
                                font.pixelSize: Tok.DesignTokens.fontSizeBase
                                font.family: Tok.DesignTokens.fontFamilyCJK
                                placeholderText: "搜索算子... (Ctrl+K)"
                                placeholderTextColor: Tok.DesignTokens.textPlaceholder
                                background: Rectangle { color: "transparent" }
                                verticalAlignment: TextInput.AlignVCenter
                                Accessible.role: Accessible.SearchField
                                Accessible.name: "算子搜索"
                                Accessible.description: "输入关键词搜索算子"
                                onTextChanged: {
                                    root.searchKeyword = text
                                    operatorList.model = null
                                    operatorList.model = operatorList.buildModel()
                                }
                            }
                            ToolButton {
                                visible: searchField.text !== ""
                                text: "\u2715"  // ✕
                                // v3.0.0：Xs 11→Sm 12，关闭按钮清晰
                                font.pixelSize: Tok.DesignTokens.fontSizeSm
                                Layout.preferredWidth: 22
                                Layout.preferredHeight: 22
                                onClicked: {
                                    searchField.text = ""
                                    searchField.focus = false
                                }
                            }
                        }
                    }

                    // 筛选标签
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 3
                        Repeater {
                            model: [
                                { key: "all",       label: "全部" },
                                { key: "favorites", label: "\u2605 收藏" },
                                { key: "recents",   label: "最近" },
                                { key: "commons",   label: "常用" }
                            ]
                            delegate: Rectangle {
                                Layout.preferredWidth: (leftPanel.width - 16) / 4  // v5.0：简化宽度计算
                                height: Tok.DesignTokens.filterTabHeight
                                radius: Tok.DesignTokens.radiusSm
                                color: root.activeFilter === modelData.key
                                    ? Tok.DesignTokens.accentPrimary
                                    : Tok.DesignTokens.bgHover

                                Label {
                                    anchors.centerIn: parent
                                    text: modelData.label
                                    color: root.activeFilter === modelData.key
                                        ? Tok.DesignTokens.textPrimary
                                        : Tok.DesignTokens.textTertiary
                                    font.pixelSize: Tok.DesignTokens.fontSizeXs
                                    font.bold: root.activeFilter === modelData.key
                                    font.family: Tok.DesignTokens.fontFamilyCJK
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        root.activeFilter = modelData.key
                                        operatorList.model = null
                                        operatorList.model = operatorList.buildModel()
                                    }
                                }
                                Accessible.role: Accessible.Button
                                Accessible.name: modelData.label + "筛选"
                            }
                        }
                    }

                    // 计数
                    Label {
                        id: opCountLabel
                        text: "共 " + getFilteredOperators().length + " 个算子"
                        color: Tok.DesignTokens.textSecondary
                        // v3.0.0：Xs 11→Sm 12，提升计数文字可读性
                        font.pixelSize: Tok.DesignTokens.fontSizeSm
                        font.family: Tok.DesignTokens.fontFamilyCJK
                    }

                    // 分类→子分组 折叠树
                    ListView {
                        id: operatorList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 0
                        // v3.0.1b 修复：删除无效的 `background` 和 `highlight` 属性，
                        // ListView（QtQuick）没有 background 属性，该行导致 QML 解析失败、界面空白。
                        // 改用 highlightFollowsCurrentItem:false + currentIndex:-1 防止白底残留。
                        highlightFollowsCurrentItem: false
                        highlightMoveDuration: 0
                        highlightMoveVelocity: -1
                        currentIndex: -1
                        keyNavigationWraps: false
                        focus: false
                        // v-spec: 算子库飞级菜单 — 左侧仅渲染固定分类列表，不再就地展开
                        property var collapsed: ({})
                        property var filteredList: getFilteredOperators()
                        // v-spec: 悬停展开的当前分类（空=不显示飞级菜单）
                        property string hoveredCategory: ""
                        property int hoveredCategoryIndex: -1
                        // v-spec-fix: 缓存飞级菜单行，避免每次 model 绑定求值都返回新数组
                        // 导致 ListView delegate 反复重建、hover 状态频繁重置（子菜单抖动关闭）
                        property var flyoutRows: []

                        function updateFlyout() {
                            operatorList.flyoutRows = operatorList.flyoutModel()
                        }
                        // [RecDiag] 悬停分类变化点（经 C++ 落盘）
                        onHoveredCategoryChanged: {
                            if (editViewBridge) editViewBridge.logDiag(
                                "[RecDiag] hoveredCategory => <" + operatorList.hoveredCategory + ">")
                            updateFlyout()
                        }

                        // 默认折叠所有分类：组件加载完成后初始化
                        Component.onCompleted: {
                            var ops = getFilteredOperators()
                            var seen = {}
                            for (var i = 0; i < ops.length; ++i) {
                                var cat = ops[i].category || "其他"
                                if (!seen[cat]) {
                                    collapsed[cat] = true
                                    seen[cat] = true
                                }
                            }
                            collapsed = collapsed  // 触发属性变更通知
                            model = buildModel()
                        }

                        // v-spec: 仅生成分类行（保持固定，不展开算子/子分组）
                        function buildModel() {
                            var rows = []
                            var ops = getFilteredOperators()
                            var catMap = {}
                            for (var i = 0; i < ops.length; ++i) {
                                var op = ops[i]
                                var cat = op.category || "其他"
                                if (!catMap[cat]) catMap[cat] = 0
                                catMap[cat]++
                            }
                            var cats = Object.keys(catMap).sort()
                            for (var ci = 0; ci < cats.length; ++ci) {
                                rows.push({ kind: "category", category: cats[ci],
                                    count: catMap[cats[ci]] })
                            }
                            return rows
                        }

                        // v-spec: 飞级菜单内容（当前悬停分类下的算子，按子分组分组）
                        function flyoutModel() {
                            if (!operatorList.hoveredCategory) return []
                            var rows = []
                            // [RecDiag] 诊断：打印悬停分类与算子数据源首项字段（经 C++ 落盘）
                            var _ops = getFilteredOperators()
                            if (_ops.length > 0) {
                                var _f = _ops[0]
                                if (editViewBridge) editViewBridge.logDiag(
                                    "[RecDiag] flyoutModel cat=" + operatorList.hoveredCategory
                                    + " ops=" + _ops.length
                                    + " key0=" + JSON.stringify(Object.keys(_f))
                                    + " cnName=<" + (_f.cnName||"") + "> type=<" + (_f.type||"") + ">")
                            } else {
                                if (editViewBridge) editViewBridge.logDiag(
                                    "[RecDiag] flyoutModel cat=" + operatorList.hoveredCategory
                                    + " ops=0 (getFilteredOperators 为空)")
                            }
                            var ops = getFilteredOperators()
                            var catOps = []
                            for (var i = 0; i < ops.length; ++i) {
                                if ((ops[i].category || "其他") === operatorList.hoveredCategory)
                                    catOps.push(ops[i])
                            }
                            var sgMap = {}, noSg = []
                            for (var j = 0; j < catOps.length; ++j) {
                                var sg = catOps[j].subGroup || ""
                                if (sg) {
                                    if (!sgMap[sg]) sgMap[sg] = []
                                    sgMap[sg].push(catOps[j])
                                } else { noSg.push(catOps[j]) }
                            }
                            // v-spec-fix: 将 name/type 扁平化到行对象，避免委托内经 modelData.meta.cnName
                            // 嵌套访问 QVariantMap 导致名称显示为空；同时保留 meta 供添加算子使用
                            function opRow(op) {
                                return {
                                    kind: "operator",
                                    category: operatorList.hoveredCategory,
                                    subGroup: op.subGroup || "",
                                    name: (op.cnName || op.type || ""),
                                    type: op.type || "",
                                    meta: op
                                }
                            }
                            for (var n = 0; n < noSg.length; ++n)
                                rows.push(opRow(noSg[n]))
                            var sgKeys = Object.keys(sgMap).sort()
                            for (var s = 0; s < sgKeys.length; ++s) {
                                rows.push({ kind: "subGroup", category: operatorList.hoveredCategory,
                                    subGroup: sgKeys[s] })
                                for (var o = 0; o < sgMap[sgKeys[s]].length; ++o)
                                    rows.push(opRow(sgMap[sgKeys[s]][o]))
                            }
                            return rows
                        }

                        // v-spec: 飞级菜单高度（按行类型累加）
                        // v-fix: 面板结构 = ColumnLayout(margins 上下各3=6) + 分类标题(高26) + 算子列表。
                        // 若只累加 6+行高，则 flyoutList 实际高度 = Σ行高 - 26，底部算子行被裁剪，
                        // 导致"算子名称不显示"。必须计入 6(margin) + 26(标题)。
                        function flyoutHeight() {
                            var rows = operatorList.flyoutRows
                            var h = 6 + 26
                            for (var i = 0; i < rows.length; ++i) {
                                h += rows[i].kind === "operator"
                                    ? Tok.DesignTokens.operatorRowHeight
                                    : Tok.DesignTokens.subGroupRowHeight
                            }
                            return h
                        }

                        // v-spec: 延迟收起定时器（悬停切换更平滑，避免闪跳）
                        // v-fix: 120→250ms，给鼠标从分类行横移进入子菜单留出更充裕时间，
                        // 避免路径稍长时定时器在鼠标进入面板前触发导致"立刻关闭"
                        // v-fix6: 兜底防护 —— 定时器触发时若鼠标仍停留在二级框内
                        // (flyoutHoverArea.containsMouse)，则说明 catArea.onExited 的 start()
                        // 与 flyoutHoverArea.onEntered 的 stop() 竞争导致误触发，直接取消收起，
                        // 彻底杜绝"悬停一级分类二级框仍自动关闭"。
                        Timer {
                            id: flyoutCollapseTimer
                            interval: 250
                            // [RecDiag] 收起触发点（经 C++ 落盘）
                            onTriggered: {
                                var inFly = flyoutHoverArea && flyoutHoverArea.containsMouse
                                if (editViewBridge) editViewBridge.logDiag(
                                    "[RecDiag] flyoutCollapseTimer TRIGGERED cat=<<" + operatorList.hoveredCategory
                                    + ">> inFlyout=" + (inFly ? "yes" : "no"))
                                if (inFly) return  // 鼠标仍在二级框内，取消收起
                                operatorList.hoveredCategory = ""
                            }
                        }

                        model: buildModel()

                        delegate: Rectangle {
                            width: ListView.view.width
                            height: Tok.DesignTokens.categoryRowHeight

                            // v-spec: 分类行（固定列表，悬停触发右侧飞级菜单）
                            Rectangle {
                                anchors.fill: parent
                                color: catArea.containsMouse ? Tok.DesignTokens.bgHover : Tok.DesignTokens.bgHeader
                                radius: Tok.DesignTokens.radiusSm

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: Tok.DesignTokens.space1
                                    anchors.rightMargin: Tok.DesignTokens.space1
                                    spacing: Tok.DesignTokens.space1

                                    Label {
                                        text: "\u25B8"
                                        color: Tok.DesignTokens.accentPrimary
                                        font.pixelSize: Tok.DesignTokens.fontSizeSm
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: 14
                                    }
                                    Rectangle {
                                        width: 10; height: 10; radius: 5
                                        color: Tok.DesignTokens.categoryColor(modelData.category)
                                        anchors.verticalCenter: parent.verticalCenter
                                    }
                                    Label {
                                        text: modelData.category
                                        color: Tok.DesignTokens.textPrimary
                                        font.bold: true
                                        font.pixelSize: Tok.DesignTokens.fontSizeBase
                                        font.family: Tok.DesignTokens.fontFamilyCJK
                                        font.letterSpacing: 0.5
                                        lineHeight: Tok.DesignTokens.lineHeightNormal
                                        lineHeightMode: Text.ProportionalHeight
                                        Layout.fillWidth: true
                                        elide: Text.ElideRight
                                    }
                                    Label {
                                        text: String(modelData.count)
                                        color: Tok.DesignTokens.textTertiary
                                        font.pixelSize: Tok.DesignTokens.fontSizeXs
                                        font.family: Tok.DesignTokens.fontFamilyCJK
                                    }
                                }
                                MouseArea {
                                    id: catArea
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    // v-spec: 悬停展开飞级菜单
                                    onEntered: {
                                        flyoutCollapseTimer.stop()
                                        operatorList.hoveredCategory = modelData.category
                                        operatorList.hoveredCategoryIndex = index
                                    }
                                    onExited: {
                                        // v-fix9: 二级框 operatorFlyout 向右弹出时会覆盖分类行右侧，
                                        // 导致 catArea 丢失 hover 而误触发 onExited。此处用几何判断：
                                        // 鼠标位置若仍落在二级框内，则代表"仍在菜单区"，只停表、不收起。
                                        // 收起统一交由 flyoutHoverArea.onExited / 算子行 onExited（真正离开菜单区）负责。
                                        var _p = operatorList.mapToItem(root, mouse.x, mouse.y)
                                        var _inFly = (operatorFlyout && operatorFlyout.visible
                                            && _p.x >= operatorFlyout.x && _p.x <= operatorFlyout.x + operatorFlyout.width
                                            && _p.y >= operatorFlyout.y && _p.y <= operatorFlyout.y + operatorFlyout.height)
                                        if (editViewBridge) editViewBridge.logDiag(
                                            "[RecDiag] catArea EXITED cat=<<" + modelData.category
                                            + ">> mouseRoot=(" + Math.round(_p.x) + "," + Math.round(_p.y) + ")"
                                            + " fly=(" + Math.round(operatorFlyout.x) + "," + Math.round(operatorFlyout.y)
                                            + " " + Math.round(operatorFlyout.width) + "x" + Math.round(operatorFlyout.height) + ")"
                                            + " inFly=" + (_inFly ? "yes" : "no"))
                                        if (_inFly) { flyoutCollapseTimer.stop(); return }
                                        flyoutCollapseTimer.start()
                                    }
                                    onClicked: {
                                        flyoutCollapseTimer.stop()
                                        operatorList.hoveredCategory = modelData.category
                                        operatorList.hoveredCategoryIndex = index
                                    }
                                    Accessible.role: Accessible.Button
                                    Accessible.name: modelData.category + " - " + modelData.count + " 个算子"
                                    Accessible.description: modelData.category + " 分类，悬停展开算子列表"
                                }
                            }
                        }
                        ScrollBar.vertical: ScrollBar {}
                    }

                    // v2.7.0 O1a：算子库面板底部入口按钮
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.topMargin: Tok.DesignTokens.space2
                        spacing: Tok.DesignTokens.space2

                        // 导入算子按钮（primary）
                        Button {
                            Layout.fillWidth: true
                            text: "导入算子"
                            implicitHeight: Tok.DesignTokens.controlHeight
                            palette.buttonText: "white"
                            background: Rectangle {
                                color: parent.hovered ? Tok.DesignTokens.accentPrimaryHover
                                                      : Tok.DesignTokens.accentPrimary
                                border.color: Tok.DesignTokens.accentPrimary
                                radius: Tok.DesignTokens.radiusMd
                            }
                            onClicked: {
                                if (operatorImportDialogLoader.item) {
                                    operatorImportDialogLoader.item.open()
                                }
                            }
                        }

                        // 已导入算子按钮
                        Button {
                            Layout.fillWidth: true
                            text: {
                                var count = 0
                                if (editViewBridge && editViewBridge.operatorLibraryBridge) {
                                    count = editViewBridge.operatorLibraryBridge.importedCount()
                                }
                                return "已导入 (" + count + ")"
                            }
                            implicitHeight: Tok.DesignTokens.controlHeight
                            palette.buttonText: Tok.DesignTokens.textSecondary
                            background: Rectangle {
                                color: parent.hovered ? Tok.DesignTokens.bgHover
                                                      : Tok.DesignTokens.bgSurface
                                border.color: parent.hovered ? Tok.DesignTokens.accentPrimary
                                                             : Tok.DesignTokens.borderDefault
                                radius: Tok.DesignTokens.radiusMd
                            }
                            onClicked: {
                                if (importedOperatorsPanelLoader.item) {
                                    importedOperatorsPanelLoader.item.open()
                                }
                            }
                        }
                    }
                }
            }

            // ============ 中央：画布 + 底部变量区 ============
            SplitView {
                id: centerColumn
                orientation: Qt.Vertical
                SplitView.fillWidth: true
                SplitView.minimumWidth: 200

                Rectangle {
                    id: canvasFrame
                    SplitView.fillHeight: true
                    color: Tok.DesignTokens.bgCanvas

                // 鼠标滚轮缩放（v3.0.0）— P1-B09 修复：以鼠标为中心缩放
                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.NoButton
                    onWheel: function(wheel) {
                        var delta = wheel.angleDelta.y / 120
                        var oldZoom = root.canvasZoom
                        var newZoom = Math.max(0.25, Math.min(3.0, oldZoom + delta * 0.1))
                        if (newZoom === oldZoom) return
                        // 鼠标下的世界坐标（缩放前）
                        var worldX = (wheel.x - root.canvasOffsetX) / oldZoom
                        var worldY = (wheel.y - root.canvasOffsetY) / oldZoom
                        // 缩放后调整 offset 使鼠标下的世界坐标保持在鼠标位置
                        root.canvasOffsetX = wheel.x - worldX * newZoom
                        root.canvasOffsetY = wheel.y - worldY * newZoom
                        root.canvasZoom = newZoom
                    }
                }

                // P1-B02 修复：画布平移交互（中键拖拽 或 空格+左键拖拽）
                MouseArea {
                    id: panArea
                    anchors.fill: parent
                    acceptedButtons: Qt.MiddleButton | (root.spacePressed ? Qt.LeftButton : Qt.NoButton)
                    cursorShape: root.isPanning ? Qt.ClosedHandCursor
                                : (root.spacePressed ? Qt.OpenHandCursor : Qt.ArrowCursor)
                    z: 1000  // 平移时覆盖节点交互
                    onPressed: function(mouse) {
                        root.isPanning = true
                        root.panStartX = mouse.x
                        root.panStartY = mouse.y
                        root.panStartOffsetX = root.canvasOffsetX
                        root.panStartOffsetY = root.canvasOffsetY
                    }
                    onPositionChanged: function(mouse) {
                        if (root.isPanning) {
                            root.canvasOffsetX = root.panStartOffsetX + (mouse.x - root.panStartX)
                            root.canvasOffsetY = root.panStartOffsetY + (mouse.y - root.panStartY)
                        }
                    }
                    onReleased: root.isPanning = false
                }

                // v4.0: 框选交互 MouseArea（空白区域拖拽 = 框选多选）
                // P1-B12 修复：增加右键处理，弹出画布空白右键菜单
                MouseArea {
                    id: canvasRubberBandArea
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    z: -1  // 低于节点卡片，节点上的点击不会被此区域拦截

                    onPressed: function(mouse) {
                        if (mouse.button === Qt.LeftButton) {
                            root.rubberBandActive = true
                            root.rbStartX = mouse.x
                            root.rbStartY = mouse.y
                            root.rbEndX = mouse.x
                            root.rbEndY = mouse.y
                            root.selectedConnectionIndex = -1
                        }
                    }

                    onClicked: function(mouse) {
                        // P1-B12：右键空白区域弹出画布菜单
                        if (mouse.button === Qt.RightButton) {
                            canvasContextMenu.popup()
                        }
                    }

                    onPositionChanged: function(mouse) {
                        if (root.rubberBandActive) {
                            root.rbEndX = mouse.x
                            root.rbEndY = mouse.y
                        }
                    }

                    onReleased: function(mouse) {
                        if (root.rubberBandActive) {
                            root.rubberBandActive = false
                            // 判断框选是否有实际面积（避免单击误触）
                            var dx = Math.abs(root.rbEndX - root.rbStartX)
                            var dy = Math.abs(root.rbEndY - root.rbStartY)
                            if (dx > 5 || dy > 5) {
                                // P1-B01 修复：框选用屏幕坐标，节点位置需 world*zoom+offset 转屏幕坐标
                                var selX1 = Math.min(root.rbStartX, root.rbEndX)
                                var selY1 = Math.min(root.rbStartY, root.rbEndY)
                                var selX2 = Math.max(root.rbStartX, root.rbEndX)
                                var selY2 = Math.max(root.rbStartY, root.rbEndY)
                                var inRect = []
                                var nodes = editViewBridge.currentNodes
                                var z = root.canvasZoom
                                var ox = root.canvasOffsetX
                                var oy = root.canvasOffsetY
                                for (var i = 0; i < nodes.length; i++) {
                                    var nd = nodes[i]
                                    var nx = nd.x * z + ox
                                    var ny = nd.y * z + oy
                                    var nw = Tok.DesignTokens.nodeCardWidth * z
                                    var nh = (Tok.DesignTokens.nodeCardHeight + 4) * z  // v5.0：8→4
                                    if (nx + nw > selX1 && nx < selX2 && ny + nh > selY1 && ny < selY2) {
                                        inRect.push(nd.id)
                                    }
                                }
                                root.selectedNodeIds = inRect

                                // v2.5.0 功能 2 增强：框选连线端点（单选）
                                var conns = editViewBridge.connections
                                var nodeW = Tok.DesignTokens.nodeCardWidth
                                var nodeH = Tok.DesignTokens.nodeCardHeight
                                for (var ci = 0; ci < conns.length; ci++) {
                                    var conn = conns[ci]
                                    var fromNode = null, toNode = null
                                    for (var ni = 0; ni < nodes.length; ni++) {
                                        if (nodes[ni].id === conn.fromId) fromNode = nodes[ni]
                                        if (nodes[ni].id === conn.toId) toNode = nodes[ni]
                                    }
                                    if (!fromNode || !toNode) continue
                                    var fxx = (fromNode.x + nodeW) * z + ox
                                    var fyy = (fromNode.y + nodeH / 2) * z + oy
                                    var txx = toNode.x * z + ox
                                    var tyy = (toNode.y + nodeH / 2) * z + oy
                                    if ((fxx > selX1 && fxx < selX2 && fyy > selY1 && fyy < selY2) ||
                                        (txx > selX1 && txx < selX2 && tyy > selY1 && tyy < selY2)) {
                                        root.selectedConnectionIndex = ci
                                    }
                                }
                            }
                        }
                    }

                    onCanceled: {
                        root.rubberBandActive = false
                    }
                }

                // v4.0: 框选矩形
                Rectangle {
                    id: rubberBand
                    visible: root.rubberBandActive
                    x: Math.min(root.rbStartX, root.rbEndX)
                    y: Math.min(root.rbStartY, root.rbEndY)
                    width: Math.abs(root.rbEndX - root.rbStartX)
                    height: Math.abs(root.rbEndY - root.rbStartY)
                    color: Qt.rgba(0.486, 0.302, 1.0, 0.08)
                    border.color: Qt.rgba(0.486, 0.302, 1.0, 0.4)
                    border.width: 1
                    radius: 2
                    z: 100
                }

                // 画布网格背景 — P1-B01 修复：网格随 offset 平移
                Canvas {
                    anchors.fill: parent
                    onPaint: {
                        var ctx = getContext("2d")
                        ctx.clearRect(0, 0, width, height)
                        var grid = 40 * root.canvasZoom
                        if (grid < 5) grid = 5
                        // P1-B01：网格起点对齐到 offset，平移时网格跟随
                        var startX = root.canvasOffsetX % grid
                        var startY = root.canvasOffsetY % grid
                        if (startX < 0) startX += grid
                        if (startY < 0) startY += grid

                        if (root.gridMode === "lines") {
                            // 线条网格模式
                            ctx.strokeStyle = "#1F1F1F"
                            ctx.lineWidth = 0.5
                            for (var x = startX; x < width; x += grid) {
                                ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, height); ctx.stroke()
                            }
                            for (var y = startY; y < height; y += grid) {
                                ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(width, y); ctx.stroke()
                            }
                        } else {
                            // 点阵网格模式
                            ctx.fillStyle = "#1F1F1F"
                            for (var x = startX; x < width; x += grid) {
                                for (var y = startY; y < height; y += grid) {
                                    ctx.beginPath()
                                    ctx.arc(x, y, 1, 0, Math.PI * 2)
                                    ctx.fill()
                                }
                            }
                        }
                    }
                    Connections {
                        target: root
                        function onCanvasZoomChanged() { parent.requestPaint() }
                        function onGridModeChanged() { parent.requestPaint() }
                        // P1-B01：offset 变化时网格重绘
                        function onCanvasOffsetXChanged() { parent.requestPaint() }
                        function onCanvasOffsetYChanged() { parent.requestPaint() }
                    }
                }

                // 连线层 — P1-B04 修复：合并为单 Canvas，O(n) 一次绘制所有连线
                // 之前 Repeater + N 个全画布 Canvas 复杂度 O(n×area)，100 节点卡顿
                // v2.5.0 功能 2：支持悬停高亮（hoveredConnectionIndex >= 0 时该连线高亮红色）
                Canvas {
                    id: connectionsCanvas
                    anchors.fill: parent
                    z: 1  // 在节点之下
                    // v2.5.0 修复：拖拽中节点实时位置字典 {nodeId: {x, y}}
                    // moveNodeLive 不再 emit currentNodesChanged（避免 Repeater 重建中断拖拽），
                    // 改 emit nodePositionChanged，此字典接收最新世界坐标
                    property var livePositions: ({})

                    onPaint: {
                        var ctx = getContext("2d")
                        ctx.clearRect(0, 0, width, height)
                        var conns = editViewBridge.connections
                        var z = root.canvasZoom

                        // spec: editor-output-connection-optimization Task5
                        // 分层处理 + 交叉避让：绘制前一次性计算所有连线几何（O(n)）
                        var maps = root.buildConnectionMaps()

                        for (var ci = 0; ci < conns.length; ci++) {
                            var g = root.connectionCurve(ci, maps)
                            if (!g) continue
                            var fx = g.fx, fy = g.fy, tx = g.tx, ty = g.ty
                            var cx1 = g.cx1, cy1 = g.cy1, cx2 = g.cx2, cy2 = g.cy2

                            // v2.5.0 功能 2 增强：选中态优先于悬停态
                            var isSelected = (ci === root.selectedConnectionIndex)
                            var isHovered = (ci === root.hoveredConnectionIndex)
                            ctx.strokeStyle = isSelected ? "#FFD700"
                                              : (isHovered ? "#e74c3c" : Tok.DesignTokens.accentPrimary)
                            ctx.lineWidth = isSelected ? 4 : (isHovered ? 3 : 2)
                            ctx.setLineDash((isSelected || isHovered) ? [] : [6, 3])
                            // 选中态添加发光效果
                            ctx.shadowColor = isSelected ? "#FFD700" : "transparent"
                            ctx.shadowBlur = isSelected ? 10 : 0

                            ctx.beginPath()
                            ctx.moveTo(fx, fy)
                            ctx.bezierCurveTo(cx1, cy1, cx2, cy2, tx, ty)
                            ctx.stroke()

                            // 重置阴影，避免箭头也带发光导致锯齿
                            ctx.shadowBlur = 0
                            var angle = Math.atan2(ty - fy, tx - fx)
                            var ax = tx - 8 * Math.cos(angle)
                            var ay = ty - 8 * Math.sin(angle)
                            ctx.fillStyle = isSelected ? "#FFD700"
                                              : (isHovered ? "#e74c3c" : Tok.DesignTokens.accentPrimary)
                            ctx.beginPath()
                            ctx.moveTo(ax, ay)
                            ctx.lineTo(ax - 6 * Math.cos(angle - 0.5), ay - 6 * Math.sin(angle - 0.5))
                            ctx.lineTo(ax - 6 * Math.cos(angle + 0.5), ay - 6 * Math.sin(angle + 0.5))
                            ctx.closePath()
                            ctx.fill()

                            // spec: editor-output-connection-optimization Task5
                            // 连线标签：在中点绘制下游端口名（toPort），浅色小字低透明度
                            ctx.globalAlpha = 0.55
                            ctx.fillStyle = Tok.DesignTokens.textSecondary
                            ctx.font = "bold " + Math.max(9, Math.round(10 * z)) + "px sans-serif"
                            ctx.textAlign = "center"
                            ctx.textBaseline = "middle"
                            ctx.fillText(g.toPort, g.ux, g.uy)
                            ctx.globalAlpha = 1.0
                        }
                    }
                    Connections {
                        target: root
                        function onCanvasZoomChanged() { connectionsCanvas.requestPaint() }
                        function onCanvasOffsetXChanged() { connectionsCanvas.requestPaint() }
                        function onCanvasOffsetYChanged() { connectionsCanvas.requestPaint() }
                        // v2.5.0 功能 2：悬停/选中状态变化时重绘
                        function onHoveredConnectionIndexChanged() { connectionsCanvas.requestPaint() }
                        function onSelectedConnectionIndexChanged() { connectionsCanvas.requestPaint() }
                    }
                    Connections {
                        target: editViewBridge
                        function onCurrentNodesChanged() {
                            // v2.5.0 修复：currentNodesChanged 时清空 livePositions（数据已更新）
                            connectionsCanvas.livePositions = ({})
                            connectionsCanvas.requestPaint()
                        }
                        function onConnectionsChanged() { connectionsCanvas.requestPaint() }
                        // v2.5.0 修复：拖拽中节点位置变化 → 更新 livePositions + 重绘连线
                        // 不触发 currentNodesChanged，避免 Repeater 重建 delegate 中断拖拽
                        function onNodePositionChanged(nodeId, worldX, worldY) {
                            var pos = connectionsCanvas.livePositions
                            pos[nodeId] = {x: worldX, y: worldY}
                            connectionsCanvas.livePositions = pos
                            connectionsCanvas.requestPaint()
                        }
                    }
                }

                // v2.5.0 功能 2：连线点击/悬停 MouseArea
                // z:1 与 connectionsCanvas 同层，后声明在上层
                // propagateComposedEvents 让未命中连线的事件穿透到下层（节点拖拽/画布平移）
                // onPressed 总是放行（不阻挡节点拖拽）；onClicked 命中连线时删除，否则放行
                MouseArea {
                    id: connectionHitArea
                    anchors.fill: parent
                    z: 1  // 与 connectionsCanvas 同层，后声明在上层
                    enabled: root.pendingConnectionFrom === ""  // 建连中禁用删连
                    hoverEnabled: true
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    propagateComposedEvents: true

                    // 悬停检测：更新 hoveredConnectionIndex
                    onPositionChanged: function(mouse) {
                        var hitIdx = root.hitTestConnection(mouse.x, mouse.y)
                        if (hitIdx !== root.hoveredConnectionIndex) {
                            root.hoveredConnectionIndex = hitIdx
                            cursorShape = hitIdx >= 0 ? Qt.PointingHandCursor : Qt.ArrowCursor
                        }
                        // 不阻止事件传播（让节点 hover 正常工作）
                        mouse.accepted = false
                    }

                    // 离开画布时清除悬停
                    onExited: {
                        root.hoveredConnectionIndex = -1
                        cursorShape = Qt.ArrowCursor
                    }

                    // 按下时检测是否命中连线，决定是否拦截事件
                    onPressed: function(mouse) {
                        var hitIdx = root.hitTestConnection(mouse.x, mouse.y)
                        if (hitIdx >= 0) {
                            // 命中连线时拦截事件，确保 onClicked 能被触发
                            mouse.accepted = true
                        } else {
                            // 未命中连线时放行，让节点拖拽/画布平移正常工作
                            mouse.accepted = false
                        }
                    }

                    // 点击：命中连线时选中，右键弹出删除菜单
                    onClicked: function(mouse) {
                        var hitIdx = root.hitTestConnection(mouse.x, mouse.y)
                        if (hitIdx >= 0) {
                            var conns = editViewBridge.connections
                            if (hitIdx < conns.length) {
                                // 选中连线时清空节点选择，避免 Delete 行为歧义
                                root.selectedNodeIds = []
                                root.currentSelectedNodeId = ""
                                root.selectedConnectionIndex = hitIdx
                                root.hoveredConnectionIndex = -1  // 清空悬停态，避免视觉冲突
                                var conn = conns[hitIdx]
                                if (mouse.button === Qt.RightButton) {
                                    // 右键弹删除菜单
                                    connDeleteMenu.connectionData = conn
                                    connDeleteMenu.popup()
                                }
                                mouse.accepted = true
                            }
                        } else {
                            // 点击空白处取消连线选中，事件穿透到下层
                            root.selectedConnectionIndex = -1
                            mouse.accepted = false
                        }
                    }
                }

                // v2.5.0 功能 2：右键删除连接确认菜单
                Menu {
                    id: connDeleteMenu
                    property var connectionData: null
                    MenuItem {
                        text: "\u2715 删除此连接"
                        onTriggered: {
                            if (connDeleteMenu.connectionData) {
                                var c = connDeleteMenu.connectionData
                                editViewBridge.disconnectEdge(c.fromId, c.fromPort, c.toId, c.toPort)
                                root.showToast("info", "已删除连接")
                                root.hoveredConnectionIndex = -1
                            }
                        }
                    }
                }

                // v4.0 P0 修复：建连中的临时连线（跟随鼠标）
                Canvas {
                    id: pendingConnCanvas
                    anchors.fill: parent
                    visible: root.pendingConnectionFrom !== ""
                    z: 50   // 在节点卡片之下、网格之上
                    onPaint: {
                        var ctx = getContext("2d")
                        ctx.clearRect(0, 0, width, height)
                        var nodes = editViewBridge.currentNodes
                        var fromNode = null
                        for (var i = 0; i < nodes.length; ++i) {
                            if (nodes[i].id === root.pendingConnectionFrom) {
                                fromNode = nodes[i]
                                break
                            }
                        }
                        if (!fromNode) return
                        // P1-B01：屏幕坐标 = world * zoom + offset
                        var fx = (fromNode.x + Tok.DesignTokens.nodeCardWidth) * root.canvasZoom + root.canvasOffsetX
                        var fy = (fromNode.y + Tok.DesignTokens.nodeCardHeight / 2) * root.canvasZoom + root.canvasOffsetY
                        var tx = root.pendingConnMouseX
                        var ty = root.pendingConnMouseY
                        ctx.strokeStyle = Tok.DesignTokens.accentPrimary
                        ctx.lineWidth = 2
                        ctx.setLineDash([6, 3])
                        ctx.beginPath()
                        var cx1 = fx + Math.abs(tx - fx) * 0.4
                        var cx2 = tx - Math.abs(tx - fx) * 0.4
                        ctx.moveTo(fx, fy)
                        ctx.bezierCurveTo(cx1, fy, cx2, ty, tx, ty)
                        ctx.stroke()
                    }
                }

                // v4.0 P0 修复：画布鼠标追踪（用于临时连线跟随鼠标）
                MouseArea {
                    anchors.fill: parent
                    enabled: root.pendingConnectionFrom !== ""
                    hoverEnabled: true
                    z: 0   // 最底层，不干扰节点交互
                    onPositionChanged: function(mouse) {
                        root.pendingConnMouseX = mouse.x
                        root.pendingConnMouseY = mouse.y
                        pendingConnCanvas.requestPaint()
                    }
                    onClicked: {
                        // 点击空白区域取消建连
                        root.pendingConnectionFrom = ""
                    }
                }

                // 节点卡片层
                Repeater {
                    id: nodeRepeater   // v2.7.0 Phase 3.1：分组折叠时通过 itemAt() 访问 delegate
                    model: editViewBridge.currentNodes
                    delegate: Rectangle {
                        id: nodeCard
                        property var nodeData: modelData
                        property bool isSelected: selectedNodeIds.indexOf(nodeData.id) >= 0
                        // P1-B01：屏幕坐标 = world * zoom + offset
                        // v2.5.0 修复：多选拖拽时优先用 livePositions 中的实时坐标
                        // （正在拖拽的节点由 drag.target 接管 x/y，此绑定对其无效）
                        x: {
                            var live = connectionsCanvas.livePositions
                            var lp = live ? live[nodeData.id] : undefined
                            if (lp !== undefined) return lp.x * root.canvasZoom + root.canvasOffsetX
                            return nodeData.x * root.canvasZoom + root.canvasOffsetX
                        }
                        y: {
                            var live = connectionsCanvas.livePositions
                            var lp = live ? live[nodeData.id] : undefined
                            if (lp !== undefined) return lp.y * root.canvasZoom + root.canvasOffsetY
                            return nodeData.y * root.canvasZoom + root.canvasOffsetY
                        }
                        width: Tok.DesignTokens.nodeCardWidth * root.canvasZoom
                        height: (Tok.DesignTokens.nodeCardHeight + 4) * root.canvasZoom  // v5.0：8→4
                        // P1-B03：拖拽起点世界坐标（onPressed 记录，onReleased 传给 commitNodeMove）
                        property real dragOriginX: 0
                        property real dragOriginY: 0
                        // P1-B05：多选拖拽时其他选中节点的起点
                        property var multiDragOrigins: []
                        color: Tok.DesignTokens.bgSurface
                        radius: Tok.DesignTokens.radiusMd
                        border.width: isSelected ? 2 : Tok.DesignTokens.borderWidth
                        border.color: {
                            var meta = editViewBridge.getOperatorMeta(nodeData.type)
                            var cat = (meta && meta.category) || ""
                            return isSelected
                                ? Tok.DesignTokens.categoryColorSelected(cat)
                                : Tok.DesignTokens.categoryColor(cat)
                        }

                        // v4.0: 选中态发光效果
                        Rectangle {
                            anchors.fill: parent
                            anchors.margins: -2
                            radius: Tok.DesignTokens.radiusMd + 2
                            color: "transparent"
                            border.color: isSelected
                                ? Qt.rgba(0.486, 0.302, 1.0, 0.25)
                                : "transparent"
                            border.width: 2
                            visible: isSelected
                        }

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: Tok.DesignTokens.space1 * root.canvasZoom
                            spacing: 1 * root.canvasZoom

                            Label {
                                // v5.0：节点卡片中文名字号 Lg 14→Base 13
                                text: {
                                    var meta = editViewBridge.getOperatorMeta(nodeData.type)
                                    return (meta && meta.cnName) || nodeData.type
                                }
                                color: Tok.DesignTokens.textPrimary
                                font.pixelSize: Tok.DesignTokens.fontSizeBase  // v5.0：Lg→Base
                                font.bold: true
                                font.family: Tok.DesignTokens.fontFamilyCJK
                                font.letterSpacing: 0.2
                                font.hintingPreference: Font.PreferDefaultHinting
                                lineHeight: Tok.DesignTokens.lineHeightTight
                                lineHeightMode: Text.ProportionalHeight
                                elide: Text.ElideRight
                                wrapMode: Text.NoWrap
                                Layout.fillWidth: true
                            }
                            Label {
                                // v5.0：节点卡片类型副标题 Base 13→Sm 12
                                text: nodeData.type
                                color: Tok.DesignTokens.textSecondary
                                font.pixelSize: Tok.DesignTokens.fontSizeSm  // v5.0：Base→Sm
                                font.family: Tok.DesignTokens.fontMono
                                font.letterSpacing: 0.1
                                elide: Text.ElideRight
                                wrapMode: Text.NoWrap
                                Layout.fillWidth: true
                            }
                        }

                        // 输入端口（左）— P0 修复：点击完成建连
                        Rectangle {
                            id: inputPort
                            x: -Tok.DesignTokens.portSize / 2 * root.canvasZoom
                            y: parent.height / 2 - Tok.DesignTokens.portSize / 2 * root.canvasZoom
                            width: Tok.DesignTokens.portSize * root.canvasZoom
                            height: Tok.DesignTokens.portSize * root.canvasZoom
                            radius: Tok.DesignTokens.portSize / 2 * root.canvasZoom
                            // 建连中且非自连接时高亮提示可接入
                            color: (root.pendingConnectionFrom !== ""
                                    && root.pendingConnectionFrom !== nodeData.id)
                                   ? Tok.DesignTokens.accentPrimary
                                   : Tok.DesignTokens.accentWarning
                            border.color: Tok.DesignTokens.bgCanvas
                            border.width: 1
                            z: 10   // 确保端口在节点卡片之上，可接收点击

                            MouseArea {
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.CrossCursor
                                onClicked: function(mouse) {
                                    mouse.accepted = true
                                    if (root.pendingConnectionFrom !== ""
                                        && root.pendingConnectionFrom !== nodeData.id) {
                                        // 完成建连：调用 bridge.connectNodes
                                        editViewBridge.connectNodes(
                                            root.pendingConnectionFrom, "output",
                                            nodeData.id, "input")
                                        root.pendingConnectionFrom = ""
                                        root.showToast("success", "连接已创建")
                                    }
                                }
                            }
                        }
                        // 输出端口（右）— P0 修复：点击启动建连
                        Rectangle {
                            id: outputPort
                            x: parent.width - Tok.DesignTokens.portSize / 2 * root.canvasZoom
                            y: parent.height / 2 - Tok.DesignTokens.portSize / 2 * root.canvasZoom
                            width: Tok.DesignTokens.portSize * root.canvasZoom
                            height: Tok.DesignTokens.portSize * root.canvasZoom
                            radius: Tok.DesignTokens.portSize / 2 * root.canvasZoom
                            // 建连中时高亮当前起始端口
                            color: root.pendingConnectionFrom === nodeData.id
                                   ? Tok.DesignTokens.accentPrimary
                                   : Tok.DesignTokens.accentSuccess
                            border.color: Tok.DesignTokens.bgCanvas
                            border.width: 1
                            z: 10   // 确保端口在节点卡片之上，可接收点击

                            MouseArea {
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.CrossCursor
                                onClicked: function(mouse) {
                                    mouse.accepted = true
                                    if (root.pendingConnectionFrom === "") {
                                        // 启动建连，清空连线选中态避免视觉冲突
                                        root.pendingConnectionFrom = nodeData.id
                                        root.selectedConnectionIndex = -1
                                        root.showToast("info", "已选择输出端口，请点击目标节点的输入端口（Esc 取消）")
                                    } else if (root.pendingConnectionFrom === nodeData.id) {
                                        // 再次点击同一输出端口 → 取消
                                        root.pendingConnectionFrom = ""
                                        root.showToast("info", "已取消连接")
                                    }
                                }
                            }
                        }

                        // 节点交互
                        MouseArea {
                            id: nodeArea
                            anchors.fill: parent
                            drag.target: nodeCard
                            // P1-B01：移除 minimumX/maximumX 限制，节点可拖到画布外（用户可平移画布找回）
                            drag.threshold: 1
                            cursorShape: Qt.OpenHandCursor
                            acceptedButtons: Qt.LeftButton | Qt.RightButton
                            hoverEnabled: true  // v2.8.0：用于控制“?”帮助按钮的显示

                            onPressed: function(mouse) {
                                if (mouse.button !== Qt.LeftButton) return
                                // P1-B03：记录拖拽起点世界坐标（commitNodeMove 需要）
                                nodeCard.dragOriginX = nodeData.x
                                nodeCard.dragOriginY = nodeData.y
                                // P1-B05：多选拖拽时记录其他选中节点的起点
                                if (root.selectedNodeIds.length > 1 &&
                                    root.selectedNodeIds.indexOf(nodeData.id) >= 0) {
                                    var origins = []
                                    var nodes = editViewBridge.currentNodes
                                    for (var i = 0; i < nodes.length; ++i) {
                                        if (nodes[i].id !== nodeData.id &&
                                            root.selectedNodeIds.indexOf(nodes[i].id) >= 0) {
                                            origins.push({id: nodes[i].id, x: nodes[i].x, y: nodes[i].y})
                                        }
                                    }
                                    nodeCard.multiDragOrigins = origins
                                } else {
                                    nodeCard.multiDragOrigins = []
                                }
                            }

                            onPositionChanged: function(mouse) {
                                if (!drag.active) return
                                // P1-B03：拖拽中实时调 moveNodeLive 触发连线重绘
                                var newX = (nodeCard.x - root.canvasOffsetX) / root.canvasZoom
                                var newY = (nodeCard.y - root.canvasOffsetY) / root.canvasZoom
                                editViewBridge.moveNodeLive(nodeData.id, newX, newY)
                                // P1-B05：多选拖拽同步偏移其他选中节点
                                var origins = nodeCard.multiDragOrigins
                                if (origins.length > 0) {
                                    var dx = newX - nodeCard.dragOriginX
                                    var dy = newY - nodeCard.dragOriginY
                                    for (var i = 0; i < origins.length; ++i) {
                                        editViewBridge.moveNodeLive(origins[i].id,
                                            origins[i].x + dx, origins[i].y + dy)
                                    }
                                }
                            }

                            onClicked: function(mouse) {
                                if (mouse.button === Qt.RightButton) {
                                    nodeContextMenu.nodeId = nodeData.id
                                    nodeContextMenu.popup()
                                } else {
                                    root.selectNode(nodeData.id)
                                }
                            }

                            // v4.0: 选中弹性动画
                            ScaleAnimator on scale {
                                from: 0.85
                                to: 1.0
                                duration: 250
                                easing.type: Easing.OutBack
                                running: isSelected && !nodeCard.drag.active
                            }
                            onDoubleClicked: root.openEditorForNode(nodeData.id)
                            onReleased: {
                                if (drag.active) {
                                    // P1-B03：调 commitNodeMove 推 undo 栈（originX/Y 来自 onPressed）
                                    var newX = (nodeCard.x - root.canvasOffsetX) / root.canvasZoom
                                    var newY = (nodeCard.y - root.canvasOffsetY) / root.canvasZoom
                                    editViewBridge.commitNodeMove(nodeData.id,
                                        nodeCard.dragOriginX, nodeCard.dragOriginY,
                                        newX, newY)
                                    // P1-B05：多选拖拽提交其他选中节点的移动
                                    var origins = nodeCard.multiDragOrigins
                                    if (origins.length > 0) {
                                        var dx = newX - nodeCard.dragOriginX
                                        var dy = newY - nodeCard.dragOriginY
                                        for (var i = 0; i < origins.length; ++i) {
                                            editViewBridge.commitNodeMove(origins[i].id,
                                                origins[i].x, origins[i].y,
                                                origins[i].x + dx, origins[i].y + dy)
                                        }
                                    }
                                    nodeCard.multiDragOrigins = []
                                }
                            }
                        }

                        // v2.8.0：算子帮助“?”按钮（节点选中或 hover 时显示，点击打开 OperatorHelpDialog）
                        Rectangle {
                            id: helpBtn
                            anchors.top: parent.top
                            anchors.right: parent.right
                            anchors.topMargin: 4 * root.canvasZoom
                            anchors.rightMargin: 4 * root.canvasZoom
                            width: 16 * root.canvasZoom
                            height: 16 * root.canvasZoom
                            radius: width / 2
                            // 高于端口(z:10)与节点交互 MouseArea，确保可点击且不触发拖拽
                            z: 11
                            visible: isSelected || nodeArea.containsMouse || helpBtnArea.containsMouse
                            color: helpBtnArea.containsMouse
                                   ? Tok.DesignTokens.textTertiary   // hover 态：亮灰
                                   : "#6E6E6E"                        // 默认：灰
                            border.color: Tok.DesignTokens.bgCanvas
                            border.width: 1

                            Text {
                                anchors.centerIn: parent
                                text: "?"
                                color: "#FFFFFF"
                                font.pixelSize: Math.max(8, 10 * root.canvasZoom)
                                font.bold: true
                                font.family: Tok.DesignTokens.fontFamily
                            }

                            // 独立 MouseArea：消费点击/按下，阻止事件冒泡到节点拖拽 MouseArea
                            MouseArea {
                                id: helpBtnArea
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onPressed: function(mouse) { mouse.accepted = true }
                                onPositionChanged: function(mouse) { mouse.accepted = true }
                                onClicked: function(mouse) {
                                    mouse.accepted = true
                                    root.openOperatorHelp(nodeData.type)
                                }
                            }
                        }
                        Accessible.role: Accessible.Button
                        Accessible.name: {
                            var meta = editViewBridge.getOperatorMeta(nodeData.type)
                            return (meta && meta.cnName) || nodeData.type
                        }
                        Accessible.description: {
                            // P0 修复：原代码使用 ({...}) IIFE 语法，QML 不支持
                            var meta = editViewBridge.getOperatorMeta(nodeData.type)
                            var name = (meta && meta.cnName) || nodeData.type
                            return "算子节点: " + name + "，双击编辑参数，右键打开菜单"
                        }
                    }
                }

                // ============================================================
                // v2.7.0 Phase 3.1：分组容器可视化叠加层
                // z: 0.5 — 位于节点（z:0）和连线 Canvas（z:1）之间
                // 不改变现有节点 Repeater 逻辑，仅通过叠加层绘制分组包围盒
                // 数据源：editViewBridge.subChainGroups / branchGroups / parallelGroups
                // 折叠状态：root.groupCollapsed 字典，点击标题栏切换
                // ============================================================

                // ---- 子链分组容器（蓝色虚线）----
                Repeater {
                    id: subChainGroupLayer
                    model: editViewBridge.subChainGroups
                    delegate: Item {
                        id: subChainGroupContainer
                        property var groupData: modelData
                        property string groupId: groupData.loopToolId
                        property bool collapsed: root.groupCollapsed[groupId] === true
                        // 计算子节点屏幕坐标包围盒（已应用 canvasZoom/Offset 变换）
                        property var bbox: root.computeNodeBBox(groupData.childToolIds)
                        x: bbox.minX - Tok.DesignTokens.groupPadding
                        y: bbox.minY - Tok.DesignTokens.groupPadding - Tok.DesignTokens.groupHeaderHeight
                        width: (bbox.maxX - bbox.minX) + Tok.DesignTokens.groupPadding * 2
                        height: (bbox.maxY - bbox.minY) + Tok.DesignTokens.groupPadding * 2 + Tok.DesignTokens.groupHeaderHeight
                        z: 0.5
                        visible: bbox.valid

                        // 虚线边框（Canvas 绘制，支持 setLineDash）
                        Canvas {
                            anchors.fill: parent
                            onPaint: {
                                var ctx = getContext("2d")
                                ctx.reset()
                                ctx.strokeStyle = Tok.DesignTokens.subChainGroupColor
                                ctx.lineWidth = 1.5
                                ctx.setLineDash([6, 4])
                                ctx.strokeRect(0.5, 0.5, width - 1, height - 1)
                            }
                            // 画布变换变化时重绘虚线边框
                            Connections {
                                target: root
                                function onCanvasZoomChanged() { requestPaint() }
                                function onCanvasOffsetXChanged() { requestPaint() }
                                function onCanvasOffsetYChanged() { requestPaint() }
                            }
                            Connections {
                                target: editViewBridge
                                function onCurrentNodesChanged() { requestPaint() }
                            }
                        }

                        // 标题栏（蓝色背景 + 白字）
                        Rectangle {
                            id: subChainHeader
                            x: 0
                            y: 0
                            width: parent.width
                            height: Tok.DesignTokens.groupHeaderHeight
                            color: Tok.DesignTokens.subChainGroupColor
                            radius: 4
                            // 底部直角，与容器边框自然衔接
                            Rectangle {
                                x: 0
                                y: parent.height - 4
                                width: parent.width
                                height: 4
                                color: parent.color
                            }
                            Label {
                                anchors.fill: parent
                                anchors.leftMargin: 8
                                anchors.rightMargin: 8
                                // 折叠三角箭头 ▶/▼ + 标题
                                text: (subChainGroupContainer.collapsed ? "\u25B6" : "\u25BC")
                                      + " 子链: " + groupData.loopToolName
                                      + " (" + groupData.childToolCount + "个算子)"
                                color: "#FFFFFF"
                                font.pixelSize: 11
                                font.bold: true
                                verticalAlignment: Text.AlignVCenter
                            }
                            // 点击标题栏切换折叠状态
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.toggleGroupCollapse(
                                    subChainGroupContainer.groupId,
                                    groupData.childToolIds)
                            }
                        }
                    }
                }

                // ---- 分支分组容器（true 绿 + false 红 两个子区域）----
                Repeater {
                    id: branchGroupLayer
                    model: editViewBridge.branchGroups
                    delegate: Item {
                        id: branchGroupContainer
                        property var groupData: modelData
                        property string groupId: groupData.branchToolId
                        property bool collapsed: root.groupCollapsed[groupId] === true
                        property var trueBBox: root.computeNodeBBox(groupData.trueBranchToolIds)
                        property var falseBBox: root.computeNodeBBox(groupData.falseBranchToolIds)
                        // 容器自身不绘制（width/height=0），仅作为逻辑分组承载两个子区域
                        x: 0
                        y: 0
                        width: 0
                        height: 0
                        z: 0.5
                        visible: trueBBox.valid || falseBBox.valid

                        // 真分支区域（绿色虚线，标题栏显示分支信息）
                        Item {
                            id: trueBranchArea
                            x: branchGroupContainer.trueBBox.minX - Tok.DesignTokens.groupPadding
                            y: branchGroupContainer.trueBBox.minY - Tok.DesignTokens.groupPadding - Tok.DesignTokens.groupHeaderHeight
                            width: (branchGroupContainer.trueBBox.maxX - branchGroupContainer.trueBBox.minX) + Tok.DesignTokens.groupPadding * 2
                            height: (branchGroupContainer.trueBBox.maxY - branchGroupContainer.trueBBox.minY) + Tok.DesignTokens.groupPadding * 2 + Tok.DesignTokens.groupHeaderHeight
                            visible: branchGroupContainer.trueBBox.valid && !branchGroupContainer.collapsed

                            Canvas {
                                anchors.fill: parent
                                onPaint: {
                                    var ctx = getContext("2d")
                                    ctx.reset()
                                    ctx.strokeStyle = Tok.DesignTokens.trueBranchColor
                                    ctx.lineWidth = 1.5
                                    ctx.setLineDash([6, 4])
                                    ctx.strokeRect(0.5, 0.5, width - 1, height - 1)
                                }
                                Connections {
                                    target: root
                                    function onCanvasZoomChanged() { requestPaint() }
                                    function onCanvasOffsetXChanged() { requestPaint() }
                                    function onCanvasOffsetYChanged() { requestPaint() }
                                }
                                Connections {
                                    target: editViewBridge
                                    function onCurrentNodesChanged() { requestPaint() }
                                }
                            }

                            Rectangle {
                                x: 0
                                y: 0
                                width: parent.width
                                height: Tok.DesignTokens.groupHeaderHeight
                                color: Tok.DesignTokens.trueBranchColor
                                radius: 4
                                Rectangle {
                                    x: 0
                                    y: parent.height - 4
                                    width: parent.width
                                    height: 4
                                    color: parent.color
                                }
                                Label {
                                    anchors.fill: parent
                                    anchors.leftMargin: 8
                                    anchors.rightMargin: 8
                                    // 显示分支名 + 条件运算符 + 真分支节点数
                                    text: "分支: " + groupData.branchToolName
                                          + " [" + groupData.conditionOp + "] \u2192 真 ("
                                          + groupData.trueBranchToolIds.length + ")"
                                    color: "#FFFFFF"
                                    font.pixelSize: 11
                                    font.bold: true
                                    verticalAlignment: Text.AlignVCenter
                                }
                                // 点击真分支标题栏切换整个分支组的折叠状态
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: root.toggleGroupCollapse(
                                        branchGroupContainer.groupId,
                                        groupData.trueBranchToolIds.concat(groupData.falseBranchToolIds))
                                }
                            }
                        }

                        // 假分支区域（红色虚线）
                        Item {
                            id: falseBranchArea
                            x: branchGroupContainer.falseBBox.minX - Tok.DesignTokens.groupPadding
                            y: branchGroupContainer.falseBBox.minY - Tok.DesignTokens.groupPadding - Tok.DesignTokens.groupHeaderHeight
                            width: (branchGroupContainer.falseBBox.maxX - branchGroupContainer.falseBBox.minX) + Tok.DesignTokens.groupPadding * 2
                            height: (branchGroupContainer.falseBBox.maxY - branchGroupContainer.falseBBox.minY) + Tok.DesignTokens.groupPadding * 2 + Tok.DesignTokens.groupHeaderHeight
                            visible: branchGroupContainer.falseBBox.valid && !branchGroupContainer.collapsed

                            Canvas {
                                anchors.fill: parent
                                onPaint: {
                                    var ctx = getContext("2d")
                                    ctx.reset()
                                    ctx.strokeStyle = Tok.DesignTokens.falseBranchColor
                                    ctx.lineWidth = 1.5
                                    ctx.setLineDash([6, 4])
                                    ctx.strokeRect(0.5, 0.5, width - 1, height - 1)
                                }
                                Connections {
                                    target: root
                                    function onCanvasZoomChanged() { requestPaint() }
                                    function onCanvasOffsetXChanged() { requestPaint() }
                                    function onCanvasOffsetYChanged() { requestPaint() }
                                }
                                Connections {
                                    target: editViewBridge
                                    function onCurrentNodesChanged() { requestPaint() }
                                }
                            }

                            Rectangle {
                                x: 0
                                y: 0
                                width: parent.width
                                height: Tok.DesignTokens.groupHeaderHeight
                                color: Tok.DesignTokens.falseBranchColor
                                radius: 4
                                Rectangle {
                                    x: 0
                                    y: parent.height - 4
                                    width: parent.width
                                    height: 4
                                    color: parent.color
                                }
                                Label {
                                    anchors.fill: parent
                                    anchors.leftMargin: 8
                                    anchors.rightMargin: 8
                                    text: "分支: " + groupData.branchToolName
                                          + " \u2192 假 ("
                                          + groupData.falseBranchToolIds.length + ")"
                                    color: "#FFFFFF"
                                    font.pixelSize: 11
                                    font.bold: true
                                    verticalAlignment: Text.AlignVCenter
                                }
                                // 假分支标题栏不单独切换折叠（折叠由真分支标题栏统一控制）
                            }
                        }
                    }
                }

                // ---- 并行分组容器（黄色虚线）----
                Repeater {
                    id: parallelGroupLayer
                    model: editViewBridge.parallelGroups
                    delegate: Item {
                        id: parallelGroupContainer
                        property var groupData: modelData
                        property string groupId: groupData.branchId
                        property bool collapsed: root.groupCollapsed[groupId] === true
                        property var bbox: root.computeNodeBBox(groupData.branchToolIds)
                        x: bbox.minX - Tok.DesignTokens.groupPadding
                        y: bbox.minY - Tok.DesignTokens.groupPadding - Tok.DesignTokens.groupHeaderHeight
                        width: (bbox.maxX - bbox.minX) + Tok.DesignTokens.groupPadding * 2
                        height: (bbox.maxY - bbox.minY) + Tok.DesignTokens.groupPadding * 2 + Tok.DesignTokens.groupHeaderHeight
                        z: 0.5
                        visible: bbox.valid

                        Canvas {
                            anchors.fill: parent
                            onPaint: {
                                var ctx = getContext("2d")
                                ctx.reset()
                                ctx.strokeStyle = Tok.DesignTokens.parallelGroupColor
                                ctx.lineWidth = 1.5
                                ctx.setLineDash([6, 4])
                                ctx.strokeRect(0.5, 0.5, width - 1, height - 1)
                            }
                            Connections {
                                target: root
                                function onCanvasZoomChanged() { requestPaint() }
                                function onCanvasOffsetXChanged() { requestPaint() }
                                function onCanvasOffsetYChanged() { requestPaint() }
                            }
                            Connections {
                                target: editViewBridge
                                function onCurrentNodesChanged() { requestPaint() }
                            }
                        }

                        Rectangle {
                            x: 0
                            y: 0
                            width: parent.width
                            height: Tok.DesignTokens.groupHeaderHeight
                            color: Tok.DesignTokens.parallelGroupColor
                            radius: 4
                            Rectangle {
                                x: 0
                                y: parent.height - 4
                                width: parent.width
                                height: 4
                                color: parent.color
                            }
                            Label {
                                anchors.fill: parent
                                anchors.leftMargin: 8
                                anchors.rightMargin: 8
                                text: (parallelGroupContainer.collapsed ? "\u25B6" : "\u25BC")
                                      + " 并行: " + groupData.branchId
                                      + " (" + groupData.branchToolIds.length + "个算子)"
                                // 黄色背景配深色文字（对比度更高）
                                color: "#1A1A1A"
                                font.pixelSize: 11
                                font.bold: true
                                verticalAlignment: Text.AlignVCenter
                            }
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.toggleGroupCollapse(
                                    parallelGroupContainer.groupId,
                                    groupData.branchToolIds)
                            }
                        }
                    }
                }

                // 右键菜单（v3.0.0）
                Menu {
                    id: nodeContextMenu
                    property string nodeId: ""

                    MenuItem {
                        text: "\u270E 编辑参数"
                        onTriggered: root.openEditorForNode(nodeContextMenu.nodeId)
                    }
                    // v2.5.0 功能 5c：运行到此算子（执行上游链 + 当前算子）
                    // v2.5.0 修复：智能识别输入源 — 若上游链含 ReadImage 节点则直接执行，
                    // 否则才弹出 FileDialog 让用户选择图像
                    MenuItem {
                        text: "\u25B6 运行到此算子"
                        onTriggered: {
                            var nodeId = nodeContextMenu.nodeId
                            // v5.3.8：先判断是否为数据源型算子（相机/读图等），无需输入图像
                            var runInput = editViewBridge.resolveRunInput(nodeId)
                            if (!runInput.required) {
                                root.runToNode("", nodeId)
                                return
                            }
                            // 再尝试自动解析上游 ReadImage 节点的 filePath
                            var autoPath = runInput.path
                            if (autoPath && autoPath !== "") {
                                // 上游有 ReadImage 节点且已配置图像路径 → 直接执行
                                root.runToNode(autoPath, nodeId)
                            } else {
                                // 无 ReadImage 上游或 filePath 为空 → 弹 FileDialog
                                runToNodeFileDialog.targetNodeId = nodeId
                                runToNodeFileDialog.open()
                            }
                        }
                    }
                    MenuItem {
                        text: "\u2605 " + (editViewBridge.isFavorite(
                            editViewBridge.currentNodes.length > 0 ? editViewBridge.getOperatorMeta(
                                (function() {
                                    var nodes = editViewBridge.currentNodes
                                    for (var i = 0; i < nodes.length; ++i)
                                        if (nodes[i].id === nodeContextMenu.nodeId) return nodes[i].type
                                    return ""
                                })()).type : "") ? "取消收藏" : "收藏")
                        onTriggered: {
                            var nodes = editViewBridge.currentNodes
                            for (var i = 0; i < nodes.length; ++i) {
                                if (nodes[i].id === nodeContextMenu.nodeId) {
                                    editViewBridge.toggleFavorite(nodes[i].type)
                                    break
                                }
                            }
                        }
                    }
                    MenuSeparator {}
                    MenuItem {
                        text: "\u29C9 复制节点"
                        onTriggered: {
                            root.selectedNodeIds = [nodeContextMenu.nodeId]
                            root.currentSelectedNodeId = nodeContextMenu.nodeId
                            root.copySelectedNodes()
                        }
                    }
                    MenuItem {
                        text: "\u2715 删除节点"
                        onTriggered: {
                            editViewBridge.deleteNode(nodeContextMenu.nodeId)
                            selectedNodeIds = selectedNodeIds.filter(
                                function(id) { return id !== nodeContextMenu.nodeId })
                            // P1-B11 修复：若删除的是当前选中节点，清空 currentSelectedNodeId
                            if (nodeContextMenu.nodeId === root.currentSelectedNodeId) {
                                root.currentSelectedNodeId = ""
                            }
                        }
                    }
                }

                // P1-B12 修复：画布空白区域右键菜单
                Menu {
                    id: canvasContextMenu

                    MenuItem {
                        text: "\u25A6 适应画布"
                        onTriggered: root.fitCanvasToNodes()
                    }
                    MenuItem {
                        text: "\u29C9 重置缩放"
                        onTriggered: root.resetCanvasView()
                    }
                    MenuSeparator {}
                    MenuItem {
                        text: "\u2714 全选"
                        onTriggered: {
                            var nodes = editViewBridge.currentNodes
                            var ids = []
                            for (var i = 0; i < nodes.length; ++i) ids.push(nodes[i].id)
                            root.selectedNodeIds = ids
                            if (ids.length > 0) root.currentSelectedNodeId = ids[0]
                        }
                    }
                    MenuItem {
                        text: "\u2715 取消选择"
                        enabled: root.selectedNodeIds.length > 0 || root.selectedConnectionIndex >= 0
                        onTriggered: {
                            root.selectedNodeIds = []
                            root.currentSelectedNodeId = ""
                            root.selectedConnectionIndex = -1
                        }
                    }
                    MenuItem {
                        text: "\u29C9 粘贴节点"
                        enabled: root.clipboardNodes.length > 0
                        onTriggered: root.pasteNodes()
                    }
                }

                // 空画布提示
                ColumnLayout {
                    anchors.centerIn: parent
                    spacing: Tok.DesignTokens.space2
                    visible: editViewBridge.currentNodes.length === 0

                    Label {
                        text: "流程图编辑区"
                        color: Tok.DesignTokens.accentPrimary
                        font.bold: true
                        // v3.0.0：Xl 16→2xl 20，标题更醒目
                        font.pixelSize: Tok.DesignTokens.fontSize2xl
                        font.family: Tok.DesignTokens.fontFamilyCJK
                        font.letterSpacing: 0.5
                        Layout.alignment: Qt.AlignHCenter
                    }
                    Label {
                        text: "双击左侧算子库中的算子添加到画布"
                        color: Tok.DesignTokens.textSecondary
                        // v3.0.0：Base 13→Lg 14，提升可读性
                        font.pixelSize: Tok.DesignTokens.fontSizeLg
                        font.family: Tok.DesignTokens.fontFamilyCJK
                        Layout.alignment: Qt.AlignHCenter
                    }
                    Label {
                        text: "拖拽节点调整位置 · Ctrl+K 搜索 · 滚轮缩放 · Delete 删除"
                        color: Tok.DesignTokens.textTertiary
                        // v3.0.0：Xs 11→Base 13，提示文字也清晰
                        font.pixelSize: Tok.DesignTokens.fontSizeBase
                        font.family: Tok.DesignTokens.fontFamilyCJK
                        Layout.alignment: Qt.AlignHCenter
                    }
                }

                // 小地图（v3.0.0 真实渲染）
                Rectangle {
                    id: minimap
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.rightMargin: Tok.DesignTokens.space2
                    anchors.bottomMargin: Tok.DesignTokens.space2
                    width: 140
                    height: 90
                    color: Qt.rgba(0.1, 0.1, 0.1, 0.75)
                    border.color: Tok.DesignTokens.borderDefault
                    border.width: Tok.DesignTokens.borderWidth
                    radius: Tok.DesignTokens.radiusMd
                    visible: editViewBridge.currentNodes.length > 0

                    Canvas {
                        id: minimapCanvas
                        anchors.fill: parent
                        anchors.margins: 2
                        property var nodes: editViewBridge.currentNodes
                        property var connections: editViewBridge.connections

                        onPaint: {
                            var ctx = getContext("2d")
                            ctx.clearRect(0, 0, width, height)

                            var nodes = minimapCanvas.nodes || []
                            if (nodes.length === 0) return

                            // 计算边界
                            var minX = Infinity, minY = Infinity, maxX = -Infinity, maxY = -Infinity
                            for (var i = 0; i < nodes.length; ++i) {
                                minX = Math.min(minX, nodes[i].x)
                                minY = Math.min(minY, nodes[i].y)
                                maxX = Math.max(maxX, nodes[i].x + Tok.DesignTokens.nodeCardWidth)
                                maxY = Math.max(maxY, nodes[i].y + Tok.DesignTokens.nodeCardHeight)
                            }
                            var padW = Tok.DesignTokens.nodeCardWidth
                            var padH = Tok.DesignTokens.nodeCardHeight
                            var mapW = maxX - minX + padW * 2
                            var mapH = maxY - minY + padH * 2
                            var sx = width / mapW
                            var sy = height / mapH
                            var s = Math.min(sx, sy)

                            // 绘制节点
                            for (var j = 0; j < nodes.length; ++j) {
                                var nx = (nodes[j].x - minX + padW) * s
                                var ny = (nodes[j].y - minY + padH) * s
                                var nw = Tok.DesignTokens.nodeCardWidth * s
                                var nh = Tok.DesignTokens.nodeCardHeight * s
                                ctx.fillStyle = Tok.DesignTokens.accentPrimary
                                ctx.fillRect(nx, ny, nw, nh)
                            }

                            // v4.0: 绘制连线
                            var conns = minimapCanvas.connections || []
                            ctx.strokeStyle = Qt.rgba(0.486, 0.302, 1.0, 0.4)
                            ctx.lineWidth = 1
                            for (var ci = 0; ci < conns.length; ci++) {
                                var fromNd = null, toNd = null
                                for (var ni = 0; ni < nodes.length; ni++) {
                                    // P1-A6 修复：字段名 fromNodeId→fromId, toNodeId→toId（与数据源对齐）
                                    if (nodes[ni].id === conns[ci].fromId) fromNd = nodes[ni]
                                    if (nodes[ni].id === conns[ci].toId) toNd = nodes[ni]
                                    if (fromNd && toNd) break
                                }
                                if (fromNd && toNd) {
                                    var fx = (fromNd.x - minX + padW) * s
                                    var fy = (fromNd.y - minY + padH) * s
                                    var tx = (toNd.x - minX + padW) * s
                                    var ty = (toNd.y - minY + padH) * s
                                    ctx.beginPath()
                                    ctx.moveTo(fx, fy)
                                    ctx.lineTo(tx, ty)
                                    ctx.stroke()
                                }
                            }

                            // v4.0: 绘制视口矩形 — P1-B01 修复：公式补 /canvasZoom
                            // 视口左上角世界坐标 = -canvasOffset / canvasZoom
                            var vpWorldX = -root.canvasOffsetX / root.canvasZoom
                            var vpWorldY = -root.canvasOffsetY / root.canvasZoom
                            var vpX = (vpWorldX - minX + padW) * s
                            var vpY = (vpWorldY - minY + padH) * s
                            var vpW = (canvasFrame.width / root.canvasZoom) * s
                            var vpH = (canvasFrame.height / root.canvasZoom) * s
                            ctx.strokeStyle = Qt.rgba(1, 1, 1, 0.6)
                            ctx.lineWidth = 1
                            ctx.strokeRect(vpX, vpY, vpW, vpH)
                        }
                    }
                    Connections {
                        target: editViewBridge
                        function onCurrentNodesChanged() {
                            minimapCanvas.nodes = editViewBridge.currentNodes
                            minimapCanvas.requestPaint()
                        }
                    }

                    // v4.0: 点击小地图导航
                    MouseArea {
                        anchors.fill: parent
                        onClicked: function(mouse) {
                            // 需要从画布坐标反算
                            var nodes = minimapCanvas.nodes || []
                            if (nodes.length === 0) return
                            var minX = Infinity, minY = Infinity, maxX = -Infinity, maxY = -Infinity
                            for (var i = 0; i < nodes.length; ++i) {
                                minX = Math.min(minX, nodes[i].x)
                                minY = Math.min(minY, nodes[i].y)
                                maxX = Math.max(maxX, nodes[i].x + Tok.DesignTokens.nodeCardWidth)
                                maxY = Math.max(maxY, nodes[i].y + Tok.DesignTokens.nodeCardHeight)
                            }
                            var padW = Tok.DesignTokens.nodeCardWidth
                            var padH = Tok.DesignTokens.nodeCardHeight
                            var mapW = maxX - minX + padW * 2
                            var mapH = maxY - minY + padH * 2
                            var sx = minimapCanvas.width / mapW
                            var sy = minimapCanvas.height / mapH
                            var sc = Math.min(sx, sy)
                            var targetX = (mouse.x / sc) + minX - padW
                            var targetY = (mouse.y / sc) + minY - padH
                            // P1-B01 修复：offset = 屏幕中心 - 目标世界坐标 * zoom
                            // 视口中心屏幕坐标 = canvasFrame.width/2，要使视口中心世界坐标 = targetX
                            // (canvasFrame.width/2 - offset) / zoom = targetX  =>  offset = canvasFrame.width/2 - targetX * zoom
                            root.canvasOffsetX = canvasFrame.width / 2 - targetX * root.canvasZoom
                            root.canvasOffsetY = canvasFrame.height / 2 - targetY * root.canvasZoom
                            minimapCanvas.requestPaint()
                        }
                    }
                }

                // v5.1：展开左侧算子库按钮
                Rectangle {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.leftMargin: Tok.DesignTokens.space2
                    anchors.topMargin: Tok.DesignTokens.space2
                    width: 28; height: 28; radius: Tok.DesignTokens.radiusMd
                    color: expandLeftArea.containsMouse
                        ? Tok.DesignTokens.bgHover : Tok.DesignTokens.bgSurface
                    border.color: Tok.DesignTokens.borderDefault
                    border.width: 1
                    visible: !root.leftPanelVisible
                    ToolTip.text: "展开算子库"
                    ToolTip.visible: expandLeftArea.containsMouse
                    ToolTip.delay: 500
                    RowLayout {
                        anchors.centerIn: parent
                        spacing: 4
                        Label {
                            text: "\u25B6"
                            color: Tok.DesignTokens.textSecondary
                            font.pixelSize: 12
                        }
                        Label {
                            text: "算子"
                            color: Tok.DesignTokens.textTertiary
                            font.pixelSize: Tok.DesignTokens.fontSizeXs
                        }
                    }
                    MouseArea {
                        id: expandLeftArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.leftPanelVisible = true
                    }
                }

                // v5.1：展开右侧信息列按钮
                Rectangle {
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.rightMargin: Tok.DesignTokens.space2
                    anchors.topMargin: Tok.DesignTokens.space2
                    width: 28; height: 28; radius: Tok.DesignTokens.radiusMd
                    color: expandRightInfoArea.containsMouse
                        ? Tok.DesignTokens.bgHover : Tok.DesignTokens.bgSurface
                    border.color: Tok.DesignTokens.borderDefault
                    border.width: 1
                    visible: !root.rightInfoVisible
                    ToolTip.text: "展开图片预览"
                    ToolTip.visible: expandRightInfoArea.containsMouse
                    ToolTip.delay: 500
                    RowLayout {
                        anchors.centerIn: parent
                        spacing: 4
                        Label {
                            text: "\u25C0"
                            color: Tok.DesignTokens.textSecondary
                            font.pixelSize: 12
                        }
                        Label {
                            text: "预览"
                            color: Tok.DesignTokens.textTertiary
                            font.pixelSize: Tok.DesignTokens.fontSizeXs
                        }
                    }
                    MouseArea {
                        id: expandRightInfoArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.rightInfoVisible = true
                    }
                }

                // ============ spec: editor-output-connection-optimization Task10：智能算子推荐展示区 ============
                // 常驻画布右上角（锚定 canvasFrame），非 Popup；topMargin 避开右上角展开按钮行
                RecommendationPanel {
                    id: recommendationPanel
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.rightMargin: Tok.DesignTokens.space2
                    anchors.topMargin: 44
                    bridge: editViewBridge
                    selectedNodeId: root.currentSelectedNodeId
                }

                // 响应式：窄屏时显示展开右侧面板按钮（v3.2.0 增加 pin 状态指示）
                Rectangle {
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.rightMargin: Tok.DesignTokens.space2
                    anchors.topMargin: Tok.DesignTokens.space2
                    width: 28; height: 28; radius: Tok.DesignTokens.radiusMd
                    color: Tok.DesignTokens.bgSurface
                    border.color: root.rightPanelPinned
                        ? Tok.DesignTokens.accentPrimary
                        : Tok.DesignTokens.borderDefault
                    border.width: root.rightPanelPinned ? 2 : Tok.DesignTokens.borderWidth
                    visible: !root.rightPanelVisible && root.currentSelectedNodeId !== ""
                    ToolTip.text: root.rightPanelPinned
                        ? "算子参数编辑器已固定（点击展开）"
                        : "展开算子参数编辑器"
                    ToolTip.visible: expandBtnArea.containsMouse
                    ToolTip.delay: 500
                    Label {
                        anchors.centerIn: parent
                        text: root.rightPanelPinned ? "🔒" : "\u25C0"
                        color: Tok.DesignTokens.accentPrimary
                        font.pixelSize: Tok.DesignTokens.fontSizeLg
                    }
                    MouseArea {
                        id: expandBtnArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.rightPanelVisible = true
                    }
                }
            }

            // 底部变量管理区（左：图像变量，右：控制变量）
            Rectangle {
                id: bottomWorkspace
                SplitView.preferredHeight: root.bottomPanelVisible ? root.bottomPanelHeight : 0
                SplitView.minimumHeight: 0
                color: Tok.DesignTokens.bgPanel
                visible: root.bottomPanelVisible
                clip: true

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0

                    // 标题栏
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 28
                        color: Tok.DesignTokens.bgHeader

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: Tok.DesignTokens.space2
                            anchors.rightMargin: Tok.DesignTokens.space2
                            spacing: 8

                            Label {
                                text: "变量管理"
                                color: Tok.DesignTokens.accentPrimary
                                font.bold: true
                                font.pixelSize: Tok.DesignTokens.fontSizeBase
                                font.family: Tok.DesignTokens.fontFamilyCJK
                            }
                            Item { Layout.fillWidth: true }
                            // 折叠按钮
                            Rectangle {
                                width: 24; height: 24; radius: 4
                                color: collapseBottomArea.containsMouse
                                    ? Tok.DesignTokens.bgHover : "transparent"
                                Label {
                                    anchors.centerIn: parent
                                    text: root.bottomPanelVisible ? "\u25BC" : "\u25B2"
                                    color: Tok.DesignTokens.textSecondary
                                    font.pixelSize: 10
                                }
                                MouseArea {
                                    id: collapseBottomArea
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: root.bottomPanelVisible = !root.bottomPanelVisible
                                }
                            }
                        }
                    }

                    // 左右拆分：图像变量 / 控制变量
                    SplitView {
                        id: bottomVarSplit
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        orientation: Qt.Horizontal

                        Rectangle {
                            SplitView.preferredWidth: bottomVarSplit.width / 2
                            SplitView.minimumWidth: 160
                            color: Tok.DesignTokens.bgPanel

                            VariableManagerPanel {
                                id: bottomImageVarPanel
                                anchors.fill: parent
                                bridge: editViewBridge
                                mode: 2   // 仅图像变量

                                onImageNodeSelected: function(nodeId) {
                                    // 切换预览为选中节点的输出图像（单张）
                                    var iv = bridge.imageVariableManager
                                    if (!iv) return
                                    var path = iv.imagePath(nodeId)
                                    if (path && path.length > 0) {
                                        root.previewSourceImage = "file:///" + path
                                        root.previewProcessedImage = ""
                                    } else {
                                        // 未找到输出图：清空预览，避免残留叠加
                                        root.previewSourceImage = ""
                                        root.previewProcessedImage = ""
                                    }
                                }
                            }
                        }

                        Rectangle {
                            SplitView.preferredWidth: bottomVarSplit.width / 2
                            SplitView.minimumWidth: 160
                            color: Tok.DesignTokens.bgPanel

                            VariableManagerPanel {
                                id: bottomControlVarPanel
                                anchors.fill: parent
                                bridge: editViewBridge
                                mode: 3   // v5.3：改为仅算子参数，自动列出所有算子节点参数
                            }
                        }
                    }
                }
            }
        }

        // ============ 右侧：图片预览窗口（Halcon 风格） ============
            Rectangle {
                id: rightImagePreviewPanel
                SplitView.fillHeight: true
                SplitView.preferredWidth: Math.max(260, root.width * 0.28)
                SplitView.minimumWidth: 240
                SplitView.maximumWidth: root.width * 0.55
                color: Tok.DesignTokens.bgPanel
                visible: root.rightInfoVisible

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 0
                    spacing: 0

                    // 标题栏
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 32
                        color: Tok.DesignTokens.bgHeader

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: Tok.DesignTokens.space2
                            anchors.rightMargin: Tok.DesignTokens.space2
                            spacing: 8

                            Label {
                                text: "图片预览"
                                color: Tok.DesignTokens.accentPrimary
                                font.bold: true
                                font.pixelSize: Tok.DesignTokens.fontSizeBase
                                font.family: Tok.DesignTokens.fontFamilyCJK
                                Layout.fillWidth: true
                            }
                            // 折叠右侧预览列按钮
                            Rectangle {
                                width: 24; height: 24; radius: 4
                                color: collapseRightArea.containsMouse
                                    ? Tok.DesignTokens.bgHover : "transparent"
                                Label {
                                    anchors.centerIn: parent
                                    text: "\u25C0"
                                    color: Tok.DesignTokens.textSecondary
                                    font.pixelSize: 12
                                }
                                MouseArea {
                                    id: collapseRightArea
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: root.rightInfoVisible = false
                                }
                            }
                        }
                    }

                    // Halcon 风格图像预览
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.margins: Tok.DesignTokens.space1
                        color: Tok.DesignTokens.bgSurface
                        border.color: Tok.DesignTokens.borderDefault
                        border.width: 1
                        radius: Tok.DesignTokens.radiusMd
                        clip: true

                        ImageViewer {
                            id: rightImageViewer
                            anchors.fill: parent
                            imageSource: root.previewProcessedImage.toString() !== ""
                                         ? root.previewProcessedImage
                                         : root.previewSourceImage
                        }
                    }
                }
            }
        }

        // 原底部日志面板已移除，日志入口折叠到右下角
    }

    // 右下角日志入口
    Rectangle {
        id: logEntry
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.rightMargin: Tok.DesignTokens.space2
        anchors.bottomMargin: Tok.DesignTokens.space2
        width: 28; height: 28; radius: Tok.DesignTokens.radiusMd
        color: logEntryArea.containsMouse ? Tok.DesignTokens.bgHover : Tok.DesignTokens.bgSurface
        border.color: Tok.DesignTokens.borderDefault
        border.width: 1
        z: 50
        Label {
            anchors.centerIn: parent
            text: "\u{1F4CB}"
            font.pixelSize: 12
        }
        MouseArea {
            id: logEntryArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: root.logOverlayVisible = !root.logOverlayVisible
        }
    }

    // 右下角日志浮窗
    Rectangle {
        id: logOverlay
        anchors.right: parent.right
        anchors.bottom: logEntry.top
        anchors.rightMargin: Tok.DesignTokens.space2
        anchors.bottomMargin: Tok.DesignTokens.space1
        width: 420
        height: 260
        visible: root.logOverlayVisible
        color: Tok.DesignTokens.bgPanel
        border.color: Tok.DesignTokens.borderDefault
        border.width: 1
        radius: Tok.DesignTokens.radiusMd
        z: 50
        clip: true

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 28
                color: Tok.DesignTokens.bgHeader

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Tok.DesignTokens.space2
                    anchors.rightMargin: Tok.DesignTokens.space2
                    spacing: 8

                    Label {
                        text: "\u{1F4CB} 日志 / 调试"
                        color: Tok.DesignTokens.accentPrimary
                        font.bold: true
                        font.pixelSize: Tok.DesignTokens.fontSizeBase
                        font.family: Tok.DesignTokens.fontFamilyCJK
                    }
                    Item { Layout.fillWidth: true }
                    Rectangle {
                        width: 24; height: 24; radius: 4
                        color: closeLogArea.containsMouse ? Tok.DesignTokens.bgHover : "transparent"
                        Label {
                            anchors.centerIn: parent
                            text: "\u2715"
                            color: Tok.DesignTokens.textSecondary
                            font.pixelSize: 10
                        }
                        MouseArea {
                            id: closeLogArea
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.logOverlayVisible = false
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.margins: Tok.DesignTokens.space1
                color: Tok.DesignTokens.bgSurface
                border.color: Tok.DesignTokens.borderDefault
                border.width: 1
                radius: Tok.DesignTokens.radiusMd

                Flickable {
                    anchors.fill: parent
                    clip: true
                    contentWidth: width
                    ScrollBar.vertical: ScrollBar {
                        policy: ScrollBar.AsNeeded
                    }

                    Label {
                        anchors.fill: parent
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        text: "运行方案后将在此显示日志和调试信息"
                        color: Tok.DesignTokens.textPlaceholder
                        font.pixelSize: Tok.DesignTokens.fontSizeBase
                        font.family: Tok.DesignTokens.fontFamilyCJK
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }
    }

    // ============ v3.1.0: 右侧浮动面板（FloatingPanel + PropertyPreviewPanel）============
    FloatingPanel {
        id: floatingRightPanel
        // 自由浮动模式：不设置任何 anchor，由 panelTitle 区域驱动
        visible: root.rightPanelFloating && root.rightPanelVisible
        panelTitle: "算子详情"
        z: 100

        PropertyPreviewPanel {
            id: floatingContent
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.topMargin: 36   // 为标题栏留出空间
            anchors.bottom: parent.bottom
            bridge: editViewBridge
            selectedNodeId: root.currentSelectedNodeId
            // v2.5.0 功能 4：修复预览属性链路
            previewSourceImage: root.previewSourceImage
            previewProcessedImage: root.previewProcessedImage
            onOpenEditorRequested: function(nodeId) {
                root.openEditorForNode(nodeId)
            }
            onPreviewDoubleClicked: function(sourceUrl, processedUrl, title) {
                root.openImagePreview(sourceUrl, processedUrl, title)
            }
            onRequestFloat: root.toggleRightPanelFloating()
            // 浮动模式下的特殊样式（无外部边框，透明背景）
            color: "transparent"
        }
    }

    // ============ v3.1.0: 图像预览独立浮窗 ============
    ImagePreviewWindow {
        id: previewWindow
        // 组件可见性由 open() / close() 控制
    }

    // ============ 部署执行忙碌遮罩（异步部署期间显示，避免误以为卡死）============
    Rectangle {
        id: deployBusyOverlay
        anchors.fill: parent
        color: "#80000000"
        visible: root.deployBusy
        z: 1500
        Column {
            anchors.centerIn: parent
            spacing: 16
            BusyIndicator { running: true; anchors.horizontalCenter: parent.horizontalCenter }
            Text {
                text: "正在执行方案，请稍候…"
                color: "white"
                font.pixelSize: 14
                anchors.horizontalCenter: parent.horizontalCenter
            }
        }
        // 阻止底层点击穿透
        MouseArea {
            anchors.fill: parent
            onClicked: {}
            preventStealing: true
        }
    }

    // ============ Toast 提示 ============
    Popup {
        id: toastPopup
        x: parent.width - width - Tok.DesignTokens.space4
        y: Tok.DesignTokens.headerBarHeight + Tok.DesignTokens.space2
        width: Math.min(contentText.implicitWidth + Tok.DesignTokens.space8, parent.width - 80)
        height: contentText.implicitHeight + Tok.DesignTokens.space6
        modal: false
        focus: false
        closePolicy: Popup.NoAutoClose
        visible: root.toastVisible

        // v4.0: Toast 进出动画
        enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 200 } }
        exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 150 } }
        background: Rectangle {
            radius: Tok.DesignTokens.radiusLg
            color: Tok.DesignTokens.toastBg(root.toastLevel)
            border.color: Tok.DesignTokens.textPrimary
            border.width: Tok.DesignTokens.borderWidth
        }
        contentItem: Label {
            id: contentText
            text: root.toastMessage
            color: Tok.DesignTokens.textPrimary
            // v3.0.0：Base 13→Lg 14，Toast 提示更易读
            font.pixelSize: Tok.DesignTokens.fontSizeLg
            font.family: Tok.DesignTokens.fontFamilyCJK
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            padding: Tok.DesignTokens.space3
        }
    }

    // ============ spec: editor-output-connection-optimization Task5：连接管理面板 ============
    ConnectionManagerPanel {
        id: connectionManagerPanel
        bridge: editViewBridge
        onDeleted: function() { root.showToast("info", "已删除连接") }
    }

    // ============ spec: editor-output-connection-optimization Task7：输入/输出项冲突指定面板 ============
    ConflictPanel {
        id: conflictPanel
        bridge: editViewBridge
        onApplied: function(count) { root.showToast("success", "已消除 " + count + " 处冲突") }
    }

    // ============ v4.0: 快捷键面板 ============
    Shortcut {
        sequences: ["?", "Ctrl+/"]
        onActivated: shortcutsPanel.open()
    }

    ShortcutsPanel {
        id: shortcutsPanel
    }

    // ============ v-spec: 算子库飞级子菜单（悬停分类向右弹出）============
    // 作为 root 的直接子项，声明在 splitView 之后，确保绘制在画布之上
    Rectangle {
        id: operatorFlyout
        visible: operatorList && operatorList.hoveredCategory !== ""
                 && operatorList.flyoutRows.length > 0
        width: 210
        height: operatorList.flyoutHeight()
        // 相对 root 定位：位于算子列表右侧 + 悬停分类行处（考虑列表滚动偏移）
        // v-spec-fix: 向左重叠 6px，消除与分类行之间的间隙，避免鼠标移动途中触发收起
        x: operatorList.mapToItem(root, 0, 0).x + operatorList.width - 6
        y: {
            var base = operatorList.mapToItem(root, 0, 0).y
            var raw = base + (operatorList.hoveredCategoryIndex
                              * Tok.DesignTokens.categoryRowHeight
                              - operatorList.contentY)
            return Math.max(base, Math.min(raw, root.height - height - 8))
        }
        color: Tok.DesignTokens.bgSurface
        border.color: Tok.DesignTokens.borderDefault
        border.width: 1
        radius: Tok.DesignTokens.radiusMd
        z: 9999  // 声明在 splitView 之后 + 高 z，确保绘制在画布之上

        // v-spec-fix: 覆盖整个飞级菜单的保持区域 —— 鼠标在子菜单任意位置都阻止收起，
        // 解决"移出分类行后子菜单在到达前关闭"的问题
        // v-fix: 原实现声明在 ColumnLayout 之前，被其完全覆盖收不到 hover（日志证实
        // flyoutHoverArea ENTERED 从未触发），导致移入子菜单时无人停表、面板被误关。
        // 现将保持区域移到最后（最上层）并设 acceptedButtons: Qt.NoButton：
        // 只接收 hover 事件以维持展开，不消费鼠标按钮，避免挡住算子行的双击添加。

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 3
            spacing: 0

            // 分类标题
            Rectangle {
                Layout.fillWidth: true
                height: 26
                color: Tok.DesignTokens.bgHeader
                radius: Tok.DesignTokens.radiusSm
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Tok.DesignTokens.space2
                    anchors.rightMargin: Tok.DesignTokens.space2
                    spacing: Tok.DesignTokens.space1
                    Rectangle {
                        width: 10; height: 10; radius: 5
                        color: Tok.DesignTokens.categoryColor(operatorList.hoveredCategory)
                    }
                    Label {
                        text: operatorList.hoveredCategory
                        color: Tok.DesignTokens.textPrimary
                        font.bold: true
                        font.pixelSize: Tok.DesignTokens.fontSizeBase
                        font.family: Tok.DesignTokens.fontFamilyCJK
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                    }
                    Label {
                        text: "双击添加"
                        color: Tok.DesignTokens.textTertiary
                        font.pixelSize: Tok.DesignTokens.fontSizeXxs
                        font.family: Tok.DesignTokens.fontFamilyCJK
                    }
                }
            }

            // 算子/子分组列表
            ListView {
                id: flyoutList
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 0
                model: operatorList.flyoutRows

                delegate: Rectangle {
                    width: flyoutList.width
                    height: modelData.kind === "operator"
                        ? Tok.DesignTokens.operatorRowHeight
                        : Tok.DesignTokens.subGroupRowHeight
                    // v-fix3: 显式给算子行铺设深色背景（bgSurface），避免白色文字落在透明/浅色
                    // 底上不可见。原实现行背景透明，若面板/画布底色偏浅，白字即"算子名不显示"。
                    color: Tok.DesignTokens.bgSurface
                    // [RecDiag] 诊断：打印每个 delegate 行实际拿到的数据（经 C++ 落盘）
                    Component.onCompleted: {
                        if (editViewBridge) editViewBridge.logDiag(
                            "[RecDiag] delegate kind=" + modelData.kind
                            + " name=<" + (modelData.name||"") + "> type=<" + (modelData.type||"") + ">"
                            + " listH=" + flyoutList.height
                            + " rowW=" + width + " rowH=" + height
                            + " showBg=" + color
                            + " subGroup=" + (modelData.subGroup||""))
                    }

                    // 整行鼠标处理：双击添加算子；悬停停表（防止二级框收起）
                    // v-fix9: 恢复 hoverEnabled + onEntered/onExited 停表。
                    // 此前 v-fix7 移除本行 hover 后，若 flyoutHoverArea 的 hover 失效，
                    // 则"鼠标进入二级框"将无人停表 → 二级框误收（回归）。此处作为兜底停表路径。
                    MouseArea {
                        anchors.fill: parent
                        hoverEnabled: true
                        onEntered: {
                            if (editViewBridge) editViewBridge.logDiag(
                                "[RecDiag] opRow ENTERED kind=" + modelData.kind
                                + " name=<" + (modelData.name||"") + ">")
                            flyoutCollapseTimer.stop()
                        }
                        onExited: {
                            if (editViewBridge) editViewBridge.logDiag(
                                "[RecDiag] opRow EXITED kind=" + modelData.kind
                                + " name=<" + (modelData.name||"") + ">")
                            flyoutCollapseTimer.start()
                        }
                        onDoubleClicked: {
                            if (modelData.kind === "operator") {
                                var nodeId = editViewBridge.addOperator(
                                    modelData.type,
                                    canvasFrame.width / 2 - Tok.DesignTokens.nodeCardWidth / 2,
                                    canvasFrame.height / 2 - Tok.DesignTokens.nodeCardHeight / 2)
                                if (nodeId) {
                                    editViewBridge.selectNode(nodeId)
                                    root.openEditorForNode(nodeId)
                                }
                                operatorList.hoveredCategory = ""
                            }
                        }
                    }

                    // 子分组头
                    RowLayout {
                        visible: modelData.kind === "subGroup"
                        anchors.fill: parent
                        anchors.leftMargin: Tok.DesignTokens.space2
                        spacing: 4
                        Rectangle {
                            width: 3; height: 12; radius: 1
                            color: Tok.DesignTokens.accentSuccess
                        }
                        Label {
                            text: modelData.subGroup
                            color: Tok.DesignTokens.textSecondary
                            font.bold: true
                            font.pixelSize: Tok.DesignTokens.fontSizeSm
                            font.family: Tok.DesignTokens.fontFamilyCJK
                        }
                    }

                    // 算子行
                    RowLayout {
                        visible: modelData.kind === "operator"
                        anchors.fill: parent
                        anchors.leftMargin: modelData.subGroup
                            ? Tok.DesignTokens.space6 : Tok.DesignTokens.space2
                        anchors.rightMargin: Tok.DesignTokens.space2
                        spacing: Tok.DesignTokens.space2
                        Rectangle {
                            width: 10; height: 10; radius: 3
                            color: Tok.DesignTokens.categoryColor(operatorList.hoveredCategory)
                        }
                        Label {
                            // v-spec-fix: 仅在算子行求值，避免子分组行（无 name 字段）触发
                            // "Unable to assign [undefined] to QString" 绑定错误，
                            // 该错误会干扰 delegate 渲染并波及算子名称显示
                            text: modelData.kind === "operator" ? (modelData.name || "") : ""
                            color: Tok.DesignTokens.textPrimary
                            font.pixelSize: Tok.DesignTokens.fontSizeBase
                            font.bold: true
                            font.family: Tok.DesignTokens.fontFamilyCJK
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        // 收藏星标（单击切换收藏，双击不触发添加）
                        Label {
                            text: (modelData.kind === "operator" && modelData.type)
                                ? (editViewBridge.isFavorite(modelData.type)
                                    ? "\u2605" : "\u2606")
                                : ""
                            color: (modelData.kind === "operator" && modelData.type
                                    && editViewBridge.isFavorite(modelData.type))
                                ? Tok.DesignTokens.accentWarning : Tok.DesignTokens.textDisabled
                            font.pixelSize: Tok.DesignTokens.fontSizeSm
                            visible: modelData.kind === "operator"
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                enabled: modelData.kind === "operator"
                                onDoubleClicked: mouse.accepted = true
                                onClicked: {
                                    if (modelData.type) {
                                        editViewBridge.toggleFavorite(modelData.type)
                                        operatorList.updateFlyout()
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // v-fix: 最上层保持区域 —— 覆盖整个子菜单。
        // v-fix2: 保留 acceptedButtons: Qt.NoButton（不拦截点击，避免吞掉算子行双击/星标点击）。
        //   保持 hover 的主要机制是算子行的 onEntered/onExited 停表（行级 MouseArea 无 NoButton，
        //   hover 有效）；本层 NoButton 虽不接收 hover，但作为透明覆盖无害，故保留。
        MouseArea {
            id: flyoutHoverArea
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.NoButton
            // [RecDiag] hover 诊断（经 C++ 落盘）
            onEntered: {
                if (editViewBridge) editViewBridge.logDiag("[RecDiag] flyoutHoverArea ENTERED"
                    + " pos=" + Math.round(operatorFlyout.x) + "," + Math.round(operatorFlyout.y)
                    + " size=" + Math.round(operatorFlyout.width) + "x" + Math.round(operatorFlyout.height)
                    + " list=" + operatorFlyout.width + "x" + flyoutList.height)
                flyoutCollapseTimer.stop()
            }
            onExited: { if (editViewBridge) editViewBridge.logDiag("[RecDiag] flyoutHoverArea EXITED"); flyoutCollapseTimer.start() }
        }
    }
}