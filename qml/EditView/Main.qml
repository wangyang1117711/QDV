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
    width: 1280
    height: 800
    color: Tok.DesignTokens.bgCanvas

    property string currentSelectedNodeId: ""
    property string toastMessage: ""
    property string toastLevel: "info"
    property bool   toastVisible: false
    property string searchKeyword: ""
    property string activeFilter: "all"
    property real   canvasZoom: 1.0
    property var    selectedNodeIds: []    // v3.0.0：多选

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

    // v2.5.0 功能 2：连线命中检测
    // 计算屏幕坐标 (px, py) 到第 idx 条连线的最短距离
    // 连线为贝塞尔曲线，采样 20 个点近似为线段，计算点到线段最短距离
    // 返回值：距离 < threshold 时返回最近的那条连线索引，-1 表示未命中
    function hitTestConnection(px, py) {
        var nodes = editViewBridge.currentNodes
        var conns = editViewBridge.connections
        var z = root.canvasZoom
        var ox = root.canvasOffsetX
        var oy = root.canvasOffsetY
        var nodeW = Tok.DesignTokens.nodeCardWidth
        var nodeH = Tok.DesignTokens.nodeCardHeight
        var threshold = 12  // 命中阈值（像素），适当放宽以提高点击成功率
        var livePos = connectionsCanvas.livePositions  // v2.5.0 修复：实时位置字典

        // v2.5.0 修复：优先用 livePositions 中的实时坐标
        function getNodePos(node) {
            var live = livePos[node.id]
            if (live !== undefined) return {x: live.x, y: live.y}
            return {x: node.x, y: node.y}
        }

        var bestIdx = -1
        var bestDist = threshold
        for (var ci = 0; ci < conns.length; ci++) {
            var conn = conns[ci]
            var fromNode = null, toNode = null
            for (var ni = 0; ni < nodes.length; ni++) {
                if (nodes[ni].id === conn.fromId) fromNode = nodes[ni]
                if (nodes[ni].id === conn.toId) toNode = nodes[ni]
            }
            if (!fromNode || !toNode) continue

            // v2.5.0 修复：用实时坐标计算贝塞尔曲线端点
            var fromPos = getNodePos(fromNode)
            var toPos = getNodePos(toNode)
            // 贝塞尔曲线端点（屏幕坐标）
            var fx = (fromPos.x + nodeW) * z + ox
            var fy = (fromPos.y + nodeH / 2) * z + oy
            var tx = toPos.x * z + ox
            var ty = (toPos.y + nodeH / 2) * z + oy
            var cx1 = fx + Math.abs(tx - fx) * 0.4
            var cx2 = tx - Math.abs(tx - fx) * 0.4

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
                      + 3 * oneMinusU * oneMinusU * u * fy
                      + 3 * oneMinusU * u * u * ty
                      + u * u * u * ty

                // 点 (px,py) 到线段 (prevX,prevY)-(x,y) 的最短距离
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
            // 收藏变化时刷新算子库面板
            if (root.activeFilter === "favorites") {
                operatorListView.model = root.getFilteredOperators()
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
    }

    // ============ 文件对话框 ============
    // v5.3.1：修复保存失败 bug —— String(selectedFile) 不解码百分号编码，
    // 中文/空格路径会变成 %20 等编码，导致 QSaveFile 找不到目录。
    // 正确做法：用 decodeURIComponent 解码 QUrl 的百分号编码。
    // 同时增加多格式支持：JSON / 压缩包 .qdvz / XML
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
                        property var collapsed: ({})
                        property var filteredList: getFilteredOperators()

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

                        function buildModel() {
                            var rows = []
                            var ops = getFilteredOperators()
                            var catMap = {}
                            for (var i = 0; i < ops.length; ++i) {
                                var op = ops[i]
                                var cat = op.category || "其他"
                                if (!catMap[cat]) catMap[cat] = []
                                catMap[cat].push(op)
                            }
                            var cats = Object.keys(catMap).sort()
                            for (var ci = 0; ci < cats.length; ++ci) {
                                var cat = cats[ci]
                                rows.push({ kind: "category", category: cat,
                                    expanded: !operatorList.collapsed[cat] })
                                if (!operatorList.collapsed[cat]) {
                                    var catOps = catMap[cat]
                                    var sgMap = {}, noSg = []
                                    for (var oi = 0; oi < catOps.length; ++oi) {
                                        var sg = catOps[oi].subGroup || ""
                                        if (sg) {
                                            if (!sgMap[sg]) sgMap[sg] = []
                                            sgMap[sg].push(catOps[oi])
                                        } else { noSg.push(catOps[oi]) }
                                    }
                                    for (var ni = 0; ni < noSg.length; ++ni)
                                        rows.push({ kind: "operator", category: cat, subGroup: "", meta: noSg[ni] })
                                    var sgKeys = Object.keys(sgMap).sort()
                                    for (var si = 0; si < sgKeys.length; ++si) {
                                        var sg = sgKeys[si], sgKey = cat + "::" + sg
                                        rows.push({ kind: "subGroup", category: cat, subGroup: sg,
                                            expanded: !operatorList.collapsed[sgKey] })
                                        if (!operatorList.collapsed[sgKey]) {
                                            for (var soi = 0; soi < sgMap[sg].length; ++soi)
                                                rows.push({ kind: "operator", category: cat, subGroup: sg, meta: sgMap[sg][soi] })
                                        }
                                    }
                                }
                            }
                            return rows
                        }
                        model: buildModel()

                        delegate: Rectangle {
                            width: ListView.view.width
                            height: modelData.kind === "category"
                                ? Tok.DesignTokens.categoryRowHeight
                                : modelData.kind === "subGroup"
                                    ? Tok.DesignTokens.subGroupRowHeight
                                    : Tok.DesignTokens.operatorRowHeight

                            // 分类行
                            Rectangle {
                                anchors.fill: parent
                                visible: modelData.kind === "category"
                                color: catArea.containsMouse ? Tok.DesignTokens.bgHover : Tok.DesignTokens.bgHeader
                                radius: Tok.DesignTokens.radiusSm

                                Row {
                                    anchors.fill: parent
                                    anchors.leftMargin: Tok.DesignTokens.space1
                                    anchors.rightMargin: Tok.DesignTokens.space1
                                    spacing: Tok.DesignTokens.space1

                                    Label {
                                        text: modelData.expanded ? "\u25BE" : "\u25B8"
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
                                        // v5.0：分类行文字 Lg 14→Base 13
                                        text: modelData.category
                                        color: Tok.DesignTokens.textPrimary
                                        font.bold: true
                                        font.pixelSize: Tok.DesignTokens.fontSizeBase  // v5.0：Lg→Base
                                        font.family: Tok.DesignTokens.fontFamilyCJK
                                        font.letterSpacing: 0.5
                                        lineHeight: Tok.DesignTokens.lineHeightNormal
                                        lineHeightMode: Text.ProportionalHeight
                                        anchors.verticalCenter: parent.verticalCenter
                                    }
                                }
                                MouseArea {
                                    id: catArea
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        operatorList.collapsed[modelData.category] = !!modelData.expanded
                                        operatorList.collapsed = operatorList.collapsed
                                        operatorList.model = operatorList.buildModel()
                                    }
                                }
                            }

                            // 子分组行
                            Rectangle {
                                anchors.fill: parent
                                visible: modelData.kind === "subGroup"
                                color: sgArea.containsMouse ? Tok.DesignTokens.bgHover : Tok.DesignTokens.bgPanel

                                Row {
                                    anchors.fill: parent
                                    anchors.leftMargin: Tok.DesignTokens.space4 + 2
                                    anchors.rightMargin: Tok.DesignTokens.space1
                                    spacing: Tok.DesignTokens.space1

                                    Label {
                                        text: modelData.expanded ? "\u25BE" : "\u25B8"
                                        color: Tok.DesignTokens.accentSuccess
                                        font.pixelSize: Tok.DesignTokens.fontSizeSm
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: 12
                                    }
                                    Label {
                                        // v3.0.1：子分组行文字优化——Sm 12→Base 13
                                        // 在分类(14)和算子(14)之间，13px 中间字号形成层次
                                        text: modelData.subGroup || ""
                                        color: Tok.DesignTokens.textSecondary
                                        font.pixelSize: Tok.DesignTokens.fontSizeBase
                                        font.family: Tok.DesignTokens.fontFamilyCJK
                                        font.letterSpacing: 0.3
                                        lineHeight: Tok.DesignTokens.lineHeightNormal
                                        lineHeightMode: Text.ProportionalHeight
                                        anchors.verticalCenter: parent.verticalCenter
                                    }
                                }
                                MouseArea {
                                    id: sgArea
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        operatorList.collapsed[modelData.category + "::" + modelData.subGroup] = !!modelData.expanded
                                        operatorList.collapsed = operatorList.collapsed
                                        operatorList.model = operatorList.buildModel()
                                    }
                                }
                            }

                            // 算子行
                            Rectangle {
                                anchors.fill: parent
                                visible: modelData.kind === "operator"
                                // v3.0.1：用 bgPanel 替代 "transparent"，
                                // 避免不同 Qt 版本下 transparent 解析为白色导致白底白字
                                color: opArea.containsMouse ? Tok.DesignTokens.bgHover : Tok.DesignTokens.bgPanel
                                radius: Tok.DesignTokens.radiusSm

                                Row {
                                    anchors.fill: parent
                                    anchors.leftMargin: modelData.subGroup ? Tok.DesignTokens.space8 : 20
                                    anchors.rightMargin: Tok.DesignTokens.space2
                                    spacing: Tok.DesignTokens.space2

                                    Rectangle {
                                        // v5.0：分类色块 16×16→12×12
                                        width: 12; height: 12; radius: 3
                                        color: Tok.DesignTokens.categoryColor(modelData.category)
                                        anchors.verticalCenter: parent.verticalCenter
                                    }
                                    Label {
                                        // v5.0：算子行文字字号 Lg 14→Base 13
                                        text: (modelData.meta && modelData.meta.cnName)
                                            ? modelData.meta.cnName : modelData.category
                                        color: Tok.DesignTokens.textPrimary
                                        font.pixelSize: Tok.DesignTokens.fontSizeBase  // v5.0：Lg→Base
                                        font.bold: true
                                        font.family: Tok.DesignTokens.fontFamilyCJK
                                        font.hintingPreference: Font.PreferDefaultHinting
                                        font.letterSpacing: 0.2
                                        lineHeight: Tok.DesignTokens.lineHeightRelax
                                        lineHeightMode: Text.ProportionalHeight
                                        elide: Text.ElideRight
                                        wrapMode: Text.NoWrap
                                        anchors.verticalCenter: parent.verticalCenter
                                        // v5.0：色块变小后留更多文字空间
                                        width: parent.width - 56
                                        Accessible.role: Accessible.StaticText
                                        Accessible.name: (modelData.meta && modelData.meta.cnName)
                                            ? modelData.meta.cnName : modelData.category
                                    }
                                    // 收藏星标
                                    Label {
                                        text: editViewBridge.isFavorite(
                                            modelData.meta ? modelData.meta.type : "") ? "\u2605" : "\u2606"
                                        color: editViewBridge.isFavorite(
                                            modelData.meta ? modelData.meta.type : "")
                                            ? Tok.DesignTokens.accentWarning : Tok.DesignTokens.textDisabled
                                        font.pixelSize: Tok.DesignTokens.fontSizeSm  // v5.0：Lg→Sm
                                        anchors.verticalCenter: parent.verticalCenter
                                        MouseArea {
                                            anchors.fill: parent
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: {
                                                if (modelData.meta) {
                                                    editViewBridge.toggleFavorite(modelData.meta.type)
                                                    if (root.activeFilter === "favorites") {
                                                        operatorList.model = null
                                                        operatorList.model = operatorList.buildModel()
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                                MouseArea {
                                    id: opArea
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    Accessible.role: Accessible.ListItem
                                    Accessible.name: modelData.meta ? modelData.meta.cnName : "算子"
                                    Accessible.description: (modelData.meta ? modelData.meta.cnName : "算子") + " - " + modelData.category + " 类算子，双击添加到画布"
                                    onDoubleClicked: {
                                        var nodeId = editViewBridge.addOperator(
                                            modelData.meta.type,
                                            canvasFrame.width / 2 - Tok.DesignTokens.nodeCardWidth / 2,
                                            canvasFrame.height / 2 - Tok.DesignTokens.nodeCardHeight / 2)
                                        if (nodeId) {
                                            editViewBridge.selectNode(nodeId)
                                            root.openEditorForNode(nodeId)
                                        }
                                    }
                                }
                            }
                        }
                        ScrollBar.vertical: ScrollBar {}
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
                        var nodes = editViewBridge.currentNodes
                        var conns = editViewBridge.connections
                        var z = root.canvasZoom
                        var ox = root.canvasOffsetX
                        var oy = root.canvasOffsetY
                        var nodeW = Tok.DesignTokens.nodeCardWidth
                        var nodeH = Tok.DesignTokens.nodeCardHeight
                        var livePos = connectionsCanvas.livePositions

                        // v2.5.0 修复：获取节点世界坐标（优先用 livePositions 中的实时坐标）
                        function getNodePos(node) {
                            var live = livePos[node.id]
                            if (live !== undefined) {
                                return {x: live.x, y: live.y}
                            }
                            return {x: node.x, y: node.y}
                        }

                        for (var ci = 0; ci < conns.length; ci++) {
                            var conn = conns[ci]
                            var fromNode = null, toNode = null
                            for (var ni = 0; ni < nodes.length; ni++) {
                                if (nodes[ni].id === conn.fromId) fromNode = nodes[ni]
                                if (nodes[ni].id === conn.toId) toNode = nodes[ni]
                            }
                            if (!fromNode || !toNode) continue
                            // v2.5.0 修复：用 livePositions 中的实时坐标计算端点
                            var fromPos = getNodePos(fromNode)
                            var toPos = getNodePos(toNode)
                            // P1-B01：屏幕坐标 = world * zoom + offset
                            var fx = (fromPos.x + nodeW) * z + ox
                            var fy = (fromPos.y + nodeH / 2) * z + oy
                            var tx = toPos.x * z + ox
                            var ty = (toPos.y + nodeH / 2) * z + oy
                            var cx1 = fx + Math.abs(tx - fx) * 0.4
                            var cx2 = tx - Math.abs(tx - fx) * 0.4

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
                            ctx.bezierCurveTo(cx1, fy, cx2, ty, tx, ty)
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
                            anchors.fill: parent
                            drag.target: nodeCard
                            // P1-B01：移除 minimumX/maximumX 限制，节点可拖到画布外（用户可平移画布找回）
                            drag.threshold: 1
                            cursorShape: Qt.OpenHandCursor
                            acceptedButtons: Qt.LeftButton | Qt.RightButton

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

    // ============ v4.0: 快捷键面板 ============
    Shortcut {
        sequences: ["?", "Ctrl+/"]
        onActivated: shortcutsPanel.open()
    }

    ShortcutsPanel {
        id: shortcutsPanel
    }
}