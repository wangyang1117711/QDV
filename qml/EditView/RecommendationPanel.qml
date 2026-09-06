// =====================================================================
// RecommendationPanel.qml — 智能算子推荐展示区（spec：editor-output-connection-optimization，Task 10）
// ----------------------------------------------------------------------------
// 功能：
//   1. 选中节点时展示该节点的 Top3 推荐算子（recommender.recommend），
//      每项显示算子描述、置信度（百分比）、中文 reason，按置信度降序。
//   2. 一键添加：点击推荐项 → addOperator 在选中节点右侧创建新节点，
//      并自动 connectNodes(选中节点 output → 新节点 input)（可撤销）。
//   3. 权重调整：三个滑杆（typeCompat / structure / frequency）实时调用
//      recommender.setWeight 并刷新推荐（持久化到 config/recommender.json）。
//   4. "最近使用" tab：调用 recommender.recommendRecent 展示最近使用算子。
//
// 定位方式：作为 canvasFrame 的子项，anchors 定位于画布右上角（常驻面板，非 Popup）。
// =====================================================================

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QDV.EditView 3.0 as Tok

Rectangle {
    id: root

    // ============ 外部属性 ============
    property var    bridge: null            // EditViewBridge
    property string selectedNodeId: ""      // 当前选中的单个节点 id（对应 Main.qml 的 currentSelectedNodeId）
    property string activeTab: "recommend"  // "recommend" | "recent"

    // ============ 内部状态 ============
    property string selectedNodeType: ""    // 选中节点的算子 type（用于最近使用 tab）
    property string hintText: "选中算子以查看推荐"
    property bool   collapsed: false        // 折叠为标题栏

    width: 272
    height: collapsed ? 40 : 400

    color: Tok.DesignTokens.bgPanel
    border.color: Tok.DesignTokens.borderDefault
    border.width: Tok.DesignTokens.borderWidth
    radius: Tok.DesignTokens.radiusLg
    z: 2000   // 高于画布平移/框选区域（z:1000），保证可点击

    // ============ 数据模型 ============
    ListModel {
        id: recModel
    }

    // 从 currentNodes 中查找选中节点的算子 type
    function findSelectedType() {
        var nodes = root.bridge ? root.bridge.currentNodes : []
        for (var i = 0; i < nodes.length; ++i) {
            if (nodes[i].id === root.selectedNodeId) return nodes[i].type
        }
        return ""
    }

    // 刷新推荐列表
    function refresh() {
        if (!root.bridge) return
        // P0-1（0906 优化）：移除逐推荐条目的 [RecDiag] 诊断输出（logDiag 同步字符串
        // 拼接 + qDebug 在推荐刷新热路径上，每次刷新 5~15 条）
        root.selectedNodeType = root.findSelectedType()
        recModel.clear()

        if (root.selectedNodeId === "") {
            root.hintText = "选中算子以查看推荐"
            return
        }

        var recs = []
        var recEngine = root.bridge.recommender      // Q_PROPERTY 属性访问（同名 Q_INVOKABLE 方法被 Q_PROPERTY 遮蔽，QML 中应作为属性使用）
        if (!recEngine) {
            root.hintText = "推荐引擎不可用"
            return
        }

        if (root.activeTab === "recent") {
            if (root.selectedNodeType === "") {
                root.hintText = "选中算子以查看最近使用"
                return
            }
            recs = recEngine.recommendRecent(root.selectedNodeType)
        } else {
            recs = recEngine.recommend(root.selectedNodeId)
        }

        // 按置信度降序
        try {
            recs.sort(function(a, b) { return b.confidence - a.confidence })
        } catch (e) {
            // 排序失败不阻断展示
        }

        for (var j = 0; j < recs.length; ++j) {
            var r = recs[j]
            var d = ""
            try { d = root.bridge.getShortDesc(r.type) } catch (e) { d = "" }
            recModel.append({
                type: r.type,
                confidence: r.confidence,
                reason: r.reason !== undefined ? r.reason : "",
                desc: d
            })
        }
        root.hintText = recs.length === 0 ? "暂无推荐，试试调整权重" : ""
    }

    // 一键添加：在选中节点右侧创建新节点并自动建连
    function addRecommended(type) {
        if (root.selectedNodeId === "") return
        var nodes = root.bridge.currentNodes
        var sel = null
        for (var i = 0; i < nodes.length; ++i) {
            if (nodes[i].id === root.selectedNodeId) { sel = nodes[i]; break }
        }
        if (!sel) return

        var gap = Tok.DesignTokens.nodeCardWidth + Tok.DesignTokens.space4
        var newId = root.bridge.addOperator(type, sel.x + gap, sel.y)
        if (newId) {
            // 建连：选中节点 output → 新节点 input（可撤销）
            root.bridge.connectNodes(root.selectedNodeId, "output", newId, "input")
        }
    }

    // 权重调整入口
    function setWeight(key, v) {
        if (!root.bridge) return
        var recEngine = root.bridge.recommender
        if (recEngine) {
            recEngine.setWeight(key, v)
            root.refresh()
        }
    }

    onSelectedNodeIdChanged: root.refresh()
    onActiveTabChanged: root.refresh()
    onBridgeChanged: if (root.bridge) root.refresh()
    Component.onCompleted: root.refresh()

    // 画布节点变化（新增/删除/移动/建连）后自动刷新推荐
    // P0-2（0906 优化）：参数修改走粒度信号 nodeParamsChanged（不影响推荐依据的
    // 连线/类型结构），推荐结果只依赖连线与节点类型 —— 参数变化不再触发全量
    // 推荐重算（每次 refresh 遍历全部连线×算子类型，无缓存）
    Connections {
        target: root.bridge
        function onCurrentNodesChanged() { root.refresh() }
        function onConnectionsChanged() { root.refresh() }
    }
    // 参数粒度信号：推荐不依赖参数值，跳过 refresh（保持推荐结果不变）

    // ============ 标题栏 ============
    Rectangle {
        id: headerBar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 40
        color: Tok.DesignTokens.bgHeader
        radius: Tok.DesignTokens.radiusLg
        // 底部圆角切除
        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: Tok.DesignTokens.radiusLg
            color: Tok.DesignTokens.bgHeader
        }

        RowLayout {
            anchors.left: parent.left
            anchors.leftMargin: Tok.DesignTokens.space2
            anchors.verticalCenter: parent.verticalCenter
            spacing: Tok.DesignTokens.space2

            Label {
                text: "🔍 智能推荐"
                color: Tok.DesignTokens.textPrimary
                font.pixelSize: Tok.DesignTokens.fontSizeSm
                font.bold: true
                font.family: Tok.DesignTokens.fontFamilyCJK
            }
        }

        // 折叠/展开按钮
        ToolButton {
            id: collapseBtn
            anchors.right: parent.right
            anchors.rightMargin: Tok.DesignTokens.space2
            anchors.verticalCenter: parent.verticalCenter
            text: root.collapsed ? "▣" : "▢"
            font.pixelSize: 12
            implicitWidth: 24
            implicitHeight: 24
            ToolTip.text: root.collapsed ? "展开推荐面板" : "折叠推荐面板"
            ToolTip.visible: hovered
            background: Rectangle {
                color: collapseBtn.hovered ? Tok.DesignTokens.bgHover : "transparent"
                radius: Tok.DesignTokens.radiusSm
            }
            onClicked: root.collapsed = !root.collapsed
        }
    }

    // ============ 内容区（折叠时隐藏）============
    ColumnLayout {
        anchors.top: headerBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: Tok.DesignTokens.space2
        spacing: Tok.DesignTokens.space2
        visible: !root.collapsed

        // ---- Tab 切换：推荐 / 最近使用 ----
        RowLayout {
            Layout.fillWidth: true
            spacing: Tok.DesignTokens.space2

            Repeater {
                model: [
                    { key: "recommend", label: "推荐" },
                    { key: "recent",    label: "最近使用" }
                ]
                delegate: Rectangle {
                    Layout.fillWidth: true
                    height: Tok.DesignTokens.controlHeight
                    radius: Tok.DesignTokens.radiusMd
                    color: root.activeTab === modelData.key
                           ? Tok.DesignTokens.accentPrimary
                           : Tok.DesignTokens.bgSurface
                    border.color: root.activeTab === modelData.key
                                  ? Tok.DesignTokens.accentPrimary
                                  : Tok.DesignTokens.borderDefault
                    border.width: Tok.DesignTokens.borderWidth
                    Text {
                        anchors.centerIn: parent
                        text: modelData.label
                        color: root.activeTab === modelData.key
                               ? "#FFFFFF" : Tok.DesignTokens.textSecondary
                        font.pixelSize: Tok.DesignTokens.fontSizeXs
                        font.family: Tok.DesignTokens.fontFamilyCJK
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.activeTab = modelData.key
                    }
                }
            }
        }

        // ---- 提示信息（无选中节点 / 无推荐）----
        Label {
            Layout.fillWidth: true
            Layout.preferredHeight: 24
            visible: root.hintText !== ""
            text: root.hintText
            color: Tok.DesignTokens.textDisabled
            font.pixelSize: Tok.DesignTokens.fontSizeXs
            font.family: Tok.DesignTokens.fontFamilyCJK
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }

        // ---- 推荐列表 ----
        Rectangle {
            id: listContainer
            Layout.fillWidth: true
            Layout.preferredHeight: 190
            radius: Tok.DesignTokens.radiusMd
            color: Tok.DesignTokens.bgSurface
            clip: true

            ListView {
                id: recList
                anchors.fill: parent
                anchors.margins: 1
                model: recModel
                spacing: Tok.DesignTokens.space1
                ScrollBar.vertical: ScrollBar {}
                delegate: Rectangle {
                    id: itemWrap
                    width: recList.width
                    height: 58
                    radius: Tok.DesignTokens.radiusSm
                    color: itemArea.containsMouse
                           ? Tok.DesignTokens.bgHover
                           : "transparent"
                    border.color: Tok.DesignTokens.borderDefault
                    border.width: itemArea.containsMouse ? Tok.DesignTokens.borderWidth : 0

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: Tok.DesignTokens.space2
                        spacing: 2

                        // 第一行：描述 + 置信度
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Tok.DesignTokens.space2

                            Label {
                                Layout.fillWidth: true
                                text: model.desc !== "" ? model.desc : model.type
                                color: Tok.DesignTokens.textPrimary
                                font.pixelSize: Tok.DesignTokens.fontSizeSm
                                font.family: Tok.DesignTokens.fontFamilyCJK
                                elide: Text.ElideRight
                            }
                            Label {
                                text: Math.round(model.confidence * 100) + "%"
                                color: Tok.DesignTokens.accentSuccess
                                font.pixelSize: Tok.DesignTokens.fontSizeXs
                                font.bold: true
                            }
                        }

                        // 第二行：reason 说明
                        Label {
                            Layout.fillWidth: true
                            text: model.reason
                            color: Tok.DesignTokens.textSecondary
                            font.pixelSize: Tok.DesignTokens.fontSizeXs
                            font.family: Tok.DesignTokens.fontFamilyCJK
                            elide: Text.ElideRight
                            wrapMode: Text.WrapAnywhere
                            maximumLineCount: 1
                        }
                    }

                    MouseArea {
                        id: itemArea
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        hoverEnabled: true
                        onClicked: root.addRecommended(model.type)
                    }
                }
            }
        }

        // ---- 权重调整 ----
        Rectangle {
            id: weightSection
            Layout.fillWidth: true
            Layout.preferredHeight: weightCol.implicitHeight + Tok.DesignTokens.space3
            radius: Tok.DesignTokens.radiusMd
            color: Tok.DesignTokens.bgSurface

            ColumnLayout {
                id: weightCol
                anchors.fill: parent
                anchors.margins: Tok.DesignTokens.space2
                spacing: 2

                Label {
                    text: "权重调整"
                    color: Tok.DesignTokens.textSecondary
                    font.pixelSize: Tok.DesignTokens.fontSizeXs
                    font.bold: true
                    font.family: Tok.DesignTokens.fontFamilyCJK
                }

                Repeater {
                    model: [
                        { key: "typeCompat", label: "类型兼容" },
                        { key: "structure",  label: "方案结构" },
                        { key: "frequency",  label: "使用频率" }
                    ]
                    delegate: RowLayout {
                        Layout.fillWidth: true
                        spacing: Tok.DesignTokens.space2

                        Label {
                            Layout.preferredWidth: 64
                            text: modelData.label
                            color: Tok.DesignTokens.textSecondary
                            font.pixelSize: Tok.DesignTokens.fontSizeXxs
                            font.family: Tok.DesignTokens.fontFamilyCJK
                        }
                        Slider {
                            Layout.fillWidth: true
                            id: wSlider
                            from: 0.0
                            to: 1.0
                            stepSize: 0.05
                            // 初始值从引擎读取（持久化值），仅设置一次，避免与用户拖动冲突
                            Component.onCompleted: {
                                if (root.bridge) {
                                    var re = root.bridge.recommender
                                    if (re) value = re.getWeight(modelData.key)
                                }
                            }
                            onMoved: root.setWeight(modelData.key, value)
                        }
                        Label {
                            Layout.preferredWidth: 34
                            text: (Math.round(wSlider.value * 100)) + "%"
                            color: Tok.DesignTokens.textSecondary
                            font.pixelSize: Tok.DesignTokens.fontSizeXxs
                            horizontalAlignment: Text.AlignRight
                        }
                    }
                }

                Label {
                    text: "改动实时生效并持久化"
                    color: Tok.DesignTokens.textDisabled
                    font.pixelSize: Tok.DesignTokens.fontSizeXxs
                    font.family: Tok.DesignTokens.fontFamilyCJK
                }
            }
        }
    }
}