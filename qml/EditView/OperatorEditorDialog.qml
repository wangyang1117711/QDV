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

    // 加载节点数据
    function load() {
        if (!bridge || !nodeId) return
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

            ParamForm {
                id: form
                width: formScroll.width
                params: dlg.meta ? dlg.meta.params : []
                currentValues: dlg.workingValues
                // P1-B4-H1 联动：传入算子类型供 isParamVisible 判断
                operatorType: dlg.meta ? dlg.meta.type : ""
                onValuesChanged: function(newValues) {
                    dlg.workingValues = JSON.parse(JSON.stringify(newValues))
                }
                onValidationError: function(name, message) {
                    console.warn("[OperatorEditorDialog] 校验失败:", name, message)
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
                        if (!runInput.required) {
                            // 数据源型算子（打开相机/采集图像等）无需输入图像，直接运行
                            runResultDialog.resultText = "运行中..."
                            runResultDialog.open()
                            dlg.bridge.runSingleOperatorAsync(dlg.nodeId, "")
                            return
                        }
                        // 智能识别输入源
                        var autoPath = runInput.path
                        if (autoPath && autoPath !== "") {
                            // v5.3.4：改为异步执行，避免阻塞 UI
                            runResultDialog.resultText = "运行中..."
                            runResultDialog.open()
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
            runResultDialog.resultText = "运行中..."
            runResultDialog.open()
            dlg.bridge.runSingleOperatorAsync(dlg.nodeId, path)
        }
    }

    // v2.5.0 功能 5c：运行结果摘要（v5.3 改为 Popup 避免 QRhi 跨实例）
    Popup {
        id: runResultDialog
        modal: true
        x: (parent.width - width) / 2
        y: (parent.height - height) / 2
        width: 400
        height: 200
        property string resultText: ""
        background: Rectangle {
            color: Tok.DesignTokens.bgPanel
            border.color: Tok.DesignTokens.accentPrimary
            border.width: 1
            radius: Tok.DesignTokens.radiusMd
        }
        contentItem: ColumnLayout {
            spacing: 10
            Label {
                text: "运行结果"
                color: Tok.DesignTokens.accentPrimary
                font.bold: true
                font.pixelSize: Tok.DesignTokens.fontSizeBase
                font.family: Tok.DesignTokens.fontFamilyCJK
            }
            Text {
                text: runResultDialog.resultText
                color: Tok.DesignTokens.textPrimary
                font.pixelSize: 12
                font.family: Tok.DesignTokens.fontFamilyCJK
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            Button {
                text: "确定"
                Layout.alignment: Qt.AlignRight
                onClicked: runResultDialog.close()
            }
        }
    }

    // v5.3.4：接收异步执行结果
    Connections {
        target: dlg.bridge
        function onSingleOperatorFinished(result) {
            if (!result || !result.success) {
                runResultDialog.resultText =
                    "运行失败：" + (result && result.error ? result.error : "未知错误")
            } else {
                runResultDialog.resultText =
                    "运行成功\n上游算子数：" + result.upstreamCount +
                    "\n总耗时：" + result.elapsedMs + " ms" +
                    "\n输出图像：" + (result.outputImagePath || "无")
            }
            runResultDialog.open()
        }
    }
}
