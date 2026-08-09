// =====================================================================
// OperatorTunerDialog.qml — 交互式调参对话框（v3.3.0 2026-07-09）
//
// 功能：
// - 双击算子节点打开此对话框
// - 将数值(Int/Float)参数以滑块呈现，枚举以下拉框，布尔以勾选框
// - 拖动滑块/修改下拉时，防抖 300ms 后异步重新执行当前算子（含上游链）
// - 右侧实时显示算子输出图像，实现"边看效果边调参"
// - 「应用」将参数写回程序节点；「重置」恢复打开时快照；「默认」恢复算子默认值
// - 复杂参数(String/ROI/Vector)和动作型算子(ReadImage/GrabImage)不做交互处理
//
// 入口：
//   var dlg = tunerComp.createObject(root, { bridge: bridge, nodeId: nodeId })
//   dlg.open()
//
// 依赖：
// - bridge.runSingleOperatorAsync(nodeId, inputPath)  异步执行
// - bridge.singleOperatorFinished(result)             执行完成信号
// - bridge.updateOperatorParams(nodeId, params)       写回参数
// - bridge.resolveInputImageForNode(nodeId)           检查上游输入
// =====================================================================

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QDV.EditView 3.0 as Tok

// v5.3：改为 Popup（而非 Dialog），避免 Dialog 的 Overlay 创建独立 QRhi 上下文，
// 导致 "Texture belongs to QRhi A but client code attempted to use it with QRhi B"。
// Popup 渲染在父组件的 QQuickWindow 中，共享同一个 QRhi。
Popup {
    id: tuner
    modal: false               // 非模态：用户可同时操作主窗口查看预览
    width: 760
    height: 520
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

    // ============ 内部状态 ============
    property var meta: null                  // 算子元数据
    property var tunableParams: []           // 可调参数列表（仅 Int/Float/Enum/Bool）
    property var workingValues: ({})         // 编辑过程中的当前值
    property var snapshotValues: ({})        // 打开时的快照（重置用）
    property var defaultValues: ({})         // 算子默认值（默认按钮用）
    property string previewSource: ""        // 预览图 URL
    property string lastElapsed: ""          // 上次执行耗时
    property bool hasInputImage: false       // 上游/自身是否有可用输入图像
    property bool running: false             // 是否正在执行
    property string statusMessage: ""        // 状态提示文本
    property bool isReadImage: false         // 当前节点是否为 ReadImage（输入源型算子）
    // v5.3.7：相机类算子不需要输入图像，可直接执行
    property bool isNoImageOperator: false   // OpenFramegrabber/GrabImage 等无需图像输入的算子
    // v5.4：输出参数开关相关
    property bool outputSectionCollapsed: true  // 输出设置分组默认折叠
    property var outputConfig: ({})             // 当前节点的输出开关配置（key=输出名 value={enabled:bool}）

    // ============ 防抖定时器 ============
    // 参数变化后 300ms 内若无新变化，则触发执行；避免拖动滑块过程中频繁执行
    Timer {
        id: debounceTimer
        interval: 300
        repeat: false
        onTriggered: tuner.executeCurrent()
    }

    // ============ 加载节点数据 ============
    function load() {
        if (!bridge || !nodeId) return
        var nodes = bridge.currentNodes || []
        var found = null
        for (var i = 0; i < nodes.length; ++i) {
            if (nodes[i].id === nodeId) {
                found = nodes[i]
                break
            }
        }
        if (!found) {
            tuner.statusMessage = "节点不存在"
            return
        }
        meta = bridge.getOperatorMeta(found.type)
        workingValues = JSON.parse(JSON.stringify(found.params || ({})))
        // v5.3.7 诊断日志：确认 load 时读取的参数值
        console.log("Tuner load: type=" + found.type + " params=" + JSON.stringify(workingValues))
        snapshotValues = JSON.parse(JSON.stringify(workingValues))
        isReadImage = (found.type === "ReadImage")
        // v5.3.7：相机类算子和 ReadImage 不需要上游图像输入
        isNoImageOperator = (found.type === "OpenFramegrabber" ||
                             found.type === "GrabImage" ||
                             found.type === "RobotPose" ||
                             found.type === "HandEyeCalib" ||
                             found.type === "ReadImage")

        // 构建默认值 map
        var defs = {}
        if (meta && meta.params) {
            for (var j = 0; j < meta.params.length; ++j) {
                defs[meta.params[j].name] = meta.params[j].defaultValue
            }
        }
        defaultValues = defs

        // 过滤可调参数：保留 Int(0)/Float(1)/Enum(2)/Bool(3)
        // v5.3 修复 ReadImage 输入源：额外保留 String 类型的 filePath 参数，
        // 让用户在交互式调参窗口中直接选择图像输入源。
        var tunable = []
        if (meta && meta.params) {
            for (var k = 0; k < meta.params.length; ++k) {
                var p = meta.params[k]
                var isFilePath = (p.name === "filePath")
                if (p.type === 0 || p.type === 1 || p.type === 2 || p.type === 3 ||
                    (isReadImage && isFilePath && p.type === 4)) {
                    tunable.push(p)
                }
            }
        }
        tunableParams = tunable

        // v5.4：加载节点输出开关配置（从 bridge 读取，走 UndoCommand 已持久化的数据）
        if (bridge && nodeId) {
            outputConfig = bridge.getOutputConfig(nodeId) || ({})
        }

        // 检查上游/自身输入图像
        refreshInputImageStatus()

        if (tunableParams.length === 0) {
            statusMessage = "该算子无可调参数，不支持交互调参"
        } else if (!hasInputImage && !isNoImageOperator) {
            statusMessage = isReadImage
                ? "请选择图像文件作为输入源"
                : "上游无 ReadImage 节点或未配置图像，请先配置输入源"
        } else {
            statusMessage = "就绪 — 拖动滑块或修改下拉即可实时预览"
            // 首次加载自动执行一次，显示当前效果
            executeCurrent()
        }
    }

    onNodeIdChanged: load()
    onBridgeChanged: load()
    Component.onCompleted: load()

    // ============ 刷新输入源状态 ============
    function refreshInputImageStatus() {
        if (!bridge || !nodeId) {
            hasInputImage = false
            return
        }
        // 对 ReadImage 节点，直接检查自身的 filePath 参数
        if (isReadImage) {
            var fp = workingValues["filePath"] || ""
            hasInputImage = (fp.length > 0)
            return
        }
        // 其他算子走上游链查找
        var inputPath = bridge.resolveInputImageForNode(nodeId)
        hasInputImage = (inputPath && inputPath.length > 0)
    }

    // ============ 执行当前算子（异步）============
    function executeCurrent() {
        if (!bridge || !nodeId) return
        // v5.3.7：相机类算子（isNoImageOperator）跳过 hasInputImage 检查
        if (tunableParams.length === 0) return
        if (!hasInputImage && !isNoImageOperator) return
        // 先把当前编辑值写回节点，确保 runSingleOperator 用最新参数
        bridge.updateOperatorParams(nodeId, workingValues)
        running = true
        statusMessage = "执行中..."
        // ReadImage 节点：用自身 filePath 作为输入路径，让 runSingleOperator 走完整链
        var inputPath = isReadImage ? (workingValues["filePath"] || "") : ""
        bridge.runSingleOperatorAsync(nodeId, inputPath)
    }

    // ============ 接收异步执行结果 ============
    Connections {
        target: bridge
        function onSingleOperatorStarted() {
            running = true
        }
        function onSingleOperatorFinished(result) {
            running = false
            if (result && result.success) {
                var path = result.outputImagePath || ""
                if (path && path.length > 0) {
                    tuner.previewSource = "file:///" + path
                }
                lastElapsed = result.elapsedMs + " ms"
                statusMessage = "执行成功 — " + lastElapsed
            } else {
                statusMessage = "执行失败：" + (result && result.error ? result.error : "未知错误")
            }
        }
    }

    // ============ 参数操作 ============
    function setValue(name, val) {
        // v5.3.7：用深拷贝确保 workingValues 触发变化信号
        // 之前 var v = workingValues; v[name]=val; workingValues=v 是同一引用，
        // QML 不触发 onWorkingValuesChanged，导致 currentIndex 绑定不刷新
        var v = JSON.parse(JSON.stringify(workingValues || ({})))
        v[name] = val
        workingValues = v
        // v5.3 修复 ReadImage 输入源：filePath 变更后刷新输入源状态，
        // 并立即把参数写回节点，让属性面板同步。
        if (isReadImage && name === "filePath") {
            refreshInputImageStatus()
            if (bridge && nodeId) {
                bridge.updateOperatorParams(nodeId, workingValues)
            }
        }
        // 触发防抖执行
        debounceTimer.restart()
    }

    function applyAndClose() {
        if (bridge && nodeId && tunableParams.length > 0) {
            // v5.3.7 诊断日志：确认 applyAndClose 时传入的参数值
            console.log("Tuner applyAndClose: workingValues=" + JSON.stringify(workingValues))
            bridge.updateOperatorParams(nodeId, workingValues)
            // 立即读取确认参数是否被保存
            var savedParams = bridge.getOperatorParams(nodeId)
            console.log("Tuner applyAndClose: savedParams=" + JSON.stringify(savedParams))
        }
        tuner.close()
    }

    function resetToSnapshot() {
        workingValues = JSON.parse(JSON.stringify(snapshotValues))
        debounceTimer.restart()
    }

    function resetToDefault() {
        workingValues = JSON.parse(JSON.stringify(defaultValues))
        debounceTimer.restart()
    }

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
                    text: tuner.meta ? tuner.meta.cnName + " 交互调参" : "交互调参"
                    color: Tok.DesignTokens.textPrimary
                    font.pixelSize: Tok.DesignTokens.fontSizeLg
                    font.bold: true
                    font.family: Tok.DesignTokens.fontFamilyCJK
                    Layout.fillWidth: true
                }
                // 执行忙碌指示器
                BusyIndicator {
                    visible: tuner.running
                    running: tuner.running
                    implicitWidth: 22
                    implicitHeight: 22
                }
                Label {
                    text: tuner.lastElapsed
                    visible: tuner.lastElapsed.length > 0
                    color: Tok.DesignTokens.textTertiary
                    font.pixelSize: Tok.DesignTokens.fontSizeXs
                    font.family: Tok.DesignTokens.fontFamilyCJK
                }
            }
        }

        // 主体：左参数 + 右预览
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // ===== 左侧：参数控件 =====
            Rectangle {
                Layout.fillHeight: true
                Layout.preferredWidth: 360
                color: Tok.DesignTokens.bgSurface
                Layout.margins: 0

                ScrollView {
                    id: paramScroll
                    anchors.fill: parent
                    anchors.margins: 8
                    clip: true
                    ScrollBar.vertical.policy: ScrollBar.AsNeeded

                    ColumnLayout {
                        width: paramScroll.width - 16
                        spacing: 6

                        // 无可调参数提示
                        Label {
                            visible: tuner.tunableParams.length === 0
                            text: tuner.statusMessage
                            color: Tok.DesignTokens.textTertiary
                            font.pixelSize: 12
                            font.family: Tok.DesignTokens.fontFamilyCJK
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            topPadding: 40
                        }

                        // 参数 Repeater
                        Repeater {
                            model: tuner.tunableParams
                            delegate: Loader {
                                Layout.fillWidth: true
                                // 根据参数类型选择组件
                                sourceComponent: {
                                    if (!modelData) return null
                                    // v5.3 修复 ReadImage 输入源：filePath 参数使用专用文件选择组件
                                    if (tuner.isReadImage && modelData.name === "filePath") {
                                        return filePathFieldComp
                                    }
                                    switch (modelData.type) {
                                        case 0: return intSliderComp   // Int
                                        case 1: return floatSliderComp // Float
                                        case 2: return enumComboComp   // Enum
                                        case 3: return boolCheckComp   // Bool
                                        default: return null
                                    }
                                }
                                onLoaded: {
                                    if (item) {
                                        item.spec = modelData
                                        // v5.3.9-2 强化：ComboBox 额外显式同步一次
                                        if (item.combo && item.combo.syncIndexFromValue) {
                                            Qt.callLater(function() { item.combo.syncIndexFromValue() })
                                        }
                                    }
                                }
                            }
                        }

                        // ============ v5.4: 输出参数开关区 ============
                        // 放在参数列表下方，随 ScrollView 滚动
                        // 仅当算子元数据包含 outputs 时显示
                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.topMargin: 10
                            spacing: 6
                            visible: tuner.meta && tuner.meta.outputs
                                     && tuner.meta.outputs.length > 0

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
                                        text: tuner.outputSectionCollapsed ? "▶" : "▼"
                                        color: Tok.DesignTokens.textSecondary
                                        font.pixelSize: Tok.DesignTokens.fontSizeXs
                                    }
                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: tuner.outputSectionCollapsed = !tuner.outputSectionCollapsed
                                    }
                                }
                            }

                            // 输出开关列表（折叠时隐藏）
                            ColumnLayout {
                                Layout.fillWidth: true
                                visible: !tuner.outputSectionCollapsed
                                spacing: 4

                                Repeater {
                                    model: tuner.meta && tuner.meta.outputs ? tuner.meta.outputs : []
                                    delegate: RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 8

                                        CheckBox {
                                            // 优先取 outputConfig 中的 enabled，未配置时回退到 defaultEnabled
                                            checked: tuner.outputConfig[modelData.name]
                                                     ? (tuner.outputConfig[modelData.name].enabled === true)
                                                     : (modelData.defaultEnabled !== false)
                                            onCheckedChanged: {
                                                // 更新本地 outputConfig 副本（深拷贝触发变化信号）
                                                var newConfig = JSON.parse(JSON.stringify(tuner.outputConfig || ({})))
                                                if (!newConfig[modelData.name]) {
                                                    newConfig[modelData.name] = {}
                                                }
                                                newConfig[modelData.name].enabled = checked
                                                tuner.outputConfig = newConfig
                                                // 实时写回 bridge（走 UndoCommand 可撤销路径）
                                                if (tuner.bridge && tuner.nodeId) {
                                                    tuner.bridge.updateOutputConfig(tuner.nodeId, newConfig)
                                                }
                                            }
                                            // multiTargetOnly 输出在单目标场景下给出提示
                                            ToolTip.visible: hovered && modelData.multiTargetOnly === true
                                            ToolTip.text: "仅多目标分类场景下启用有效"
                                        }

                                        Rectangle {
                                            width: 10; height: 10; radius: 2
                                            color: modelData.color || Tok.DesignTokens.accentSuccess
                                        }

                                        Label {
                                            text: modelData.cnName || modelData.name
                                            color: Tok.DesignTokens.textPrimary
                                            font.pixelSize: Tok.DesignTokens.fontSizeXs
                                            font.family: Tok.DesignTokens.fontFamilyCJK
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
                                            wrapMode: Text.WordWrap
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // 分隔线
            Rectangle {
                Layout.fillHeight: true
                width: 1
                color: Tok.DesignTokens.borderDefault
            }

            // ===== 右侧：实时预览 =====
            Rectangle {
                Layout.fillHeight: true
                Layout.fillWidth: true
                color: Tok.DesignTokens.bgPanel

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 4

                    Label {
                        text: "实时预览"
                        color: Tok.DesignTokens.textSecondary
                        font.pixelSize: Tok.DesignTokens.fontSizeSm
                        font.bold: true
                        font.family: Tok.DesignTokens.fontFamilyCJK
                    }

                    // 预览图像区域
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        color: "#1A1A1A"
                        border.color: Tok.DesignTokens.borderDefault
                        border.width: 1
                        radius: Tok.DesignTokens.radiusSm
                        clip: true

                        Image {
                            id: previewImage
                            anchors.fill: parent
                            anchors.margins: 2
                            source: tuner.previewSource
                            fillMode: Image.PreserveAspectFit
                            smooth: true
                            asynchronous: false  // v5.3：禁用异步加载，避免后台线程纹理与 QRhi 跨实例
                            cache: false  // 同一 URL 频繁更新，禁用缓存确保刷新
                        }

                        // 无图像占位
                        Label {
                            anchors.centerIn: parent
                            visible: tuner.previewSource.length === 0
                            text: tuner.hasInputImage ? "等待执行..." : "无输入图像"
                            color: Tok.DesignTokens.textPlaceholder
                            font.pixelSize: 12
                            font.family: Tok.DesignTokens.fontFamilyCJK
                        }
                    }

                    // 状态栏
                    Label {
                        text: tuner.statusMessage
                        color: tuner.statusMessage.indexOf("失败") >= 0
                               ? "#EF5350" : Tok.DesignTokens.textTertiary
                        font.pixelSize: 11
                        font.family: Tok.DesignTokens.fontFamilyCJK
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                }
            }
        }
        // 底部按钮栏
        Rectangle {
            Layout.fillWidth: true
            height: 52
            color: Tok.DesignTokens.bgHeader

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                anchors.topMargin: 8
                anchors.bottomMargin: 8
                spacing: 8

                Button {
                    text: "默认"
                    flat: true
                    enabled: tuner.tunableParams.length > 0
                    ToolTip.text: "恢复算子默认参数值"
                    ToolTip.visible: hovered
                    onClicked: tuner.resetToDefault()
                }
                Button {
                    text: "重置"
                    flat: true
                    enabled: tuner.tunableParams.length > 0
                    ToolTip.text: "恢复打开对话框时的参数值"
                    ToolTip.visible: hovered
                    onClicked: tuner.resetToSnapshot()
                }
                Item { Layout.fillWidth: true }
                // 立即执行按钮（手动触发，跳过防抖）
                Button {
                    text: "\u25B6 执行"
                    flat: true
                    enabled: tuner.tunableParams.length > 0 && (tuner.hasInputImage || tuner.isNoImageOperator) && !tuner.running
                    ToolTip.text: "立即执行一次（不等防抖）"
                    ToolTip.visible: hovered
                    onClicked: {
                        debounceTimer.stop()
                        tuner.executeCurrent()
                    }
                }
                Button {
                    text: "关闭"
                    flat: true
                    onClicked: tuner.close()
                }
                Button {
                    text: "应用"
                    highlighted: true
                    enabled: tuner.tunableParams.length > 0
                    ToolTip.text: "将当前参数写回程序节点并关闭"
                    ToolTip.visible: hovered
                    onClicked: tuner.applyAndClose()
                }
            }
        }
    }

    // ==================== 参数控件组件 ====================

    // v5.3 修复 ReadImage 输入源：专用文件路径选择组件
    Component {
        id: filePathFieldComp
        RowLayout {
            property var spec: null
            spacing: 6
            Layout.fillWidth: true
            height: 32

            Label {
                text: spec ? spec.cnName : "文件路径"
                color: Tok.DesignTokens.textPrimary
                font.pixelSize: 12
                font.family: Tok.DesignTokens.fontFamilyCJK
                Layout.preferredWidth: 100
            }

            TextField {
                id: pathField
                Layout.fillWidth: true
                placeholderText: spec ? (spec.help || "点击右侧按钮选择图像") : "点击右侧按钮选择图像"
                placeholderTextColor: Tok.DesignTokens.textPlaceholder
                text: spec ? (tuner.workingValues[spec.name] || "") : ""
                color: Tok.DesignTokens.textPrimary
                font.pixelSize: 12
                font.family: Tok.DesignTokens.fontMono
                selectByMouse: true
                persistentSelection: true
                background: Rectangle {
                    color: pathField.activeFocus ? Tok.DesignTokens.bgHover : Tok.DesignTokens.bgSurface
                    border.color: pathField.activeFocus ? Tok.DesignTokens.borderFocus : Tok.DesignTokens.borderDefault
                    border.width: 1
                    radius: Tok.DesignTokens.radiusSm
                }
                onEditingFinished: {
                    tuner.setValue(spec.name, text)
                }
            }

            Button {
                id: browseBtn
                text: "浏览…"
                Layout.preferredWidth: 60
                Layout.preferredHeight: 32
                font.pixelSize: 11
                font.family: Tok.DesignTokens.fontFamilyCJK
                background: Rectangle {
                    color: browseBtn.hovered ? Tok.DesignTokens.bgHover : Tok.DesignTokens.bgSurface
                    border.color: Tok.DesignTokens.borderDefault
                    border.width: 1
                    radius: Tok.DesignTokens.radiusSm
                }
                contentItem: Label {
                    text: browseBtn.text
                    color: Tok.DesignTokens.textPrimary
                    font: browseBtn.font
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                onClicked: fileDialog.open()
            }

            FileDialog {
                id: fileDialog
                title: "选择图像文件"
                fileMode: FileDialog.OpenFile
                nameFilters: [
                    "图像文件 (*.png *.jpg *.jpeg *.bmp *.tiff *.tif *.webp)",
                    "所有文件 (*)"
                ]
                currentFolder: {
                    var currentPath = spec ? (tuner.workingValues[spec.name] || "") : ""
                    if (currentPath && currentPath !== "") {
                        var idx = Math.max(currentPath.lastIndexOf("\\"),
                                           currentPath.lastIndexOf("/"))
                        if (idx > 0) {
                            var dir = currentPath.substring(0, idx)
                            return "file:///" + dir.replace(/\\/g, "/")
                        }
                    }
                    return "file:///" + Qt.resolvedUrl("~").toString().substring(8)
                }
                onAccepted: {
                    var picked = String(selectedFile)
                    if (picked.startsWith("file:///")) picked = picked.substring(8)
                    else if (picked.startsWith("file://")) picked = picked.substring(7)
                    pathField.text = picked
                    tuner.setValue(spec.name, picked)
                }
            }
        }
    }

    // Int 滑块（整数）
    Component {
        id: intSliderComp
        ColumnLayout {
            property var spec: null
            spacing: 2
            Layout.fillWidth: true

            // 标签 + 数值显示
            RowLayout {
                Layout.fillWidth: true
                spacing: 4
                Label {
                    text: spec ? spec.cnName + (spec.unit ? " (" + spec.unit + ")" : "") : ""
                    color: Tok.DesignTokens.textPrimary
                    font.pixelSize: 12
                    font.family: Tok.DesignTokens.fontFamilyCJK
                    Layout.fillWidth: true
                }
                Label {
                    id: intValueLabel
                    text: spec ? String(tuner.workingValues[spec.name] !== undefined
                                      ? tuner.workingValues[spec.name]
                                      : spec.defaultValue) : "0"
                    color: Tok.DesignTokens.accentPrimary
                    font.pixelSize: 12
                    font.bold: true
                    font.family: Tok.DesignTokens.fontFamilyCJK
                    Layout.preferredWidth: 60
                    horizontalAlignment: Text.AlignRight
                }
            }
            // 滑块
            Slider {
                id: intSlider
                Layout.fillWidth: true
                from: spec ? (spec.minValue !== undefined && spec.minValue !== null
                             ? spec.minValue : 0) : 0
                to: spec ? (spec.maxValue !== undefined && spec.maxValue !== null
                           ? spec.maxValue : 100) : 100
                stepSize: spec ? (spec.step !== undefined && spec.step !== null
                                 ? spec.step : 1) : 1
                value: spec ? (tuner.workingValues[spec.name] !== undefined
                              ? tuner.workingValues[spec.name]
                              : (spec.defaultValue || 0)) : 0
                snapMode: Slider.SnapOnRelease
                onMoved: {
                    var v = Math.round(value)
                    intValueLabel.text = String(v)
                    tuner.setValue(spec.name, v)
                }
            }
            // 范围提示
            Label {
                text: spec ? ("范围: " + (spec.minValue !== undefined ? spec.minValue : "—")
                            + " ~ " + (spec.maxValue !== undefined ? spec.maxValue : "—")
                            + (spec.step ? "  步长: " + spec.step : "")) : ""
                color: Tok.DesignTokens.textPlaceholder
                font.pixelSize: 10
                font.family: Tok.DesignTokens.fontFamilyCJK
            }
        }
    }

    // Float 滑块（浮点数）
    Component {
        id: floatSliderComp
        ColumnLayout {
            property var spec: null
            spacing: 2
            Layout.fillWidth: true

            RowLayout {
                Layout.fillWidth: true
                spacing: 4
                Label {
                    text: spec ? spec.cnName + (spec.unit ? " (" + spec.unit + ")" : "") : ""
                    color: Tok.DesignTokens.textPrimary
                    font.pixelSize: 12
                    font.family: Tok.DesignTokens.fontFamilyCJK
                    Layout.fillWidth: true
                }
                TextField {
                    id: floatInput
                    text: spec ? (tuner.workingValues[spec.name] !== undefined
                                 ? Number(tuner.workingValues[spec.name]).toString()
                                 : Number(spec.defaultValue).toString()) : "0"
                    color: Tok.DesignTokens.accentPrimary
                    font.pixelSize: 12
                    font.bold: true
                    font.family: Tok.DesignTokens.fontFamilyCJK
                    Layout.preferredWidth: 70
                    horizontalAlignment: Text.AlignRight
                    selectByMouse: true
                    padding: 2
                    background: Rectangle {
                        color: Tok.DesignTokens.bgSurface
                        border.color: floatInput.activeFocus
                                     ? Tok.DesignTokens.borderFocus
                                     : Tok.DesignTokens.borderDefault
                        border.width: 1
                        radius: 2
                    }
                    validator: DoubleValidator {
                        bottom: spec ? (spec.minValue !== undefined ? spec.minValue : -1e9) : -1e9
                        top: spec ? (spec.maxValue !== undefined ? spec.maxValue : 1e9) : 1e9
                        decimals: 4
                    }
                    onEditingFinished: {
                        var v = parseFloat(text)
                        if (!isNaN(v)) {
                            tuner.setValue(spec.name, v)
                        }
                    }
                }
            }
            Slider {
                id: floatSlider
                Layout.fillWidth: true
                from: spec ? (spec.minValue !== undefined && spec.minValue !== null
                             ? spec.minValue : 0) : 0
                to: spec ? (spec.maxValue !== undefined && spec.maxValue !== null
                           ? spec.maxValue : 1) : 1
                stepSize: spec ? (spec.step !== undefined && spec.step !== null
                                 ? spec.step : 0.01) : 0.01
                value: spec ? (tuner.workingValues[spec.name] !== undefined
                              ? tuner.workingValues[spec.name]
                              : (spec.defaultValue || 0)) : 0
                onMoved: {
                    var v = value
                    floatInput.text = Number(v).toString()
                    tuner.setValue(spec.name, v)
                }
            }
            Label {
                text: spec ? ("范围: " + (spec.minValue !== undefined ? spec.minValue : "—")
                            + " ~ " + (spec.maxValue !== undefined ? spec.maxValue : "—")
                            + (spec.step ? "  步长: " + spec.step : "")) : ""
                color: Tok.DesignTokens.textPlaceholder
                font.pixelSize: 10
                font.family: Tok.DesignTokens.fontFamilyCJK
            }
        }
    }

    // Enum 下拉框
    Component {
        id: enumComboComp
        RowLayout {
            id: enumRow
            property var spec: null
            spacing: 6
            Layout.fillWidth: true
            height: 32

            // v5.3.9 修复：Loader 在 onLoaded 中注入 spec，而 ComboBox 的
            // Component.onCompleted 早于 spec 注入，导致初始 currentIndex 为 0。
            // 在 spec 变化后主动同步一次，确保重开对话框时显示真实保存值。
            // v5.3.9-2 强化：用 Qt.callLater 延迟同步，避免 model 尚未填充时同步失败。
            onSpecChanged: {
                Qt.callLater(function() {
                    if (combo) combo.syncIndexFromValue()
                })
            }

            Label {
                text: spec ? spec.cnName : ""
                color: Tok.DesignTokens.textPrimary
                font.pixelSize: 12
                font.family: Tok.DesignTokens.fontFamilyCJK
                Layout.preferredWidth: 100
            }
            ComboBox {
                id: combo
                Layout.fillWidth: true
                model: spec ? (spec.options || []) : []
                // v5.3.7：去掉 currentIndex 绑定表达式，改命令式更新避免闪回
                property bool _userEditing: false

                function syncIndexFromValue() {
                    if (!spec) return
                    var dv = tuner.workingValues[spec.name] !== undefined
                            ? tuner.workingValues[spec.name]
                            : spec.defaultValue
                    var targetIdx = 0
                    if (spec.optionKeys && spec.optionKeys.length > 0) {
                        var idx = spec.optionKeys.indexOf(String(dv))
                        if (idx >= 0) targetIdx = idx
                    } else if (typeof dv === "number") {
                        if (dv >= 0 && dv < (spec.options ? spec.options.length : 0))
                            targetIdx = dv
                    }
                    if (combo.currentIndex !== targetIdx) {
                        combo.currentIndex = targetIdx
                    }
                }

                Component.onCompleted: syncIndexFromValue()
                // workingValues 变化时刷新（非用户编辑期间）
                Connections {
                    target: tuner
                    function onWorkingValuesChanged() {
                        if (!combo._userEditing) syncIndexFromValue()
                    }
                }

                background: Rectangle {
                    color: Tok.DesignTokens.bgSurface
                    border.color: Tok.DesignTokens.borderDefault
                    border.width: 1
                    radius: Tok.DesignTokens.radiusSm
                }
                contentItem: Label {
                    text: combo.displayText
                    color: Tok.DesignTokens.textPrimary
                    font.pixelSize: 12
                    font.family: Tok.DesignTokens.fontFamilyCJK
                    leftPadding: 8
                    verticalAlignment: Text.AlignVCenter
                }
                onActivated: function(index) {
                    // v5.3.7：使用 onActivated 的 index 参数，不依赖 currentIndex
                    combo._userEditing = true
                    var val
                    if (spec.optionKeys && spec.optionKeys.length > 0) {
                        val = spec.optionKeys[index]
                    } else {
                        val = index
                    }
                    tuner.setValue(spec.name, val)
                    Qt.callLater(function() { combo._userEditing = false })
                }
            }
        }
    }

    // Bool 勾选框
    Component {
        id: boolCheckComp
        RowLayout {
            property var spec: null
            spacing: 6
            Layout.fillWidth: true
            height: 32

            CheckBox {
                id: checkBox
                text: spec ? spec.cnName : ""
                checked: spec ? (tuner.workingValues[spec.name] !== undefined
                                ? tuner.workingValues[spec.name]
                                : (spec.defaultValue || false)) : false
                contentItem: Label {
                    text: checkBox.text
                    color: Tok.DesignTokens.textPrimary
                    font.pixelSize: 12
                    font.family: Tok.DesignTokens.fontFamilyCJK
                    leftPadding: checkBox.indicator.width + 6
                    verticalAlignment: Text.AlignVCenter
                }
                indicator: Rectangle {
                    implicitWidth: 18
                    implicitHeight: 18
                    x: checkBox.leftPadding
                    y: parent.height / 2 - height / 2
                    radius: Tok.DesignTokens.radiusSm
                    border.color: checkBox.checked ? Tok.DesignTokens.borderFocus : Tok.DesignTokens.borderDefault
                    color: checkBox.checked ? Tok.DesignTokens.borderFocus : Tok.DesignTokens.bgSurface
                }
                onToggled: tuner.setValue(spec.name, checked)
            }
            Item { Layout.fillWidth: true }
        }
    }
}
