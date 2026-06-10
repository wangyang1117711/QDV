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
    property bool   rightPanelVisible: true
    property bool   leftPanelVisible: true
    property bool   rightPanelFloating: false   // v3.1.0: 右侧面板浮动模式
    property url    previewSourceImage: ""       // v3.1.0: 原图路径
    property url    previewProcessedImage: ""    // v3.1.0: 处理后图像路径
    property var    analysisResults: []          // v3.1.0: 分析结果数据

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
        var comp = Qt.createComponent("qrc:/qml/EditView/OperatorEditorDialog.qml")
        if (comp.status === Component.Ready) {
            comp.createObject(root, {
                bridge: editViewBridge,
                nodeId: nodeId
            }).open()
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
        selectedNodeIds = [nodeId]
        if (isNarrow || isMedium) {
            rightPanelVisible = true  // 自动展开右侧面板
        }
    }

    // v3.1.0: 打开图像预览浮窗
    function openImagePreview(sourceUrl, processedUrl, title) {
        previewWindow.sourceImage = sourceUrl
        previewWindow.processedImage = processedUrl
        previewWindow.imageTitle = title || "效果预览"
        previewWindow.open()
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

    // ============ 文件对话框 ============
    FileDialog {
        id: saveFileDialog
        title: "保存方案"
        fileMode: FileDialog.SaveFile
        nameFilters: ["JSON 方案 (*.json)"]
        defaultSuffix: "json"
        onAccepted: {
            var path = String(selectedFile)
            if (path.startsWith("file:///")) path = path.substring(8)
            else if (path.startsWith("file://")) path = path.substring(7)
            editViewBridge.saveToFile(path)
        }
    }
    FileDialog {
        id: loadFileDialog
        title: "加载方案"
        fileMode: FileDialog.OpenFile
        nameFilters: ["JSON 方案 (*.json)"]
        onAccepted: {
            var path = String(selectedFile)
            if (path.startsWith("file:///")) path = path.substring(8)
            else if (path.startsWith("file://")) path = path.substring(7)
            editViewBridge.loadFromFile(path)
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
                anchors.margins: Tok.DesignTokens.space1
                spacing: Tok.DesignTokens.space1

                // 品牌标题
                Label {
                    text: "QDV · " + (editViewBridge.currentSchemeName || "未命名方案")
                           + (editViewBridge.isDirty ? " *" : "")
                    color: Tok.DesignTokens.textPrimary
                    // v3.0.0：方案标题 Base 13→Lg 14
                    font.pixelSize: Tok.DesignTokens.fontSizeLg
                    font.bold: true
                    font.family: Tok.DesignTokens.fontFamilyCJK
                    Layout.leftMargin: Tok.DesignTokens.space2
                    Layout.minimumWidth: 200
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
                }
                ToolButton {
                    text: "\u21AA"  // ↪
                    ToolTip.text: "重做 (Ctrl+Y)"
                    ToolTip.visible: hovered
                    enabled: editViewBridge.canRedo
                    onClicked: editViewBridge.redo()
                }
                ToolSeparator {}

                // 缩放控件
                ToolButton {
                    text: "\u2212"  // −
                    ToolTip.text: "缩小"
                    ToolTip.visible: hovered
                    onClicked: { root.canvasZoom = Math.max(0.25, root.canvasZoom - 0.1) }
                }
                Label {
                    text: Math.round(root.canvasZoom * 100) + "%"
                    color: Tok.DesignTokens.textSecondary
                    font.pixelSize: Tok.DesignTokens.fontSizeBase
                    font.family: Tok.DesignTokens.fontFamilyCJK
                    horizontalAlignment: Text.AlignHCenter
                    Layout.preferredWidth: 45
                }
                ToolButton {
                    text: "+"
                    ToolTip.text: "放大"
                    ToolTip.visible: hovered
                    onClicked: { root.canvasZoom = Math.min(3.0, root.canvasZoom + 0.1) }
                }
                ToolButton {
                    text: "\u229E"  // ⊞
                    ToolTip.text: "适应画布"
                    ToolTip.visible: hovered
                    onClicked: { root.canvasZoom = 1.0 }
                }
                ToolSeparator {}

                // 左右面板切换（v3.0.0）
                ToolButton {
                    text: leftPanelVisible ? "\u25C0" : "\u25B6"  // ◀/▶
                    ToolTip.text: leftPanelVisible ? "隐藏算子库" : "显示算子库"
                    ToolTip.visible: hovered
                    onClicked: leftPanelVisible = !leftPanelVisible
                }
                ToolButton {
                    text: rightPanelVisible ? "\u25B6" : "\u25C0"  // ▶/◀
                    ToolTip.text: rightPanelVisible ? "隐藏详情" : "显示详情"
                    ToolTip.visible: hovered
                    onClicked: rightPanelVisible = !rightPanelVisible
                }
                ToolSeparator {}

                // 算子编辑 + 部署
                ToolButton {
                    text: "\u270E 编辑"  // ✎
                    enabled: root.currentSelectedNodeId !== ""
                    onClicked: {
                        if (root.currentSelectedNodeId)
                            root.openEditorForNode(root.currentSelectedNodeId)
                    }
                }
                ToolButton {
                    text: "\u25B6 部署"  // ▶
                    ToolTip.text: "运行当前方案"
                    ToolTip.visible: hovered
                    onClicked: root.showToast("info", "部署功能将在后续版本实现")
                }
                ToolSeparator {}

                // 文件操作
                ToolButton { text: "新建"; onClicked: editViewBridge.newScheme() }
                ToolButton {
                    text: "保存"
                    enabled: editViewBridge.isDirty
                    onClicked: saveFileDialog.open()
                }
                ToolButton { text: "加载"; onClicked: loadFileDialog.open() }
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
        Shortcut {
            sequences: ["Ctrl+K"]
            onActivated: { searchField.forceActiveFocus(); searchField.selectAll() }
        }
        // v3.0.0：Delete 键删除选中节点
        Shortcut {
            sequences: [StandardKey.Delete]
            enabled: root.currentSelectedNodeId !== ""
            onActivated: root.deleteSelectedNodes()
        }
        // v3.0.0：Escape 取消选择
        Shortcut {
            sequences: [StandardKey.Cancel]
            onActivated: {
                root.currentSelectedNodeId = ""
                root.selectedNodeIds = []
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

                    // 标题
                    Label {
                        text: "算子库"
                        color: Tok.DesignTokens.accentPrimary
                        font.bold: true
                        font.pixelSize: Tok.DesignTokens.fontSizeLg
                        font.family: Tok.DesignTokens.fontFamilyCJK
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
                                Accessible.name: "搜索算子"
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
                                Layout.preferredWidth: (leftPanel.width - Tok.DesignTokens.space8 * 2) / 4
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
                                        // v3.0.1：分类行文字优化——字号 Base 13→Lg 14
                                        // 与算子行同号（算子行已升级到 14），统一视觉层次
                                        text: modelData.category
                                        color: Tok.DesignTokens.textPrimary
                                        font.bold: true
                                        font.pixelSize: Tok.DesignTokens.fontSizeLg
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
                                        // v3.0.0：分类色块 14×14→16×16，更醒目
                                        width: 16; height: 16; radius: 3
                                        color: Tok.DesignTokens.categoryColor(modelData.category)
                                        anchors.verticalCenter: parent.verticalCenter
                                    }
                                    Label {
                                        // v3.0.1：算子行文字渲染优化
                                        // 1) 字号 Base 13→Lg 14 (与节点卡片主标题同号，统一视觉)
                                        // 2) 加粗 + 0.2 字间距 + 1.4 行高，14px 中文不拥挤
                                        // 3) 修正字体名 fontFamilyCJK (原 fontFamilyCJKCJK 拼写错误)
                                        // 4) textPrimary #FFFFFF (15.3:1 对比度) 高清
                                        // 5) elide 留空增至 76px 容纳更宽字号
                                        text: (modelData.meta && modelData.meta.cnName)
                                            ? modelData.meta.cnName : modelData.category
                                        color: Tok.DesignTokens.textPrimary
                                        font.pixelSize: Tok.DesignTokens.fontSizeLg
                                        font.bold: true
                                        font.family: Tok.DesignTokens.fontFamilyCJK
                                        font.hintingPreference: Font.PreferDefaultHinting
                                        font.letterSpacing: 0.2
                                        lineHeight: Tok.DesignTokens.lineHeightRelax
                                        lineHeightMode: Text.ProportionalHeight
                                        elide: Text.ElideRight
                                        wrapMode: Text.NoWrap
                                        anchors.verticalCenter: parent.verticalCenter
                                        // v3.0.1：扣除色块 18+spacing 8+星标 20+右内边距 8+留白 4 = 76
                                        width: parent.width - 76
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
                                        font.pixelSize: Tok.DesignTokens.fontSizeLg
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
                                    Accessible.role: Accessible.Button
                                    Accessible.name: modelData.meta ? modelData.meta.cnName : "算子"
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

            // ============ 中央：画布 ============
            Rectangle {
                id: canvasFrame
                color: Tok.DesignTokens.bgCanvas

                // 鼠标滚轮缩放（v3.0.0）
                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.NoButton
                    onWheel: function(wheel) {
                        var delta = wheel.angleDelta.y / 120
                        var newZoom = root.canvasZoom + delta * 0.1
                        root.canvasZoom = Math.max(0.25, Math.min(3.0, newZoom))
                    }
                }

                // 画布网格背景
                Canvas {
                    anchors.fill: parent
                    onPaint: {
                        var ctx = getContext("2d")
                        ctx.clearRect(0, 0, width, height)
                        ctx.strokeStyle = "#1F1F1F"
                        ctx.lineWidth = 0.5
                        var grid = 40 * root.canvasZoom
                        for (var x = 0; x < width; x += grid) {
                            ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, height); ctx.stroke()
                        }
                        for (var y = 0; y < height; y += grid) {
                            ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(width, y); ctx.stroke()
                        }
                    }
                    Connections {
                        target: root
                        function onCanvasZoomChanged() { parent.requestPaint() }
                    }
                }

                // 连线层
                Repeater {
                    model: editViewBridge.connections
                    Canvas {
                        id: connCanvas
                        anchors.fill: parent
                        property var connData: modelData
                        onPaint: {
                            if (!connData) return
                            var ctx = getContext("2d")
                            var fromNode = null, toNode = null
                            var nodes = editViewBridge.currentNodes
                            for (var i = 0; i < nodes.length; ++i) {
                                if (nodes[i].id === connData.fromId) fromNode = nodes[i]
                                if (nodes[i].id === connData.toId) toNode = nodes[i]
                            }
                            if (!fromNode || !toNode) return
                            var fx = fromNode.x + Tok.DesignTokens.nodeCardWidth
                            var fy = fromNode.y + Tok.DesignTokens.nodeCardHeight / 2
                            var tx = toNode.x
                            var ty = toNode.y + Tok.DesignTokens.nodeCardHeight / 2
                            var z = root.canvasZoom

                            ctx.clearRect(0, 0, width, height)
                            ctx.strokeStyle = Tok.DesignTokens.accentPrimary
                            ctx.lineWidth = 2
                            ctx.beginPath()
                            var cx1 = fx + Math.abs(tx - fx) * 0.4
                            var cx2 = tx - Math.abs(tx - fx) * 0.4
                            ctx.moveTo(fx * z, fy * z)
                            ctx.bezierCurveTo(cx1 * z, fy * z, cx2 * z, ty * z, tx * z, ty * z)
                            ctx.stroke()

                            var angle = Math.atan2(ty - fy, tx - fx)
                            var ax = tx * z - 8 * Math.cos(angle)
                            var ay = ty * z - 8 * Math.sin(angle)
                            ctx.fillStyle = Tok.DesignTokens.accentPrimary
                            ctx.beginPath()
                            ctx.moveTo(ax, ay)
                            ctx.lineTo(ax - 6 * Math.cos(angle - 0.5), ay - 6 * Math.sin(angle - 0.5))
                            ctx.lineTo(ax - 6 * Math.cos(angle + 0.5), ay - 6 * Math.sin(angle + 0.5))
                            ctx.closePath()
                            ctx.fill()
                        }
                    }
                }

                // 节点卡片层
                Repeater {
                    model: editViewBridge.currentNodes
                    delegate: Rectangle {
                        id: nodeCard
                        property var nodeData: modelData
                        property bool isSelected: selectedNodeIds.indexOf(nodeData.id) >= 0
                        x: nodeData.x * root.canvasZoom
                        y: nodeData.y * root.canvasZoom
                        width: Tok.DesignTokens.nodeCardWidth * root.canvasZoom
                        height: Tok.DesignTokens.nodeCardHeight * root.canvasZoom
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

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: Tok.DesignTokens.space1 * root.canvasZoom
                            spacing: 1 * root.canvasZoom

                            Label {
                                // v3.0.0：节点卡片中文名渲染优化
                                // 1) 字号 Sm 12→Lg 14，去掉 zoom 乘数（在 100% 缩放下足够清晰）
                                // 2) 行高 1.3 让中文在 14px 下不显得拥挤
                                // 3) CJK 字体回退 + 0.2 字间距
                                // 4) 使用 textPrimary #FFFFFF (15.3:1 对比度) + 加粗
                                text: {
                                    var meta = editViewBridge.getOperatorMeta(nodeData.type)
                                    return (meta && meta.cnName) || nodeData.type
                                }
                                color: Tok.DesignTokens.textPrimary
                                font.pixelSize: Tok.DesignTokens.fontSizeLg
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
                                // v3.0.0：节点卡片类型副标题渲染优化
                                // 1) 字号 Xs 11→Base 13
                                // 2) CJK 字体回退
                                // 3) textTertiary #9E9E9E 在 bgSurface #242424 上 5.8:1（满足 AA）
                                text: nodeData.type
                                color: Tok.DesignTokens.textSecondary
                                font.pixelSize: Tok.DesignTokens.fontSizeBase
                                font.family: Tok.DesignTokens.fontMono
                                font.letterSpacing: 0.1
                                elide: Text.ElideRight
                                wrapMode: Text.NoWrap
                                Layout.fillWidth: true
                            }
                        }

                        // 输入端口（左）
                        Rectangle {
                            x: -Tok.DesignTokens.portSize / 2 * root.canvasZoom
                            y: parent.height / 2 - Tok.DesignTokens.portSize / 2 * root.canvasZoom
                            width: Tok.DesignTokens.portSize * root.canvasZoom
                            height: Tok.DesignTokens.portSize * root.canvasZoom
                            radius: Tok.DesignTokens.portSize / 2 * root.canvasZoom
                            color: Tok.DesignTokens.accentWarning
                            border.color: Tok.DesignTokens.bgCanvas
                            border.width: 1
                        }
                        // 输出端口（右）
                        Rectangle {
                            x: parent.width - Tok.DesignTokens.portSize / 2 * root.canvasZoom
                            y: parent.height / 2 - Tok.DesignTokens.portSize / 2 * root.canvasZoom
                            width: Tok.DesignTokens.portSize * root.canvasZoom
                            height: Tok.DesignTokens.portSize * root.canvasZoom
                            radius: Tok.DesignTokens.portSize / 2 * root.canvasZoom
                            color: Tok.DesignTokens.accentSuccess
                            border.color: Tok.DesignTokens.bgCanvas
                            border.width: 1
                        }

                        // 节点交互
                        MouseArea {
                            anchors.fill: parent
                            drag.target: nodeCard
                            drag.minimumX: 0
                            drag.minimumY: 0
                            drag.maximumX: canvasFrame.width - nodeCard.width
                            drag.maximumY: canvasFrame.height - nodeCard.height
                            cursorShape: Qt.OpenHandCursor
                            acceptedButtons: Qt.LeftButton | Qt.RightButton

                            onClicked: function(mouse) {
                                if (mouse.button === Qt.RightButton) {
                                    nodeContextMenu.nodeId = nodeData.id
                                    nodeContextMenu.popup()
                                } else {
                                    root.selectNode(nodeData.id)
                                }
                            }
                            onDoubleClicked: root.openEditorForNode(nodeData.id)
                            onReleased: {
                                if (drag.active) {
                                    editViewBridge.moveNode(nodeData.id,
                                        nodeCard.x / root.canvasZoom,
                                        nodeCard.y / root.canvasZoom)
                                }
                            }
                        }
                        Accessible.role: Accessible.Button
                        Accessible.name: {
                            var meta = editViewBridge.getOperatorMeta(nodeData.type)
                            return (meta && meta.cnName) || nodeData.type
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
                        text: "\u2715 删除节点"
                        onTriggered: {
                            editViewBridge.deleteNode(nodeContextMenu.nodeId)
                            selectedNodeIds = selectedNodeIds.filter(
                                function(id) { return id !== nodeContextMenu.nodeId })
                        }
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
                        }
                    }
                    Connections {
                        target: editViewBridge
                        function onCurrentNodesChanged() {
                            minimapCanvas.nodes = editViewBridge.currentNodes
                            minimapCanvas.requestPaint()
                        }
                    }
                }

                // 响应式：窄屏时显示展开右侧面板按钮
                Rectangle {
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.rightMargin: Tok.DesignTokens.space2
                    anchors.topMargin: Tok.DesignTokens.space2
                    width: 28; height: 28; radius: Tok.DesignTokens.radiusMd
                    color: Tok.DesignTokens.bgSurface
                    border.color: Tok.DesignTokens.borderDefault
                    visible: !root.rightPanelVisible && root.currentSelectedNodeId !== ""
                    Label {
                        anchors.centerIn: parent
                        text: "\u25C0"
                        color: Tok.DesignTokens.accentPrimary
                        font.pixelSize: Tok.DesignTokens.fontSizeLg
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.rightPanelVisible = true
                    }
                }
            }

            // ============ 右侧：算子详情（v3.1.0 支持浮动模式）============
            PropertyPreviewPanel {
                id: rightPanel
                bridge: editViewBridge
                selectedNodeId: root.currentSelectedNodeId
                SplitView.preferredWidth: Tok.DesignTokens.rightPanelDefault
                SplitView.minimumWidth: Tok.DesignTokens.rightPanelMin
                visible: root.rightPanelVisible && !root.rightPanelFloating
                onOpenEditorRequested: function(nodeId) {
                    root.openEditorForNode(nodeId)
                }
                // v3.1.0: 双击效果预览区打开独立窗口
                onPreviewDoubleClicked: function(sourceUrl, processedUrl, title) {
                    root.openImagePreview(sourceUrl, processedUrl, title)
                }
                onRequestFloat: root.toggleRightPanelFloating()
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

    // ============ Toast 提示 ============
    Popup {
        id: toastPopup
        x: (parent.width - width) / 2
        y: 60
        width: Math.min(contentText.implicitWidth + Tok.DesignTokens.space8, parent.width - 80)
        height: contentText.implicitHeight + Tok.DesignTokens.space6
        modal: false
        focus: false
        closePolicy: Popup.NoAutoClose
        visible: root.toastVisible
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
}