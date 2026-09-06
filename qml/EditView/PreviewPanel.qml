// =====================================================================
// QDV PreviewPanel.qml（v2.6.0 预览窗口）
//
// 功能：
//   1. 显示当前选中算子节点的输出图像
//   2. 支持节点切换下拉框（从 ImageVariableManager 获取所有有输出的节点）
//   3. Halcon 风格缩放（滚轮）/ 平移（左键拖拽）/ 适应窗口
//   4. 实时预览开关（绑定 PreviewManager.autoPreviewEnabled）
//   5. 显示图像信息（尺寸、通道数、算子名称）
//   6. 支持固定/取消固定（pinned 状态）
//
// 数据来源：
//   - bridge.imageVariableManager.imageVariables() → 节点列表
//   - bridge.previewManager.previewNodeId → 当前预览节点
//   - bridge.imageVariableManager.imageVariable(nodeId).outputImagePath → 图像路径
// =====================================================================

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QDV.EditView 3.0 as Tok

Rectangle {
    id: root
    color: Tok.DesignTokens.bgPanel
    border.color: Tok.DesignTokens.borderDefault
    border.width: Tok.DesignTokens.borderWidth

    // === 外部注入 ===
    property var bridge: null              // EditViewBridge 实例
    property bool pinned: false            // 是否固定（固定时不随选中节点切换）
    property bool autoPreview: true        // 实时预览开关

    // === 内部状态 ===
    property string currentPreviewNodeId: ""   // 当前预览节点 ID
    property string currentImagePath: ""       // 当前预览图像路径
    property var imageInfo: ({})               // 图像信息 {width, height, channels, toolName}
    property real zoomFactor: 1.0              // 缩放因子
    property real offsetX: 0                   // 图像平移 X
    property real offsetY: 0                   // 图像平移 Y
    property bool isDragging: false            // 是否正在拖拽
    property real dragStartX: 0
    property real dragStartY: 0
    property real dragStartOffsetX: 0
    property real dragStartOffsetY: 0

    // === 分屏对比模式 ===
    property bool splitModeEnabled: false      // 分屏模式开关
    property string splitBeforePath: ""        // before 图像路径（file:/// URL）
    property string splitAfterPath: ""         // after 图像路径（file:/// URL）

    // === 信号 ===
    signal pinToggled()
    signal requestHide()

    // === 监听 PreviewManager 状态 ===
    Connections {
        target: bridge && bridge.previewManager ? bridge.previewManager : null
        enabled: target !== null

        function onPreviewNodeIdChanged(nodeId) {
            if (!root.pinned) {
                root.currentPreviewNodeId = nodeId
                root.refreshImage()
            }
        }
        function onPreviewCompleted(nodeId, success, outputPath) {
            if (nodeId === root.currentPreviewNodeId && success) {
                root.currentImagePath = outputPath
                root.refreshImage()
                // 同步分屏 after 路径（after 即当前预览输出）
                if (root.splitModeEnabled && outputPath) {
                    root.splitAfterPath = "file:///" + outputPath.replace(/\\/g, "/")
                }
            }
        }
        function onSnapshotAdded() {
            root.updateSplitPaths()
        }
        function onSnapshotsCleared() {
            root.splitBeforePath = ""
            root.splitAfterPath = ""
        }
    }

    // === 监听 ImageVariableManager 信号 ===
    Connections {
        target: bridge && bridge.imageVariableManager ? bridge.imageVariableManager : null
        enabled: target !== null

        function onImageVariableUpdated(nodeId, outputPath) {
            if (nodeId === root.currentPreviewNodeId) {
                root.currentImagePath = outputPath
                root.refreshImage()
            }
        }
        function onImageVariablesChanged() {
            nodeSelector.model = root.buildNodeList()
        }
    }

    // === 初始化 ===
    Component.onCompleted: {
        if (bridge && bridge.previewManager) {
            bridge.previewManager.autoPreviewEnabled = root.autoPreview
            // 兜底：previewNodeId 初始可能为 undefined（Q_PROPERTY 未被设置过）
            var pid = bridge.previewManager.previewNodeId
            root.currentPreviewNodeId = (pid === undefined || pid === null) ? "" : pid
        }
        nodeSelector.model = root.buildNodeList()
        root.refreshImage()
    }

    // === 构建节点列表（从 ImageVariableManager 获取有输出图像的节点）===
    function buildNodeList() {
        if (!bridge || !bridge.imageVariableManager) return []
        var list = bridge.imageVariableManager.imageVariables()
        var result = []
        for (var i = 0; i < list.length; i++) {
            var item = list[i]
            result.push({
                nodeId: item.nodeId,
                toolName: item.toolName,
                label: item.toolName + " (" + item.nodeId.substring(0, 6) + ")"
            })
        }
        return result
    }

    // === 刷新图像显示 ===
    function refreshImage() {
        if (!bridge || !bridge.imageVariableManager || !root.currentPreviewNodeId) {
            root.currentImagePath = ""
            root.imageInfo = {}
            return
        }
        var info = bridge.imageVariableManager.imageVariable(root.currentPreviewNodeId)
        if (info && info.outputImagePath) {
            root.currentImagePath = "file:///" + info.outputImagePath.replace(/\\/g, "/")
            root.imageInfo = info
            // 强制 Image 重新加载
            previewImage.source = ""
            previewImage.source = root.currentImagePath
        } else {
            root.currentImagePath = ""
            root.imageInfo = {}
        }
        // 重置缩放
        root.zoomFactor = 1.0
        root.offsetX = 0
        root.offsetY = 0
    }

    // === 适应窗口 ===
    function fitToWindow() {
        if (!previewImage.source || previewImage.status !== Image.Ready) return
        var scaleX = previewContainer.width / previewImage.sourceSize.width
        var scaleY = previewContainer.height / previewImage.sourceSize.height
        root.zoomFactor = Math.min(scaleX, scaleY, 1.0)
        root.offsetX = 0
        root.offsetY = 0
    }

    // === 切换预览节点 ===
    function selectNode(nodeId) {
        root.currentPreviewNodeId = nodeId
        if (bridge && bridge.previewManager) {
            bridge.previewManager.previewNodeId = nodeId
            bridge.previewManager.previewNow()
        }
        root.refreshImage()
    }

    // === 更新分屏对比路径（从最新 Snapshot 获取 before/after）===
    function updateSplitPaths() {
        if (!bridge || !bridge.previewManager) return
        var beforePath = bridge.previewManager.latestSnapshotBeforePath()
        var afterPath = bridge.previewManager.latestSnapshotAfterPath()
        // before 路径：来自最新快照的 before 图像
        root.splitBeforePath = beforePath ? "file:///" + beforePath.replace(/\\/g, "/") : ""
        // after 路径：优先用快照 after，回退到当前预览输出
        if (afterPath) {
            root.splitAfterPath = "file:///" + afterPath.replace(/\\/g, "/")
        } else if (root.currentImagePath) {
            root.splitAfterPath = root.currentImagePath
        }
    }

    // === 主布局 ===
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 0
        spacing: 0

        // === 顶部工具栏（32px）===
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 36
            color: Tok.DesignTokens.bgSurface
            border.color: Tok.DesignTokens.borderDefault
            border.width: 0

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Tok.DesignTokens.space2
                anchors.rightMargin: Tok.DesignTokens.space2
                spacing: Tok.DesignTokens.space2

                // 预览节点选择下拉框
                Label {
                    text: "节点:"
                    color: Tok.DesignTokens.textSecondary
                    font.pixelSize: Tok.DesignTokens.fontSizeSm
                }
                ComboBox {
                    id: nodeSelector
                    Layout.preferredWidth: 180
                    Layout.maximumWidth: 220
                    textRole: "label"
                    valueRole: "nodeId"
                    font.pixelSize: Tok.DesignTokens.fontSizeSm
                    model: []
                    onActivated: function(index) {
                        var nodeId = nodeSelector.currentValue
                        if (nodeId) root.selectNode(nodeId)
                    }
                }

                // 立即执行按钮
                Button {
                    text: "▶"
                    Layout.preferredWidth: 32
                    Layout.preferredHeight: 24
                    font.pixelSize: Tok.DesignTokens.fontSizeSm
                    ToolTip.text: "立即执行预览"
                    ToolTip.visible: hovered
                    ToolTip.delay: 500
                    enabled: root.currentPreviewNodeId !== ""
                    onClicked: {
                        if (bridge && bridge.previewManager) {
                            bridge.previewManager.previewNow()
                        }
                    }
                }

                // 适应窗口按钮
                Button {
                    text: "⤢"
                    Layout.preferredWidth: 32
                    Layout.preferredHeight: 24
                    font.pixelSize: Tok.DesignTokens.fontSizeLg
                    ToolTip.text: "适应窗口"
                    ToolTip.visible: hovered
                    ToolTip.delay: 500
                    onClicked: root.fitToWindow()
                }

                // 分屏对比切换按钮
                Button {
                    text: "分屏"
                    checkable: true
                    checked: root.splitModeEnabled
                    Layout.preferredWidth: 48
                    Layout.preferredHeight: 24
                    font.pixelSize: Tok.DesignTokens.fontSizeSm
                    ToolTip.text: "前后对比分屏视图"
                    ToolTip.visible: hovered
                    ToolTip.delay: 500
                    onClicked: {
                        root.splitModeEnabled = !root.splitModeEnabled
                        if (root.splitModeEnabled) {
                            root.updateSplitPaths()
                        }
                    }
                }

                // 缩放比例显示
                Label {
                    text: (root.zoomFactor * 100).toFixed(0) + "%"
                    color: Tok.DesignTokens.textTertiary
                    font.pixelSize: Tok.DesignTokens.fontSizeSm
                    Layout.preferredWidth: 44
                    horizontalAlignment: Text.AlignRight
                }

                Item { Layout.fillWidth: true }

                // 实时预览开关
                Switch {
                    text: "实时"
                    checked: root.autoPreview
                    font.pixelSize: Tok.DesignTokens.fontSizeSm
                    Layout.preferredHeight: 24
                    onCheckedChanged: {
                        root.autoPreview = checked
                        if (bridge && bridge.previewManager) {
                            bridge.previewManager.autoPreviewEnabled = checked
                        }
                    }
                }

                // 固定/取消固定按钮
                Button {
                    text: root.pinned ? "🔒" : "📌"
                    Layout.preferredWidth: 32
                    Layout.preferredHeight: 24
                    font.pixelSize: Tok.DesignTokens.fontSizeSm
                    ToolTip.text: root.pinned ? "取消固定" : "固定预览节点"
                    ToolTip.visible: hovered
                    ToolTip.delay: 500
                    onClicked: root.pinToggled()
                }

                // 隐藏按钮
                Button {
                    text: "▼"
                    Layout.preferredWidth: 28
                    Layout.preferredHeight: 24
                    font.pixelSize: Tok.DesignTokens.fontSizeSm
                    ToolTip.text: "隐藏预览窗口"
                    ToolTip.visible: hovered
                    ToolTip.delay: 500
                    onClicked: root.requestHide()
                }
            }
        }

        // === 主预览区域 ===
        Rectangle {
            id: previewContainer
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Tok.DesignTokens.bgCanvas
            clip: true

            // 图像
            Image {
                id: previewImage
                source: root.currentImagePath
                fillMode: Image.PreserveAspectFit
                // P0-4c（0906 优化）：恢复异步解码 + 缓存（QRhi 问题由渲染层三件套规避，
                // 见 ImageViewer.qml 同项注释）
                asynchronous: true
                cache: true
                visible: source !== "" && status === Image.Ready && !root.splitModeEnabled
                anchors.centerIn: parent

                // 缩放变换
                transform: [
                    Scale { xScale: root.zoomFactor; yScale: root.zoomFactor },
                    Translate { x: root.offsetX; y: root.offsetY }
                ]

                onStatusChanged: {
                    if (status === Image.Error) {
                        console.warn("PreviewPanel: 图像加载失败", root.currentImagePath)
                    }
                }
            }

            // 占位提示（无图像时）
            Label {
                anchors.centerIn: parent
                visible: root.currentImagePath === "" && !root.splitModeEnabled
                text: root.currentPreviewNodeId === "" ? "请选择算子节点" : "等待预览执行..."
                color: Tok.DesignTokens.textTertiary
                font.pixelSize: Tok.DesignTokens.fontSizeLg
            }

            // 加载中提示
            Label {
                anchors.centerIn: parent
                visible: root.currentImagePath !== "" && previewImage.status === Image.Loading && !root.splitModeEnabled
                text: "加载中..."
                color: Tok.DesignTokens.textSecondary
                font.pixelSize: Tok.DesignTokens.fontSizeBase
            }

            // 滚轮缩放
            MouseArea {
                anchors.fill: parent
                visible: !root.splitModeEnabled
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                hoverEnabled: true
                onWheel: function(wheel) {
                    if (wheel.angleDelta.y > 0) {
                        root.zoomFactor = Math.min(root.zoomFactor * 1.15, 10.0)
                    } else {
                        root.zoomFactor = Math.max(root.zoomFactor / 1.15, 0.1)
                    }
                }
                onPressed: function(mouse) {
                    if (mouse.button === Qt.LeftButton) {
                        root.isDragging = true
                        root.dragStartX = mouse.x
                        root.dragStartY = mouse.y
                        root.dragStartOffsetX = root.offsetX
                        root.dragStartOffsetY = root.offsetY
                    }
                }
                onPositionChanged: function(mouse) {
                    if (root.isDragging) {
                        root.offsetX = root.dragStartOffsetX + (mouse.x - root.dragStartX)
                        root.offsetY = root.dragStartOffsetY + (mouse.y - root.dragStartY)
                    }
                }
                onReleased: {
                    root.isDragging = false
                }
                onDoubleClicked: root.fitToWindow()
            }

            // 右下角图像信息
            Rectangle {
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.rightMargin: Tok.DesignTokens.space2
                anchors.bottomMargin: Tok.DesignTokens.space2
                visible: !root.splitModeEnabled
                width: infoRow.implicitWidth + Tok.DesignTokens.space4
                height: infoRow.implicitHeight + Tok.DesignTokens.space2
                color: Tok.DesignTokens.bgSurface
                opacity: 0.85
                radius: Tok.DesignTokens.radiusSm

                RowLayout {
                    id: infoRow
                    anchors.centerIn: parent
                    spacing: Tok.DesignTokens.space2

                    Label {
                        text: root.imageInfo.toolName || ""
                        color: Tok.DesignTokens.accentPrimary
                        font.pixelSize: Tok.DesignTokens.fontSizeXs
                        font.bold: true
                    }
                    Label {
                        text: (root.imageInfo.width || 0) + "×" + (root.imageInfo.height || 0)
                        color: Tok.DesignTokens.textSecondary
                        font.pixelSize: Tok.DesignTokens.fontSizeXs
                    }
                    Label {
                        text: (root.imageInfo.channels || 0) + "ch"
                        color: Tok.DesignTokens.textTertiary
                        font.pixelSize: Tok.DesignTokens.fontSizeXs
                    }
                }
            }

            // === 分屏对比视图（左 before / 右 after）===
            Rectangle {
                id: splitViewContainer
                anchors.fill: parent
                visible: root.splitModeEnabled
                color: Tok.DesignTokens.bgCanvas

                RowLayout {
                    anchors.fill: parent
                    spacing: 1

                    // 左侧：before 图像
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        color: Tok.DesignTokens.bgCanvas
                        clip: true

                        Image {
                            id: beforeImage
                            anchors.fill: parent
                            fillMode: Image.PreserveAspectFit
                            asynchronous: true  // P0-4c：恢复异步解码（QRhi 由渲染层三件套规避）
                            cache: true
                            source: root.splitBeforePath
                            visible: source !== "" && status === Image.Ready
                        }

                        // Before 标签
                        Label {
                            anchors.top: parent.top
                            anchors.left: parent.left
                            anchors.margins: Tok.DesignTokens.space2
                            text: "Before"
                            color: Tok.DesignTokens.textSecondary
                            font.pixelSize: Tok.DesignTokens.fontSizeSm
                            padding: 4
                            background: Rectangle {
                                color: Tok.DesignTokens.bgSurface
                                opacity: 0.7
                                radius: Tok.DesignTokens.radiusSm
                            }
                        }

                        // 无 before 图像提示
                        Label {
                            anchors.centerIn: parent
                            visible: root.splitBeforePath === "" || beforeImage.status !== Image.Ready
                            text: "无 Before 图像"
                            color: Tok.DesignTokens.textTertiary
                            font.pixelSize: Tok.DesignTokens.fontSizeBase
                        }
                    }

                    // 中间分隔线
                    Rectangle {
                        Layout.preferredWidth: 2
                        Layout.fillHeight: true
                        color: Tok.DesignTokens.borderDefault
                    }

                    // 右侧：after 图像
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        color: Tok.DesignTokens.bgCanvas
                        clip: true

                        Image {
                            id: afterImage
                            anchors.fill: parent
                            fillMode: Image.PreserveAspectFit
                            asynchronous: true  // P0-4c：恢复异步解码（QRhi 由渲染层三件套规避）
                            cache: true
                            source: root.splitAfterPath
                            visible: source !== "" && status === Image.Ready
                        }

                        // After 标签
                        Label {
                            anchors.top: parent.top
                            anchors.left: parent.left
                            anchors.margins: Tok.DesignTokens.space2
                            text: "After"
                            color: Tok.DesignTokens.accentPrimary
                            font.pixelSize: Tok.DesignTokens.fontSizeSm
                            padding: 4
                            background: Rectangle {
                                color: Tok.DesignTokens.bgSurface
                                opacity: 0.7
                                radius: Tok.DesignTokens.radiusSm
                            }
                        }

                        // 无 after 图像提示
                        Label {
                            anchors.centerIn: parent
                            visible: root.splitAfterPath === "" || afterImage.status !== Image.Ready
                            text: "无 After 图像"
                            color: Tok.DesignTokens.textTertiary
                            font.pixelSize: Tok.DesignTokens.fontSizeBase
                        }
                    }
                }
            }
        }
    }
}
