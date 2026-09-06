// =====================================================================
// QDV VariableManagerPanel.qml（v2.6.0 变量管理）
//
// 功能：
//   1. Tab 切换：图像变量 / 控制变量
//   2. 图像变量 Tab：
//      - 列表显示所有有输出的算子节点（缩略图 + 节点名 + 算子名）
//      - 点击切换 PreviewPanel 的预览节点
//   3. 控制变量 Tab：
//      - 列表显示所有控制变量（名称/类型/值/描述）
//      - CRUD：创建/编辑/删除变量
//      - 类型支持 int/double/string/bool
//      - 实时监控（修改变量值 → 触发预览）
//
// 数据来源：
//   - bridge.imageVariableManager → 图像变量列表
//   - bridge.variableManager → 控制变量列表
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
    property var bridge: null

    // === 信号 ===
    signal imageNodeSelected(string nodeId)
    signal requestHide()

    // === 模式：0=全部（Tab 切换），1=仅控制变量，2=仅图像变量，3=仅算子参数 ===
    property int mode: 0

    // === 当前 Tab ===
    // 0=图像变量, 1=控制变量, 2=算子参数
    property int currentTab: mode === 1 ? 1 : (mode === 3 ? 2 : 0)
    // P0-2（0906 优化）：切到算子参数 Tab 时补一次刷新 —— 隐藏期间参数变更被跳过
    onCurrentTabChanged: {
        if (currentTab === 2) {
            operatorParamList.model = root.buildOperatorParamsList()
        }
    }

    // === 监听变量变更 ===
    Connections {
        target: bridge && bridge.variableManager ? bridge.variableManager : null
        enabled: target !== null

        function onVariablesChanged() {
            controlVarList.model = root.buildControlVarList()
        }
        function onVariableCreated(name) {
            controlVarList.model = root.buildControlVarList()
        }
        function onVariableRemoved(name) {
            controlVarList.model = root.buildControlVarList()
        }
        function onValueChanged(name, newValue) {
            controlVarList.model = root.buildControlVarList()
        }
    }

    // === 监听图像变量变更 ===
    Connections {
        target: bridge && bridge.imageVariableManager ? bridge.imageVariableManager : null
        enabled: target !== null

        function onImageVariablesChanged() {
            imageVarList.model = root.buildImageVarList()
        }
    }

    // === 监听节点变更（v5.3：算子参数自动更新）===
    Connections {
        target: bridge
        enabled: target !== null

        // P0-2（0906 优化）：参数修改发粒度信号 nodeParamsChanged（不再触发画布全量重建）。
        // 本面板的 buildOperatorParamsList 是最重的级联监听者（O(节点×参数) 次 C++ 调用），
        // 且 Main.qml 常驻两个实例 —— 只在算子参数列表可见时才重建，隐藏时跳过。
        function onNodeParamsChanged(nodeId, paramNames) {
            if (operatorParamList.visible) {
                operatorParamList.model = root.buildOperatorParamsList()
            }
        }
        function onCurrentNodesChanged() {
            if (operatorParamList.visible) {
                operatorParamList.model = root.buildOperatorParamsList()
            }
        }
        // v6.x：节点运行完成后刷新算子参数列表（同步运行计算出的输出值）
        function onNodeOutputsUpdated() {
            if (operatorParamList.visible) {
                operatorParamList.model = root.buildOperatorParamsList()
            }
        }
    }

    Component.onCompleted: {
        imageVarList.model = root.buildImageVarList()
        controlVarList.model = root.buildControlVarList()
        operatorParamList.model = root.buildOperatorParamsList()
    }

    // === 构建图像变量列表 ===
    function buildImageVarList() {
        if (!bridge || !bridge.imageVariableManager) return []
        return bridge.imageVariableManager.imageVariables()
    }

    // === 构建控制变量列表 ===
    function buildControlVarList() {
        if (!bridge || !bridge.variableManager) return []
        return bridge.variableManager.variables()
    }

    // === 参数值友好格式化（数组/对象/普通标量）===
    function formatParamValue(v) {
        if (v === undefined || v === null) return ""
        if (Array.isArray(v)) return "[" + v.length + " 项]"
        if (typeof v === "object") return "[对象]"
        var s = String(v)
        return s.length > 20 ? s.substring(0, 18) + "..." : s
    }

    // === 构建算子参数列表（v5.3：列出所有算子节点的参数，按节点分组）===
    // v6.x：统一列出输入参数与输出参数，direction 区分 in/out
    // 返回扁平列表，每项含 {nodeId, nodeType, cnName, paramName, paramCnName,
    //                        paramType, typeText, direction, directionLabel, value}
    // ListView 用 section.property = "nodeId" 实现分组显示
    function buildOperatorParamsList() {
        if (!bridge || !bridge.currentNodes) return []
        var result = []
        var nodes = bridge.currentNodes
        for (var i = 0; i < nodes.length; ++i) {
            var node = nodes[i]
            var meta = bridge.getOperatorMeta(node.type)
            var params = node.params || ({})
            var nodeCnName = meta ? meta.cnName : node.type

            // --- 输入参数 ---
            var paramList = meta ? meta.params : []
            for (var j = 0; j < paramList.length; ++j) {
                var p = paramList[j]
                result.push({
                    nodeId: node.id,
                    nodeType: node.type,
                    cnName: nodeCnName,
                    paramName: p.name,
                    paramCnName: p.cnName || p.name,
                    paramType: p.type,        // 0=Int 1=Float 2=Enum 3=Bool 4=String 5=ROI 6=Vector
                    typeText: root.paramTypeText(p.type),
                    direction: "in",
                    directionLabel: "输入",
                    value: params[p.name] !== undefined ? params[p.name] : p.defaultValue
                })
            }

            // --- 输出参数（v6.x：统一输入输出管理）---
            var outputList = meta ? meta.outputs : []
            var outputConfig = bridge ? bridge.getOutputConfig(node.id) : ({})
            for (var k = 0; k < outputList.length; ++k) {
                var out = outputList[k]
                // 只显示用户勾选过的输出（与 PropertyPreviewPanel.filteredOutputs 保持一致）
                // 未配置时回退到 out.defaultEnabled，默认 true 显示
                var item = outputConfig[out.name]
                var enabled = item ? (item.enabled === true) : (out.defaultEnabled !== false)
                if (!enabled) {
                    continue
                }

                // 尝试获取实际运行结果（优先读桥接层缓存，其次 VariableManager）
                // 算子输出变量名：nodeId.outputName
                var val = ""
                if (bridge) {
                    var nodeOut = bridge.getNodeOutputValues(node.id) || ({})
                    if (nodeOut[out.name] !== undefined) {
                        val = nodeOut[out.name]
                    } else if (bridge.variableManager) {
                        var varName = node.id + "." + out.name
                        if (bridge.variableManager.exists(varName)) {
                            val = bridge.variableManager.value(varName)
                        }
                    }
                }

                result.push({
                    nodeId: node.id,
                    nodeType: node.type,
                    cnName: nodeCnName,
                    paramName: out.name,
                    paramCnName: out.cnName || out.name,
                    paramType: -1,             // 输出参数无输入型编码，typeText 直接用 typeName
                    typeText: out.typeName || "?",
                    direction: "out",
                    directionLabel: "输出",
                    value: val                  // 运行后同步实际值
                })
            }
        }
        return result
    }

    // === 参数类型转中文 ===
    function paramTypeText(type) {
        switch (type) {
            case 0: return "Int"
            case 1: return "Float"
            case 2: return "Enum"
            case 3: return "Bool"
            case 4: return "String"
            case 5: return "ROI"
            case 6: return "Vector"
            default: return "?"
        }
    }

    // === 主布局 ===
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 0
        spacing: 0

        // === 顶部 Tab 栏 ===
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 36
            color: Tok.DesignTokens.bgSurface

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Tok.DesignTokens.space2
                anchors.rightMargin: Tok.DesignTokens.space2
                spacing: Tok.DesignTokens.space2

                // 标题（仅独立模式显示）
                Label {
                    visible: root.mode !== 0
                    text: {
                        if (root.mode === 1) return "控制变量"
                        if (root.mode === 2) return "图像变量"
                        if (root.mode === 3) return "算子参数"
                        return ""
                    }
                    color: Tok.DesignTokens.accentPrimary
                    font.bold: true
                    font.pixelSize: Tok.DesignTokens.fontSizeBase
                    font.family: Tok.DesignTokens.fontFamilyCJK
                    Layout.fillWidth: true
                    verticalAlignment: Text.AlignVCenter
                }

                // Tab: 图像变量
                Rectangle {
                    visible: root.mode === 0
                    Layout.preferredWidth: 80
                    Layout.preferredHeight: 28
                    color: root.currentTab === 0
                        ? Tok.DesignTokens.accentPrimary
                        : Tok.DesignTokens.bgPanel
                    border.color: Tok.DesignTokens.borderDefault
                    border.width: 1
                    radius: Tok.DesignTokens.radiusSm

                    Label {
                        anchors.centerIn: parent
                        text: "图像变量"
                        color: root.currentTab === 0
                            ? Tok.DesignTokens.textPrimary
                            : Tok.DesignTokens.textSecondary
                        font.pixelSize: Tok.DesignTokens.fontSizeSm
                        font.bold: root.currentTab === 0
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.currentTab = 0
                    }
                }

                // Tab: 控制变量
                Rectangle {
                    visible: root.mode === 0
                    Layout.preferredWidth: 80
                    Layout.preferredHeight: 28
                    color: root.currentTab === 1
                        ? Tok.DesignTokens.accentPrimary
                        : Tok.DesignTokens.bgPanel
                    border.color: Tok.DesignTokens.borderDefault
                    border.width: 1
                    radius: Tok.DesignTokens.radiusSm

                    Label {
                        anchors.centerIn: parent
                        text: "控制变量"
                        color: root.currentTab === 1
                            ? Tok.DesignTokens.textPrimary
                            : Tok.DesignTokens.textSecondary
                        font.pixelSize: Tok.DesignTokens.fontSizeSm
                        font.bold: root.currentTab === 1
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.currentTab = 1
                    }
                }

                // Tab: 算子参数（v5.3 新增：列出所有算子节点的参数，自动更新）
                Rectangle {
                    visible: root.mode === 0
                    Layout.preferredWidth: 80
                    Layout.preferredHeight: 28
                    color: root.currentTab === 2
                        ? Tok.DesignTokens.accentPrimary
                        : Tok.DesignTokens.bgPanel
                    border.color: Tok.DesignTokens.borderDefault
                    border.width: 1
                    radius: Tok.DesignTokens.radiusSm

                    Label {
                        anchors.centerIn: parent
                        text: "算子参数"
                        color: root.currentTab === 2
                            ? Tok.DesignTokens.textPrimary
                            : Tok.DesignTokens.textSecondary
                        font.pixelSize: Tok.DesignTokens.fontSizeSm
                        font.bold: root.currentTab === 2
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.currentTab = 2
                    }
                }

                Item { Layout.fillWidth: true; visible: root.mode === 0 }

                // 添加按钮（仅控制变量模式或全部模式下控制变量Tab）
                Button {
                    visible: root.mode === 1 || (root.mode === 0 && root.currentTab === 1)
                    text: "+"
                    Layout.preferredWidth: 28
                    Layout.preferredHeight: 24
                    font.pixelSize: Tok.DesignTokens.fontSizeLg
                    ToolTip.text: "新建控制变量"
                    ToolTip.visible: hovered
                    ToolTip.delay: 500
                    onClicked: varEditDialog.openNew()
                }

                // 算子参数模式下不显示添加按钮，占位保持布局稳定
                Item {
                    visible: root.mode === 3
                    Layout.preferredWidth: 28
                    Layout.preferredHeight: 24
                }

                // 刷新按钮
                Button {
                    text: "↻"
                    Layout.preferredWidth: 28
                    Layout.preferredHeight: 24
                    font.pixelSize: Tok.DesignTokens.fontSizeBase
                    ToolTip.text: "刷新列表"
                    ToolTip.visible: hovered
                    ToolTip.delay: 500
                    onClicked: {
                        imageVarList.model = root.buildImageVarList()
                        controlVarList.model = root.buildControlVarList()
                        operatorParamList.model = root.buildOperatorParamsList()
                    }
                    visible: true
                }

                // 隐藏按钮（仅在全部Tab模式下需要）
                Button {
                    visible: root.mode === 0 || root.mode === 3
                    text: "▼"
                    Layout.preferredWidth: 28
                    Layout.preferredHeight: 24
                    font.pixelSize: Tok.DesignTokens.fontSizeSm
                    ToolTip.text: "隐藏变量管理窗口"
                    ToolTip.visible: hovered
                    ToolTip.delay: 500
                    onClicked: root.requestHide()
                }
            }
        }

        // === 内容区 ===
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Tok.DesignTokens.bgPanel

            // === 图像变量列表（Tab 0）===
            ListView {
                id: imageVarList
                anchors.fill: parent
                anchors.margins: Tok.DesignTokens.space1
                clip: true
                visible: (root.mode === 0 && root.currentTab === 0) || (root.mode === 2)
                spacing: Tok.DesignTokens.space1
                model: []

                delegate: Rectangle {
                    width: imageVarList.width
                    height: 56
                    color: mouseArea.containsMouse
                        ? Tok.DesignTokens.bgSurface
                        : Tok.DesignTokens.bgPanel
                    border.color: Tok.DesignTokens.borderDefault
                    border.width: 1
                    radius: Tok.DesignTokens.radiusSm

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: Tok.DesignTokens.space2
                        spacing: Tok.DesignTokens.space2

                        // 缩略图
                        Rectangle {
                            Layout.preferredWidth: 48
                            Layout.preferredHeight: 48
                            color: Tok.DesignTokens.bgCanvas
                            border.color: Tok.DesignTokens.borderDefault
                            border.width: 1
                            radius: Tok.DesignTokens.radiusSm

                            Image {
                                anchors.fill: parent
                                anchors.margins: 1
                                source: modelData.outputImagePath
                                    ? "file:///" + modelData.outputImagePath.replace(/\\/g, "/")
                                    : ""
                                fillMode: Image.PreserveAspectFit
                                asynchronous: true  // P0-4c：恢复异步解码（QRhi 由渲染层三件套规避）
                                cache: true
                                visible: source !== ""
                            }
                        }

                        // 节点信息
                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            spacing: 0

                            Label {
                                text: modelData.toolName || "Unknown"
                                color: Tok.DesignTokens.accentPrimary
                                font.pixelSize: Tok.DesignTokens.fontSizeSm
                                font.bold: true
                            }
                            Label {
                                text: modelData.nodeId.substring(0, 12) + "..."
                                color: Tok.DesignTokens.textTertiary
                                font.pixelSize: Tok.DesignTokens.fontSizeXs
                            }
                            Label {
                                text: (modelData.width || 0) + "×" + (modelData.height || 0)
                                    + " " + (modelData.channels || 0) + "ch"
                                color: Tok.DesignTokens.textSecondary
                                font.pixelSize: Tok.DesignTokens.fontSizeXs
                            }
                        }

                        // 选择按钮
                        Button {
                            text: "▶"
                            Layout.preferredWidth: 28
                            Layout.preferredHeight: 24
                            font.pixelSize: Tok.DesignTokens.fontSizeSm
                            ToolTip.text: "切换为预览节点"
                            ToolTip.visible: hovered
                            ToolTip.delay: 500
                            onClicked: {
                                root.imageNodeSelected(modelData.nodeId)
                            }
                        }
                    }

                    MouseArea {
                        id: mouseArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onDoubleClicked: root.imageNodeSelected(modelData.nodeId)
                    }
                }

                // 空状态
                Label {
                    anchors.centerIn: parent
                    visible: imageVarList.count === 0
                    text: "暂无图像变量\n执行算子后将自动收集输出图像"
                    color: Tok.DesignTokens.textTertiary
                    font.pixelSize: Tok.DesignTokens.fontSizeBase
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                }
            }

            // === 控制变量列表（Tab 1）===
            ListView {
                id: controlVarList
                anchors.fill: parent
                anchors.margins: Tok.DesignTokens.space1
                clip: true
                visible: (root.mode === 0 && root.currentTab === 1) || (root.mode === 1)
                spacing: Tok.DesignTokens.space1
                model: []

                section.property: ""  // 控制变量不按节点分组

                delegate: Rectangle {
                    width: controlVarList.width
                    height: 36
                    color: varMouseArea.containsMouse
                        ? Tok.DesignTokens.bgSurface
                        : Tok.DesignTokens.bgPanel
                    border.color: Tok.DesignTokens.borderDefault
                    border.width: 1
                    radius: Tok.DesignTokens.radiusSm

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: Tok.DesignTokens.space2
                        anchors.rightMargin: Tok.DesignTokens.space2
                        spacing: Tok.DesignTokens.space2

                        // 变量名
                        Label {
                            text: modelData.name
                            color: Tok.DesignTokens.accentPrimary
                            font.pixelSize: Tok.DesignTokens.fontSizeSm
                            font.bold: true
                            Layout.preferredWidth: 100
                        }

                        // 类型
                        Rectangle {
                            Layout.preferredWidth: 56
                            Layout.preferredHeight: 22
                            color: Tok.DesignTokens.bgCanvas
                            border.color: Tok.DesignTokens.borderDefault
                            border.width: 1
                            radius: Tok.DesignTokens.radiusSm

                            Label {
                                anchors.centerIn: parent
                                text: modelData.type
                                color: Tok.DesignTokens.textSecondary
                                font.pixelSize: Tok.DesignTokens.fontSizeXs
                            }
                        }

                        // 值（可编辑）
                        TextField {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 24
                            text: modelData.value !== undefined ? String(modelData.value) : ""
                            color: Tok.DesignTokens.textPrimary
                            font.pixelSize: Tok.DesignTokens.fontSizeSm
                            background: Rectangle {
                                color: Tok.DesignTokens.bgSurface
                                border.color: parent.activeFocus
                                    ? Tok.DesignTokens.borderFocus
                                    : Tok.DesignTokens.borderDefault
                                border.width: 1
                                radius: Tok.DesignTokens.radiusSm
                            }
                            onEditingFinished: {
                                var newVal = text
                                // 根据类型转换
                                if (modelData.type === "Int") {
                                    newVal = parseInt(newVal)
                                    if (isNaN(newVal)) newVal = 0
                                } else if (modelData.type === "Double") {
                                    newVal = parseFloat(newVal)
                                    if (isNaN(newVal)) newVal = 0.0
                                } else if (modelData.type === "Bool") {
                                    newVal = (newVal === "true" || newVal === "1")
                                }
                                if (bridge && bridge.variableManager) {
                                    bridge.variableManager.setValue(modelData.name, newVal)
                                }
                            }
                        }

                        // 编辑按钮
                        Button {
                            text: "✎"
                            Layout.preferredWidth: 28
                            Layout.preferredHeight: 24
                            font.pixelSize: Tok.DesignTokens.fontSizeSm
                            ToolTip.text: "编辑变量详情"
                            ToolTip.visible: hovered
                            ToolTip.delay: 500
                            onClicked: varEditDialog.openEdit(modelData.name)
                        }

                        // 删除按钮
                        Button {
                            text: "✕"
                            Layout.preferredWidth: 28
                            Layout.preferredHeight: 24
                            font.pixelSize: Tok.DesignTokens.fontSizeSm
                            palette.buttonText: "#FF6B6B"   // Qt 6 Controls Button 用 palette 而非 color
                            ToolTip.text: "删除变量"
                            ToolTip.visible: hovered
                            ToolTip.delay: 500
                            onClicked: {
                                if (bridge && bridge.variableManager) {
                                    bridge.variableManager.removeVariable(modelData.name)
                                }
                            }
                        }
                    }

                    MouseArea {
                        id: varMouseArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        propagateComposedEvents: true
                    }
                }

                // 空状态
                Label {
                    anchors.centerIn: parent
                    visible: controlVarList.count === 0
                    text: "暂无控制变量\n点击 + 创建变量，使用 ${变量名} 绑定到算子参数"
                    color: Tok.DesignTokens.textTertiary
                    font.pixelSize: Tok.DesignTokens.fontSizeBase
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                }
            }

            // === 算子参数列表（Tab 2，v5.3 新增）===
            // 按节点分组显示所有算子节点的参数，参数变化时自动更新
            ListView {
                id: operatorParamList
                anchors.fill: parent
                anchors.margins: Tok.DesignTokens.space1
                clip: true
                visible: (root.mode === 0 && root.currentTab === 2) || (root.mode === 3)
                spacing: 2
                model: []
                section.property: "nodeId"
                section.delegate: Rectangle {
                    width: operatorParamList.width
                    height: 28
                    color: Tok.DesignTokens.bgHeader
                    border.color: Tok.DesignTokens.borderDefault
                    border.width: 1

                    Label {
                        anchors.fill: parent
                        anchors.leftMargin: Tok.DesignTokens.space2
                        verticalAlignment: Text.AlignVCenter
                        text: {
                            // 从 model 中查找该 nodeId 对应的算子中文名
                            var items = operatorParamList.model
                            for (var i = 0; i < items.length; ++i) {
                                if (items[i].nodeId === section) {
                                    return items[i].cnName + "  [" + items[i].nodeType + "]"
                                }
                            }
                            return section.substring(0, 12) + "..."
                        }
                        color: Tok.DesignTokens.accentPrimary
                        font.pixelSize: Tok.DesignTokens.fontSizeSm
                        font.bold: true
                        font.family: Tok.DesignTokens.fontFamilyCJK
                    }
                }

                delegate: Rectangle {
                    width: operatorParamList.width
                    height: 28
                    color: opParamMouseArea.containsMouse
                        ? Tok.DesignTokens.bgSurface
                        : Tok.DesignTokens.bgPanel
                    border.color: Tok.DesignTokens.borderDefault
                    border.width: 1
                    radius: 2

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: Tok.DesignTokens.space2
                        anchors.rightMargin: Tok.DesignTokens.space2
                        spacing: Tok.DesignTokens.space2

                        // 方向标签（输入/输出）
                        Rectangle {
                            Layout.preferredWidth: 36
                            Layout.preferredHeight: 20
                            color: modelData.direction === "out"
                                ? Tok.DesignTokens.accentSuccess
                                : Tok.DesignTokens.bgCanvas
                            border.color: Tok.DesignTokens.borderDefault
                            border.width: 1
                            radius: 2

                            Label {
                                anchors.centerIn: parent
                                text: modelData.directionLabel || "?"
                                color: modelData.direction === "out"
                                    ? "#ffffff"
                                    : Tok.DesignTokens.textSecondary
                                font.pixelSize: Tok.DesignTokens.fontSizeXs
                            }
                        }

                        // 参数中文名
                        Label {
                            text: modelData.paramCnName
                            color: Tok.DesignTokens.textPrimary
                            font.pixelSize: Tok.DesignTokens.fontSizeSm
                            font.family: Tok.DesignTokens.fontFamilyCJK
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                        }

                        // 参数类型
                        Rectangle {
                            Layout.preferredWidth: 50
                            Layout.preferredHeight: 20
                            color: Tok.DesignTokens.bgCanvas
                            border.color: Tok.DesignTokens.borderDefault
                            border.width: 1
                            radius: 2

                            Label {
                                anchors.centerIn: parent
                                text: modelData.typeText
                                color: Tok.DesignTokens.textSecondary
                                font.pixelSize: Tok.DesignTokens.fontSizeXs
                            }
                        }

                        // 参数值（只读显示）
                        Label {
                            text: root.formatParamValue(modelData.value)
                            color: modelData.direction === "out"
                                ? Tok.DesignTokens.accentSuccess
                                : Tok.DesignTokens.accentPrimary
                            font.pixelSize: Tok.DesignTokens.fontSizeSm
                            font.family: Tok.DesignTokens.fontMono
                            Layout.preferredWidth: 120
                            elide: Text.ElideRight
                            horizontalAlignment: Text.AlignRight
                        }
                    }

                    MouseArea {
                        id: opParamMouseArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                    }
                }

                // 空状态
                Label {
                    anchors.centerIn: parent
                    visible: operatorParamList.count === 0
                    text: "暂无算子参数\n在画布上添加算子后将自动列出"
                    color: Tok.DesignTokens.textTertiary
                    font.pixelSize: Tok.DesignTokens.fontSizeBase
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                }
            }
        }
    }

    // === 变量编辑对话框 ===
    VarEditDialog {
        id: varEditDialog
        bridge: root.bridge
        anchors.centerIn: parent
    }
}
