// =====================================================================
// PropertyPreviewPanel.qml — 右侧算子详情面板（v3.0.0 方案B Design Tokens 迁移）
//
// 功能：
// - 功能说明区：算子名称/分类/描述
// - 输入参数区：动态表单 ParamForm
// - 输出参数区：动态渲染输出参数列表
// - 使用示例区：典型用法说明
// - 效果预览区：缩略图预览
// - 底部操作栏：应用 & 重置按钮
// =====================================================================

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QDV.EditView 3.0 as Tok

Rectangle {
    id: root
    color: Tok.DesignTokens.bgPanel

    // ============ 外部接口 ============
    property var bridge
    property string selectedNodeId: ""
    property var selectedNode: null
    property var currentMeta: null
    property var currentParams: ({})
    // v3.1.0: 预览图像路径
    property url previewSourceImage: ""
    property url previewProcessedImage: ""

    signal openEditorRequested(string nodeId)
    // v3.1.0: 双击效果预览图打开独立窗口
    signal previewDoubleClicked(url sourceUrl, url processedUrl, string title)
    // v3.1.0: 请求浮动/停靠切换
    signal requestFloat()
    // v3.2.0: 请求隐藏算子参数编辑器（与 pin 状态联动）
    signal requestHide()

    Accessible.role: Accessible.Pane
    Accessible.name: "算子详情面板"
    Accessible.description: "显示选中算子的详细信息、参数和预览"

    onSelectedNodeIdChanged: refresh()
    onBridgeChanged: refresh()

    function refresh() {
        selectedNode = null
        currentMeta = null
        currentParams = ({})
        if (!bridge || !selectedNodeId) return
        var nodes = bridge.currentNodes || []
        for (var i = 0; i < nodes.length; ++i) {
            if (nodes[i].id === selectedNodeId) {
                selectedNode = nodes[i]
                currentParams = selectedNode.params || ({})
                break
            }
        }
        if (selectedNode) {
            currentMeta = bridge.getOperatorMeta(selectedNode.type)
        }
    }

    Connections {
        target: bridge
        ignoreUnknownSignals: true
        function onCurrentNodesChanged() {
            var nodes = bridge.currentNodes || []
            var found = false
            for (var i = 0; i < nodes.length; ++i) {
                if (nodes[i].id === root.selectedNodeId) {
                    found = true
                    root.currentParams = nodes[i].params || ({})
                    break
                }
            }
            if (!found) {
                root.refresh()
            }
        }
    }

    // v2.2.0 D：生成使用示例文本
    function buildUsageExample() {
        if (!currentMeta) return ""
        var ex = "双击'" + (currentMeta.cnName || "算子") + "'添加到画布\n"
        ex += "连接上游算子输出到此算子输入\n"
        if (currentMeta.params && currentMeta.params.length > 0) {
            ex += "主要参数: "
            for (var i = 0; i < Math.min(3, currentMeta.params.length); ++i) {
                if (i > 0) ex += ", "
                ex += currentMeta.params[i].cnName
            }
        }
        return ex
    }

    // v3.2.0：算子参数编辑器增强 — pin 状态对外信号
    signal pinToggled()
    // 父组件通过 property pinned 传入
    property bool pinned: false

    ScrollView {
        anchors.fill: parent
        clip: true
        ScrollBar.vertical.policy: ScrollBar.AsNeeded

        ColumnLayout {
            width: root.width
            spacing: 0

            // ============ 1. 功能说明区 ============
            // 未选中提示
            ColumnLayout {
                Layout.fillWidth: true
                Layout.margins: 16
                spacing: 8
                visible: !root.selectedNode

                Item { Layout.preferredHeight: 60 }
                Label {
                    text: "算子详情"
                    color: Tok.DesignTokens.accentPrimary
                    font.bold: true
                    font.pixelSize: Tok.DesignTokens.fontSizeXl
                    font.family: Tok.DesignTokens.fontFamilyCJK
                    Layout.alignment: Qt.AlignHCenter
                }
                Label {
                    text: "请先在画布上选中一个节点"
                    color: Tok.DesignTokens.textTertiary
                    font.pixelSize: Tok.DesignTokens.fontSizeBase
                    font.family: Tok.DesignTokens.fontFamilyCJK
                    Layout.alignment: Qt.AlignHCenter
                }
                Label {
                    text: "或双击左侧算子库中的算子添加新节点"
                    color: Tok.DesignTokens.textPlaceholder
                    font.pixelSize: Tok.DesignTokens.fontSizeXs
                    font.family: Tok.DesignTokens.fontFamilyCJK
                    Layout.alignment: Qt.AlignHCenter
                }
                Item { Layout.preferredHeight: 60 }
            }

            // 选中节点时
            ColumnLayout {
                Layout.fillWidth: true
                visible: root.selectedNode !== null
                spacing: 0

                // 1a. 标题头
                Rectangle {
                    Layout.fillWidth: true
                    height: 52
                    color: Tok.DesignTokens.bgHeader
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        spacing: 8

                        // 分类颜色标识
                        Rectangle {
                            width: 20; height: 20; radius: 3
                            color: Tok.DesignTokens.categoryColor(
                                root.currentMeta ? root.currentMeta.category : "")
                        }

                        ColumnLayout {
                            spacing: 1
                            Layout.fillWidth: true
                            Label {
                                text: root.currentMeta ? root.currentMeta.cnName
                                                      : (root.selectedNode ? root.selectedNode.type : "")
                                color: Tok.DesignTokens.textPrimary
                                font.pixelSize: Tok.DesignTokens.fontSizeLg
                                font.bold: true
                                font.family: Tok.DesignTokens.fontFamilyCJK
                                elide: Text.ElideRight
                            }
                            Label {
                                text: (root.currentMeta ? root.currentMeta.category : "") +
                                      (root.currentMeta && root.currentMeta.subGroup ? " / " + root.currentMeta.subGroup : "") +
                                      " · " + (root.selectedNode ? root.selectedNode.type : "")
                                color: Tok.DesignTokens.textPlaceholder
                                font.pixelSize: Tok.DesignTokens.fontSizeXs
                                font.family: Tok.DesignTokens.fontFamilyCJK
                                elide: Text.ElideRight
                            }
                        }

                        // 收藏按钮
                        ToolButton {
                            text: bridge.isFavorite(root.selectedNode ? root.selectedNode.type : "") ? "★" : "☆"
                            font.pixelSize: 16
                            Layout.preferredWidth: 32
                            Layout.preferredHeight: 32
                            onClicked: {
                                if (root.selectedNode) {
                                    bridge.toggleFavorite(root.selectedNode.type)
                                }
                            }
                        }
                    }
                }

                // 1a-bis：v3.2.0 算子参数编辑器 — 固定/隐藏按钮栏
                Rectangle {
                    Layout.fillWidth: true
                    height: 36
                    color: Tok.DesignTokens.bgPanel
                    visible: root.selectedNode !== null
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        spacing: 6

                        // 固定按钮（pin / unpin）
                        ToolButton {
                            id: pinBtn
                            text: root.pinned ? "📌 已固定" : "📌 固定"
                            font.pixelSize: 11
                            font.family: Tok.DesignTokens.fontFamilyCJK
                            Layout.preferredHeight: 28
                            Layout.preferredWidth: 84
                            ToolTip.text: root.pinned
                                ? "已固定：编辑器保持显示。点击取消固定。"
                                : "点击固定编辑器，避免被其他操作隐藏。"
                            ToolTip.visible: hovered
                            background: Rectangle {
                                color: root.pinned
                                    ? Tok.DesignTokens.accentPrimary
                                    : (pinBtn.hovered ? Tok.DesignTokens.bgHover : Tok.DesignTokens.bgSurface)
                                border.color: Tok.DesignTokens.borderDefault
                                border.width: 1
                                radius: Tok.DesignTokens.radiusSm
                            }
                            contentItem: Label {
                                text: pinBtn.text
                                color: root.pinned
                                    ? Tok.DesignTokens.textPrimary
                                    : Tok.DesignTokens.textSecondary
                                font: pinBtn.font
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                            onClicked: root.pinToggled()
                        }

                        // 隐藏按钮
                        ToolButton {
                            id: hideBtn
                            text: "◀ 隐藏"
                            font.pixelSize: 11
                            font.family: Tok.DesignTokens.fontFamilyCJK
                            Layout.preferredHeight: 28
                            ToolTip.text: "收起算子参数编辑器到侧边栏"
                            ToolTip.visible: hovered
                            background: Rectangle {
                                color: hideBtn.hovered ? Tok.DesignTokens.bgHover : Tok.DesignTokens.bgSurface
                                border.color: Tok.DesignTokens.borderDefault
                                border.width: 1
                                radius: Tok.DesignTokens.radiusSm
                            }
                            contentItem: Label {
                                text: hideBtn.text
                                color: Tok.DesignTokens.textSecondary
                                font: hideBtn.font
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                            onClicked: root.requestHide ? root.requestHide() : null
                        }

                        Item { Layout.fillWidth: true }

                        // 当前固定状态指示
                        Label {
                            visible: root.pinned
                            text: "🔒 锁定显示"
                            color: Tok.DesignTokens.accentSuccess
                            font.pixelSize: 10
                            font.family: Tok.DesignTokens.fontFamilyCJK
                        }
                    }
                }

                // 1b. 功能描述
                Rectangle {
                    Layout.fillWidth: true
                    Layout.margins: 10
                    color: Tok.DesignTokens.bgSurface
                    radius: Tok.DesignTokens.radiusMd
                    height: descText.implicitHeight + 20

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 6

                        Label {
                            text: "功能说明"
                            color: Tok.DesignTokens.accentPrimary
                            font.bold: true
                            font.pixelSize: Tok.DesignTokens.fontSizeSm
                            font.family: Tok.DesignTokens.fontFamilyCJK
                        }
                        Label {
                            id: descText
                            text: root.currentMeta ? root.currentMeta.description : "暂无描述"
                            color: Tok.DesignTokens.textSecondary
                            font.pixelSize: Tok.DesignTokens.fontSizeSm
                            font.family: Tok.DesignTokens.fontFamilyCJK
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                    }
                }

                // 分隔线
                Rectangle { Layout.fillWidth: true; height: 1; color: Tok.DesignTokens.borderDefault }

                // ============ 2. 输入参数区 ============
                Label {
                    text: "输入参数"
                    color: Tok.DesignTokens.accentWarning
                    font.bold: true
                    font.pixelSize: Tok.DesignTokens.fontSizeSm
                    font.family: Tok.DesignTokens.fontFamilyCJK
                    Layout.margins: 10
                    Layout.topMargin: 10
                }

                ParamForm {
                    id: paramForm
                    Layout.fillWidth: true
                    Layout.leftMargin: 10
                    Layout.rightMargin: 10
                    params: root.currentMeta ? root.currentMeta.params : []
                    currentValues: root.currentParams
                    onValuesChanged: function(newValues) {
                        if (root.bridge && root.selectedNodeId) {
                            root.bridge.updateOperatorParams(root.selectedNodeId, newValues)
                        }
                    }
                    onValidationError: function(name, message) {
                        console.warn("[ParamForm] 校验失败:", name, message)
                    }
                }

                // 分隔线
                Rectangle { Layout.fillWidth: true; height: 1; color: Tok.DesignTokens.borderDefault; Layout.topMargin: 6 }

                // ============ 3. 输出参数区（v3.0.0 动态渲染）============
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.margins: 10
                    spacing: 6

                    Label {
                        text: "输出参数"
                        color: Tok.DesignTokens.accentSuccess
                        font.bold: true
                        font.pixelSize: Tok.DesignTokens.fontSizeSm
                        font.family: Tok.DesignTokens.fontFamilyCJK
                    }

                    // 动态渲染输出参数列表（从 currentMeta.outputs）
                    Repeater {
                        model: {
                            if (!root.currentMeta || !root.currentMeta.outputs
                                || root.currentMeta.outputs.length === 0)
                                return []
                            return root.currentMeta.outputs
                        }
                        delegate: RowLayout {
                            spacing: 8
                            Rectangle {
                                width: 12; height: 12; radius: 2
                                color: modelData.color || Tok.DesignTokens.accentSuccess
                            }
                            Label {
                                text: (modelData.cnName || modelData.name || "output")
                                      + (modelData.typeName ? " (" + modelData.typeName + ")" : "")
                                color: Tok.DesignTokens.textSecondary
                                font.pixelSize: Tok.DesignTokens.fontSizeXs
                                font.family: Tok.DesignTokens.fontFamilyCJK
                            }
                            Label {
                                text: modelData.desc || ""
                                color: Tok.DesignTokens.textPlaceholder
                                font.pixelSize: Tok.DesignTokens.fontSizeXs
                                font.family: Tok.DesignTokens.fontFamilyCJK
                            }
                        }
                    }

                    // 无输出参数时的占位
                    Label {
                        visible: !root.currentMeta || !root.currentMeta.outputs
                                  || root.currentMeta.outputs.length === 0
                        text: "算子执行后将输出处理后的图像和检测结果"
                        color: Tok.DesignTokens.textPlaceholder
                        font.pixelSize: Tok.DesignTokens.fontSizeXs
                        font.family: Tok.DesignTokens.fontFamilyCJK
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                }

                // 分隔线
                Rectangle { Layout.fillWidth: true; height: 1; color: Tok.DesignTokens.borderDefault }

                // ============ 4. 使用示例区 ============
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.margins: 10
                    spacing: 6

                    Label {
                        text: "使用示例"
                        color: Tok.DesignTokens.accentInfo
                        font.bold: true
                        font.pixelSize: Tok.DesignTokens.fontSizeSm
                        font.family: Tok.DesignTokens.fontFamilyCJK
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        height: usageText.implicitHeight + 20
                        color: Tok.DesignTokens.bgPanel
                        radius: Tok.DesignTokens.radiusMd
                        Label {
                            id: usageText
                            anchors.fill: parent
                            anchors.margins: 10
                            text: root.buildUsageExample()
                            color: Tok.DesignTokens.textTertiary
                            font.pixelSize: Tok.DesignTokens.fontSizeXs
                            font.family: Tok.DesignTokens.fontMono
                            wrapMode: Text.WordWrap
                        }
                    }
                }

                // 分隔线
                Rectangle { Layout.fillWidth: true; height: 1; color: Tok.DesignTokens.borderDefault }

                // ============ 5. 效果预览区（v3.1.0 升级：双图对比 + 浮动触发）============
                Rectangle {
                    Layout.fillWidth: true
                    height: 1
                    color: Tok.DesignTokens.borderDefault
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.margins: 10
                    spacing: 6

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 4

                        Label {
                            text: "效果预览"
                            color: Tok.DesignTokens.accentError
                            font.bold: true
                            font.pixelSize: Tok.DesignTokens.fontSizeSm
                            font.family: Tok.DesignTokens.fontFamilyCJK
                        }
                        Item { Layout.fillWidth: true }
                        // 浮动/停靠切换按钮
                        ToolButton {
                            text: "↗"
                            font.pixelSize: 12
                            Layout.preferredWidth: 24
                            Layout.preferredHeight: 24
                            ToolTip.text: "浮动面板"
                            ToolTip.visible: hovered
                            background: Rectangle {
                                color: parent.hovered ? Tok.DesignTokens.bgHover : "transparent"
                                radius: Tok.DesignTokens.radiusSm
                            }
                            onClicked: root.requestFloat()
                        }
                    }

                    // 双图并排预览（对照 drawio: 原图 80x88 + 滤波后 80x88）
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        // 原图
                        ColumnLayout {
                            spacing: 3
                            Layout.alignment: Qt.AlignCenter

                            Rectangle {
                                Layout.preferredWidth: 80
                                Layout.preferredHeight: 88
                                color: "#2c3e50"
                                border.color: "#3a3a4e"
                                border.width: 1
                                radius: Tok.DesignTokens.radiusSm

                                Image {
                                    id: previewOriginalImg
                                    anchors.fill: parent
                                    anchors.margins: 1
                                    fillMode: Image.PreserveAspectFit
                                    source: root.previewSourceImage || ""
                                    cache: false
                                }

                                Label {
                                    anchors.centerIn: parent
                                    text: "🖼"
                                    font.pixelSize: 18
                                    color: "#555"
                                    visible: root.previewSourceImage === ""
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    ToolTip.text: "双击打开独立预览窗口"
                                    ToolTip.visible: containsMouse
                                    hoverEnabled: true
                                    onDoubleClicked: {
                                        root.previewDoubleClicked(
                                            root.previewSourceImage,
                                            root.previewProcessedImage,
                                            (root.currentMeta ? root.currentMeta.cnName : "") + " 效果预览"
                                        )
                                    }
                                }
                            }

                            Label {
                                text: "原图"
                                color: Tok.DesignTokens.textSecondary
                                font.pixelSize: 9
                                font.family: Tok.DesignTokens.fontFamilyCJK
                                horizontalAlignment: Text.AlignHCenter
                                Layout.alignment: Qt.AlignHCenter
                            }
                        }

                        // 处理后
                        ColumnLayout {
                            spacing: 3
                            Layout.alignment: Qt.AlignCenter

                            Rectangle {
                                Layout.preferredWidth: 80
                                Layout.preferredHeight: 88
                                color: "#34495e"
                                border.color: "#3a3a4e"
                                border.width: 1
                                radius: Tok.DesignTokens.radiusSm

                                Image {
                                    id: previewProcessedImg
                                    anchors.fill: parent
                                    anchors.margins: 1
                                    fillMode: Image.PreserveAspectFit
                                    source: root.previewProcessedImage || ""
                                    cache: false
                                }

                                Label {
                                    anchors.centerIn: parent
                                    text: "✨"
                                    font.pixelSize: 18
                                    color: "#555"
                                    visible: root.previewProcessedImage === ""
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    ToolTip.text: "双击打开独立预览窗口"
                                    ToolTip.visible: containsMouse
                                    hoverEnabled: true
                                    onDoubleClicked: {
                                        root.previewDoubleClicked(
                                            root.previewSourceImage,
                                            root.previewProcessedImage,
                                            (root.currentMeta ? root.currentMeta.cnName : "") + " 效果预览"
                                        )
                                    }
                                }
                            }

                            Label {
                                text: "滤波后"
                                color: Tok.DesignTokens.textSecondary
                                font.pixelSize: 9
                                font.family: Tok.DesignTokens.fontFamilyCJK
                                horizontalAlignment: Text.AlignHCenter
                                Layout.alignment: Qt.AlignHCenter
                            }
                        }
                    }

                    // 提示文字
                    Label {
                        text: "双击效果预览图，可以打开独立窗口"
                        color: Tok.DesignTokens.textDisabled
                        font.pixelSize: Tok.DesignTokens.fontSizeXxs || 10
                        font.family: Tok.DesignTokens.fontFamilyCJK
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                    }
                }

                // 分隔线
                Rectangle { Layout.fillWidth: true; height: 1; color: Tok.DesignTokens.borderDefault }

                // ============ 6. 底部操作栏 ============
                RowLayout {
                    Layout.fillWidth: true
                    Layout.margins: 10
                    Layout.bottomMargin: 16
                    spacing: 6

                    Button {
                        text: "重置默认"
                        flat: true
                        onClicked: {
                            if (!root.bridge || !root.selectedNode || !root.currentMeta) return
                            var defaults = {}
                            var plist = root.currentMeta.params
                            for (var i = 0; i < plist.length; ++i) {
                                defaults[plist[i].name] = plist[i].defaultValue
                            }
                            root.bridge.updateOperatorParams(root.selectedNodeId, defaults)
                        }
                    }
                    Item { Layout.fillWidth: true }
                    Button {
                        text: "应用"
                        highlighted: true
                        onClicked: {
                            // 参数已通过 onValuesChanged 实时同步，此处提示
                            console.log("[PropertyPreviewPanel] 参数已应用")
                        }
                    }
                    Button {
                        text: "打开编辑器"
                        flat: true
                        onClicked: root.openEditorRequested(root.selectedNodeId)
                    }
                }
            }
        }
    }
}