// =====================================================================
// OperatorEditorDialog.qml — 算子模态详编对话框（v3.0.0 方案B Design Tokens 迁移）
//
// 功能：
// - 复用 ParamForm.qml 渲染动态表单
// - 提供 应用/取消/重置 按钮
// - 打开时缓存当前值（取消 = 恢复缓存）
// - 应用：调 bridge.updateOperatorParams(...)
//
// 入口：
//   var dlg = operatorEditorDialogComponent.createObject(root, {
//       bridge: bridge,
//       nodeId: "abc-123"
//   })
//   dlg.open()
// =====================================================================

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import QtQuick.Dialogs
import QDV.EditView 3.0 as Tok

// v5.3：改为 Popup（而非 Dialog），避免 Dialog 的 Overlay 创建独立 QRhi 上下文。
Popup {
    id: dlg
    modal: true
    width: 520
    height: 640
    padding: 0
    // v5.3.1：Popup 不支持 anchors，用 x/y 手动居中
    x: (parent.width - width) / 2
    y: (parent.height - height) / 2
    background: Rectangle {
        color: Tok.DesignTokens.bgPanel
        border.color: Tok.DesignTokens.accentPrimary
        border.width: 1
        radius: Tok.DesignTokens.radiusMd
    }

    // ============ 外部接口 ============
    property var bridge
    property string nodeId: ""
    // 当前节点 OperatorMeta + params 缓存
    property var meta: null
    property var workingValues: ({})  // 编辑过程中的当前值
    property var snapshotValues: ({})  // 打开时的快照（取消用）
    // v5.4：输出参数开关相关
    property bool outputSectionCollapsed: true  // 输出设置分组默认折叠
    property var outputConfig: ({})             // 当前节点的输出开关配置（key=输出名 value={enabled:bool}）
    // 输出项配置增强（spec：editor-output-connection-optimization）
    // （已按用户反馈移除关键字搜索框，保留状态过滤与分组）
    property int outputStatusFilter: 0          // 0=全部 1=仅启用 2=仅禁用
    property string outputGroupFilter: ""       // 当前分组过滤（空=全部）

    // v-spec: 内联运行状态（0=空闲 1=运行中 2=成功 3=失败）
    property int runStatus: 0
    // v-spec: 内联运行状态文本
    property string runStatusText: ""

    // 输出项状态过滤 + 分组：返回扁平分组列表（含 groupHeader 项）
    function groupedOutputs() {
        if (!dlg.meta || !dlg.meta.outputs) return []
        var result = []
        var seenGroups = {}
        var metaOutputs = dlg.meta.outputs
        for (var gi = 0; gi < metaOutputs.length; ++gi) {
            var grp = metaOutputs[gi].group || metaOutputs[gi].typeName || "其他"
            if (!seenGroups[grp]) {
                seenGroups[grp] = true
            }
        }
        var groups = Object.keys(seenGroups)
        for (var g = 0; g < groups.length; ++g) {
            var gname = groups[g]
            if (dlg.outputGroupFilter !== "" && gname !== dlg.outputGroupFilter) continue
            // 分组头
            result.push({isGroupHeader: true, group: gname})
            for (var oi = 0; oi < metaOutputs.length; ++oi) {
                var mo = metaOutputs[oi]
                var og = mo.group || mo.typeName || "其他"
                if (og !== gname) continue
                // 状态过滤
                var enabled = (dlg.outputConfig[mo.name] && dlg.outputConfig[mo.name].enabled === true)
                    || (!dlg.outputConfig[mo.name] && mo.defaultEnabled !== false)
                if (dlg.outputStatusFilter === 1 && !enabled) continue
                if (dlg.outputStatusFilter === 2 && enabled) continue
                var entry = JSON.parse(JSON.stringify(mo))
                entry.enabledNow = enabled
                result.push(entry)
            }
        }
        return result
    }

    // 唯一的输出分组列表（下拉过滤用）
    function outputGroups() {
        if (!dlg.meta || !dlg.meta.outputs) return []
        var seen = {}
        var out = []
        for (var i = 0; i < dlg.meta.outputs.length; ++i) {
            var grp = dlg.meta.outputs[i].group || dlg.meta.outputs[i].typeName || "其他"
            if (!seen[grp]) { seen[grp] = true; out.push(grp) }
        }
        return out
    }

    // 批量启用/禁用（走 updateOutputConfig 可撤销路径）
    function batchSetOutputs(enabledVal) {
        if (!dlg.meta || !dlg.meta.outputs) return
        var newConfig = JSON.parse(JSON.stringify(dlg.outputConfig || ({})))
        for (var i = 0; i < dlg.meta.outputs.length; ++i) {
            var name = dlg.meta.outputs[i].name
            if (!newConfig[name]) newConfig[name] = {}
            newConfig[name].enabled = enabledVal
        }
        dlg.outputConfig = newConfig
        if (dlg.bridge && dlg.nodeId) dlg.bridge.updateOutputConfig(dlg.nodeId, newConfig)
    }

    // 单输出项切换（深拷贝触发变化信号）
    function toggleOutput(name, checked) {
        var newConfig = JSON.parse(JSON.stringify(dlg.outputConfig || ({})))
        if (!newConfig[name]) newConfig[name] = {}
        newConfig[name].enabled = checked
        dlg.outputConfig = newConfig
        if (dlg.bridge && dlg.nodeId) dlg.bridge.updateOutputConfig(dlg.nodeId, newConfig)
    }

    // 加载节点数据
    function load() {
        if (!bridge || !nodeId) return
        // v-spec: 打开/切换节点时重置运行状态
        runStatus = 0
        runStatusText = ""
        // 在 currentNodes 中找
        var nodes = bridge.currentNodes || []
        var found = null
        for (var i = 0; i < nodes.length; ++i) {
            if (nodes[i].id === nodeId) {
                found = nodes[i]
                break
            }
        }
        if (!found) {
            console.warn("[OperatorEditorDialog] 节点不存在:", nodeId)
            return
        }
        meta = bridge.getOperatorMeta(found.type)
        workingValues = JSON.parse(JSON.stringify(found.params || ({})))
        snapshotValues = JSON.parse(JSON.stringify(workingValues))

        // v5.4：加载节点输出开关配置
        if (bridge && nodeId) {
            outputConfig = bridge.getOutputConfig(nodeId) || ({})
        }
    }

    onNodeIdChanged: load()
    onBridgeChanged: load()
    Component.onCompleted: load()

    // ============ 布局 ============
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 0
        spacing: 0

        // 顶部标题栏
        Rectangle {
            Layout.fillWidth: true
            height: 40
            color: Tok.DesignTokens.bgHeader

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 8

                Rectangle {
                    width: 18; height: 18
                    color: Tok.DesignTokens.accentPrimary
                    radius: Tok.DesignTokens.radiusSm
                }
                Label {
                    text: dlg.meta ? dlg.meta.cnName + " 参数编辑器" : "参数编辑器"
                    color: Tok.DesignTokens.textPrimary
                    font.pixelSize: Tok.DesignTokens.fontSizeLg
                    font.bold: true
                    font.family: Tok.DesignTokens.fontFamilyCJK
                    Layout.fillWidth: true
                }
                Label {
                    text: dlg.nodeId ? "(" + dlg.nodeId.substring(0, 8) + "...)" : ""
                    color: Tok.DesignTokens.textPlaceholder
                    font.pixelSize: Tok.DesignTokens.fontSizeXs
                    font.family: Tok.DesignTokens.fontFamilyCJK
                }
            }
        }

        // 描述（如果有）
        Label {
            visible: dlg.meta && dlg.meta.description
            text: dlg.meta ? dlg.meta.description : ""
            color: Tok.DesignTokens.textTertiary
            font.pixelSize: Tok.DesignTokens.fontSizeSm
            font.family: Tok.DesignTokens.fontFamilyCJK
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            Layout.leftMargin: 12
            Layout.rightMargin: 12
            Layout.topMargin: 8
        }

        // 分类标签
        Label {
            visible: dlg.meta && dlg.meta.category
            text: dlg.meta ? "分类：" + dlg.meta.category : ""
            color: Tok.DesignTokens.accentPrimary
            font.pixelSize: Tok.DesignTokens.fontSizeXs
            font.family: Tok.DesignTokens.fontFamilyCJK
            Layout.leftMargin: 12
        }

        // 分隔
        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: Tok.DesignTokens.borderDefault
            Layout.topMargin: 6
        }

        // ParamForm 主体
        ScrollView {
            id: formScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            ScrollBar.vertical.policy: ScrollBar.AsNeeded

            ColumnLayout {
                width: formScroll.width
                spacing: 6

                ParamForm {
                    id: form
                    Layout.fillWidth: true
                    params: dlg.meta ? dlg.meta.params : []
                    currentValues: dlg.workingValues
                    // P1-B4-H1 联动：传入算子类型供 isParamVisible 判断
                    operatorType: dlg.meta ? dlg.meta.type : ""
                    // v5.4.0：模型库列表（用于 modelPath 参数的下拉选择）
                    modelList: dlg.bridge ? dlg.bridge.getRegisteredModels() : []
                    // v5.4.2：桥接器（ZeroShotDetect 模型路径与模型类型联动）
                    bridge: dlg.bridge
                    // P0-3（0906 优化）：单键更新走浅拷贝（workingValues 是对话框本地工作值，
                    // 最终由 apply 时一次性写回；无需每键全表 JSON 深拷贝）
                    onValueChanged: function(name, value) {
                        var wv = dlg.workingValues || ({})
                        wv[name] = value
                        dlg.workingValues = wv
                    }
                    onValuesChanged: function(newValues) {
                        dlg.workingValues = JSON.parse(JSON.stringify(newValues))
                    }
                    onValidationError: function(name, message) {
                        console.warn("[OperatorEditorDialog] 校验失败:", name, message)
                    }
                }

                // ============ v5.4: 输出参数开关区 ============
                // 放在参数列表下方，随 ScrollView 滚动
                // 仅当算子元数据包含 outputs 时显示
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 10
                    Layout.leftMargin: 12
                    Layout.rightMargin: 12
                    Layout.bottomMargin: 10
                    spacing: 6
                    visible: dlg.meta && dlg.meta.outputs
                             && dlg.meta.outputs.length > 0

                    // 折叠标题
                    Rectangle {
                        Layout.fillWidth: true
                        height: 28
                        color: Tok.DesignTokens.bgSurface
                        radius: Tok.DesignTokens.radiusSm

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 8
                            anchors.rightMargin: 8
                            spacing: 6

                            Label {
                                text: "输出参数设置"
                                color: Tok.DesignTokens.accentSuccess
                                font.bold: true
                                font.pixelSize: Tok.DesignTokens.fontSizeSm
                                font.family: Tok.DesignTokens.fontFamilyCJK
                            }
                            Item { Layout.fillWidth: true }
                            Label {
                                text: dlg.outputSectionCollapsed ? "▶" : "▼"
                                color: Tok.DesignTokens.textSecondary
                                font.pixelSize: Tok.DesignTokens.fontSizeXs
                            }
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: dlg.outputSectionCollapsed = !dlg.outputSectionCollapsed
                            }
                        }
                    }

                    // 输出开关列表（折叠时隐藏）
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: !dlg.outputSectionCollapsed
                        spacing: 6

                        // ---- 输出项配置增强：分组 / 状态过滤 / 批量 ----
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6

                            // 分组下拉
                            ComboBox {
                                id: outputGroupCombo
                                Layout.preferredWidth: 90
                                height: 26
                                model: dlg.outputGroups()
                                font.pixelSize: Tok.DesignTokens.fontSizeXs
                                font.family: Tok.DesignTokens.fontFamilyCJK
                                currentIndex: 0
                                onActivated: {
                                    if (index === 0) dlg.outputGroupFilter = ""
                                    else dlg.outputGroupFilter = model[index]
                                }
                                Component.onCompleted: {
                                    // 预置"全部分组"项
                                    var arr = ["全部分组"]
                                    var gs = dlg.outputGroups()
                                    for (var i = 0; i < gs.length; ++i) arr.push(gs[i])
                                    model = arr
                                    currentIndex = 0
                                }
                            }
                        }

                        // 状态过滤 + 批量操作
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6

                            RowLayout {
                                spacing: 2
                                Repeater {
                                    model: ["全部", "仅启用", "仅禁用"]
                                    delegate: Button {
                                        text: modelData
                                        height: 24
                                        flat: true
                                        font.pixelSize: Tok.DesignTokens.fontSizeXs
                                        font.family: Tok.DesignTokens.fontFamilyCJK
                                        checked: dlg.outputStatusFilter === index
                                        highlighted: dlg.outputStatusFilter === index
                                        onClicked: dlg.outputStatusFilter = index
                                    }
                                }
                            }
                            Item { Layout.fillWidth: true }
                            Button {
                                text: "全选"
                                height: 24
                                flat: true
                                font.pixelSize: Tok.DesignTokens.fontSizeXs
                                font.family: Tok.DesignTokens.fontFamilyCJK
                                onClicked: dlg.batchSetOutputs(true)
                            }
                            Button {
                                text: "全不选"
                                height: 24
                                flat: true
                                font.pixelSize: Tok.DesignTokens.fontSizeXs
                                font.family: Tok.DesignTokens.fontFamilyCJK
                                onClicked: dlg.batchSetOutputs(false)
                            }
                        }

                        // ---- 分组 + 过滤后的输出项列表 ----
                        // 注意：委托内联渲染（不使用根级 Component + Loader），
                        // 否则加载后无法访问 Repeater 委托作用域的 modelData，导致名称不显示。
                        Repeater {
                            model: dlg.groupedOutputs()

                            delegate: Rectangle {
                                Layout.fillWidth: true
                                // 分组头：独立渲染
                                readonly property bool isHeader: modelData.isGroupHeader === true
                                height: isHeader ? 26 : 30
                                color: "transparent"

                                // ---- 分组头行 ----
                                RowLayout {
                                    visible: isHeader
                                    anchors.fill: parent
                                    anchors.leftMargin: 4
                                    spacing: 6
                                    Rectangle {
                                        width: 3; height: 14
                                        color: Tok.DesignTokens.accentPrimary
                                        radius: 1
                                    }
                                    Label {
                                        text: modelData.group
                                        color: Tok.DesignTokens.textSecondary
                                        font.bold: true
                                        font.pixelSize: Tok.DesignTokens.fontSizeXs
                                        font.family: Tok.DesignTokens.fontFamilyCJK
                                        Layout.fillWidth: true
                                    }
                                    Label {
                                        text: "快速"
                                        color: Tok.DesignTokens.accentPrimary
                                        font.pixelSize: Tok.DesignTokens.fontSizeXs
                                        font.family: Tok.DesignTokens.fontFamilyCJK
                                        MouseArea {
                                            anchors.fill: parent
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: {
                                                // 按分组批量启用：仅切该分组下输出项为 true
                                                if (!dlg.meta || !dlg.meta.outputs) return
                                                var newConfig = JSON.parse(JSON.stringify(dlg.outputConfig || ({})))
                                                for (var i = 0; i < dlg.meta.outputs.length; ++i) {
                                                    var mo = dlg.meta.outputs[i]
                                                    var og = mo.group || mo.typeName || "其他"
                                                    if (og === modelData.group) {
                                                        if (!newConfig[mo.name]) newConfig[mo.name] = {}
                                                        newConfig[mo.name].enabled = true
                                                    }
                                                }
                                                dlg.outputConfig = newConfig
                                                if (dlg.bridge && dlg.nodeId) dlg.bridge.updateOutputConfig(dlg.nodeId, newConfig)
                                            }
                                        }
                                    }
                                }

                                // ---- 输出项行 ----
                                RowLayout {
                                    visible: !isHeader
                                    anchors.fill: parent
                                    spacing: 8
                                    CheckBox {
                                        text: (modelData.alias || modelData.cnName || modelData.name)
                                        // 优先取 outputConfig 中的 enabled，未配置时回退到 defaultEnabled
                                        checked: modelData.enabledNow
                                        onCheckedChanged: dlg.toggleOutput(modelData.name, checked)
                                        // multiTargetOnly 输出在单目标场景下给出提示
                                        ToolTip.visible: hovered && modelData.multiTargetOnly === true
                                        ToolTip.text: "仅多目标分类场景下启用有效"
                                        font.pixelSize: Tok.DesignTokens.fontSizeXs
                                        font.family: Tok.DesignTokens.fontFamilyCJK
                                    }
                                    Rectangle {
                                        width: 10; height: 10; radius: 2
                                        color: modelData.color || Tok.DesignTokens.accentSuccess
                                    }
                                    Label {
                                        text: "(" + (modelData.typeName || "") + ")"
                                        color: Tok.DesignTokens.textSecondary
                                        font.pixelSize: Tok.DesignTokens.fontSizeXs
                                        font.family: Tok.DesignTokens.fontFamilyCJK
                                    }
                                    Label {
                                        text: modelData.desc || ""
                                        color: Tok.DesignTokens.textPlaceholder
                                        font.pixelSize: Tok.DesignTokens.fontSizeXs
                                        font.family: Tok.DesignTokens.fontFamilyCJK
                                        Layout.fillWidth: true
                                        elide: Text.ElideRight
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // ============ v-spec: 内联运行状态条 ============
        // 移除模态弹窗，将运行状态直接集成到编辑器窗口底部，实时展示且不阻断操作
        Rectangle {
            Layout.fillWidth: true
            height: 30
            visible: dlg.runStatus !== 0
            // 运行中：柔和背景；成功/失败：语义强调色
            color: dlg.runStatus === 1 ? Tok.DesignTokens.bgSurface
                 : dlg.runStatus === 2 ? Tok.DesignTokens.accentSuccess
                 : Tok.DesignTokens.accentError
            // 运行中左侧强调描边
            Rectangle {
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: 3
                visible: dlg.runStatus === 1
                color: Tok.DesignTokens.accentPrimary
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Tok.DesignTokens.space2
                anchors.rightMargin: Tok.DesignTokens.space2
                spacing: Tok.DesignTokens.space2

                // 运行中：进度指示器
                BusyIndicator {
                    id: runBusy
                    visible: dlg.runStatus === 1
                    running: dlg.runStatus === 1
                    implicitWidth: 14
                    implicitHeight: 14
                }
                // 成功/失败：状态图标
                Label {
                    visible: dlg.runStatus !== 1
                    text: dlg.runStatus === 2 ? "\u2713" : "\u2715"
                    color: "#FFFFFF"
                    font.pixelSize: Tok.DesignTokens.fontSizeSm
                    font.bold: true
                }
                // 状态文本
                Label {
                    text: dlg.runStatusText
                    color: "#FFFFFF"
                    font.pixelSize: Tok.DesignTokens.fontSizeSm
                    font.family: Tok.DesignTokens.fontFamilyCJK
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    wrapMode: Text.NoWrap
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }

        // 底部按钮栏
        Rectangle {
            Layout.fillWidth: true
            height: 56
            color: Tok.DesignTokens.bgHeader

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                anchors.topMargin: 8
                anchors.bottomMargin: 8
                spacing: 8

                Button {
                    text: "重置默认"
                    flat: true
                    onClicked: {
                        if (!dlg.meta) return
                        var defaults = {}
                        for (var i = 0; i < dlg.meta.params.length; ++i) {
                            defaults[dlg.meta.params[i].name] = dlg.meta.params[i].defaultValue
                        }
                        // v5.3.7：通过 form.resetValues 刷新控件，并同步 workingValues
                        form.resetValues(defaults)
                        dlg.workingValues = defaults
                    }
                }
                Button {
                    text: "撤销修改"
                    flat: true
                    onClicked: {
                        // v5.3.7：通过 form.resetValues 刷新控件，并同步 workingValues
                        form.resetValues(dlg.snapshotValues)
                        dlg.workingValues = JSON.parse(JSON.stringify(dlg.snapshotValues))
                    }
                }
                Item { Layout.fillWidth: true }
                // v2.5.0 功能 5c：运行按钮（先应用参数，再执行）
                // v2.5.0 修复：智能识别输入源 — 若上游链含 ReadImage 节点则直接执行，
                // 否则才弹 FileDialog
                Button {
                    text: "\u25B6 运行"
                    flat: true
                    ToolTip.text: "应用当前参数并执行此算子（含上游链）；若上游无 ReadImage 则需选择图像"
                    ToolTip.visible: hovered
                    onClicked: {
                        // 先应用参数
                        if (dlg.bridge && dlg.nodeId) {
                            dlg.bridge.updateOperatorParams(dlg.nodeId, dlg.workingValues)
                        }
                        // v5.3.8：统一通过 C++ 解析运行输入源
                        var runInput = dlg.bridge.resolveRunInput(dlg.nodeId)
                        // v-spec: 内联运行状态
                        dlg.runStatus = 1
                        dlg.runStatusText = "运行中..."
                        if (!runInput.required) {
                            // 数据源型算子（打开相机/采集图像等）无需输入图像，直接运行
                            dlg.bridge.runSingleOperatorAsync(dlg.nodeId, "")
                            return
                        }
                        // 智能识别输入源
                        var autoPath = runInput.path
                        if (autoPath && autoPath !== "") {
                            // v5.3.4：改为异步执行，避免阻塞 UI
                            dlg.bridge.runSingleOperatorAsync(dlg.nodeId, "")
                        } else {
                            // 无 ReadImage 上游 → 弹 FileDialog
                            runOperatorFileDialog.open()
                        }
                    }
                }
                Button {
                    text: "取消"
                    flat: true
                    onClicked: dlg.close()
                }
                Button {
                    text: "应用"
                    highlighted: true
                    onClicked: {
                        if (dlg.bridge && dlg.nodeId) {
                            dlg.bridge.updateOperatorParams(dlg.nodeId, dlg.workingValues)
                        }
                        dlg.close()
                    }
                }
            }
        }
    }

    // v2.5.0 功能 5c：运行算子的输入图像选择对话框
    FileDialog {
        id: runOperatorFileDialog
        title: "选择运行输入图像"
        fileMode: FileDialog.OpenFile
        nameFilters: ["图像文件 (*.png *.jpg *.jpeg *.bmp *.tif *.tiff)", "所有文件 (*)"]
        onAccepted: {
            var path = String(selectedFile)
            if (path.startsWith("file:///")) path = path.substring(8)
            else if (path.startsWith("file://")) path = path.substring(7)
            // v5.3.4：改为异步执行，避免阻塞 UI
            dlg.runStatus = 1
            dlg.runStatusText = "运行中..."
            dlg.bridge.runSingleOperatorAsync(dlg.nodeId, path)
        }
    }

    // v2.5.0 功能 5c：运行结果摘要（v-spec: 改为内联状态条，移除模态弹窗）
    // 接收异步执行结果
    Connections {
        target: dlg.bridge
        function onSingleOperatorFinished(result) {
            if (!result || !result.success) {
                dlg.runStatus = 3
                dlg.runStatusText =
                    "运行失败：" + (result && result.error ? result.error : "未知错误")
            } else {
                dlg.runStatus = 2
                dlg.runStatusText =
                    "运行成功 · 上游 " + result.upstreamCount + " 个算子 · 耗时 " +
                    result.elapsedMs + " ms · 输出图像：" + (result.outputImagePath || "无")
            }
        }
    }
}
