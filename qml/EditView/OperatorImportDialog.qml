// =====================================================================
// OperatorImportDialog.qml — 算子导入对话框（v2.7.0 O1a）
//
// 功能：
// - 4 分区布局：选文件 / 预览 / 校验结果 / 按钮
// - FileDialog 过滤 *.qdvop
// - 选文件后自动调 operatorLibraryBridge.requestPreview(path)
// - 冲突（红，阻断）/ 警告（橙，可忽略）三色分区显示
// - 提交按钮在 conflicts 非空时禁用
// - 提交成功 → toast + close
//
// 入口：
//   var dlg = operatorImportDialogComponent.createObject(root, {
//       bridge: bridge
//   })
//   dlg.open()
// =====================================================================

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import QtQuick.Dialogs
import QDV.EditView 3.0 as Tok

Popup {
    id: dlg
    modal: true
    width: 580
    height: 680
    padding: 0
    x: (parent.width - width) / 2
    y: (parent.height - height) / 2
    background: Rectangle {
        color: Tok.DesignTokens.bgPanel
        border.color: Tok.DesignTokens.accentPrimary
        border.width: 1
        radius: Tok.DesignTokens.radiusLg
    }

    // ============ 外部接口 ============
    property var bridge  // EditViewBridge

    // 内部状态
    property var preview: null        // requestPreview 返回的结果
    property string selectedPath: ""  // 当前选择的文件路径
    property bool hasConflicts: false // 是否有冲突（阻断提交）
    property bool hasWarnings: false  // 是否有警告

    // ============ 标题栏 ============
    Rectangle {
        id: header
        anchors.top: parent.top
        width: parent.width
        height: Tok.DesignTokens.headerBarHeight
        color: Tok.DesignTokens.bgHeader
        radius: Tok.DesignTokens.radiusLg

        Text {
            anchors.centerIn: parent
            text: "导入算子"
            color: Tok.DesignTokens.textPrimary
            font.pixelSize: Tok.DesignTokens.fontSizeXl
            font.bold: true
            font.family: Tok.DesignTokens.fontFamily
        }

        Button {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.rightMargin: Tok.DesignTokens.space2
            flat: true
            text: "×"
            font.pixelSize: 18
            onClicked: dlg.close()
            palette.buttonText: Tok.DesignTokens.textTertiary
        }
    }

    // ============ 内容区 ============
    ScrollView {
        id: scroll
        anchors.top: header.bottom
        anchors.bottom: footer.top
        width: parent.width
        clip: true
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

        ColumnLayout {
            width: scroll.width
            spacing: Tok.DesignTokens.space4

            // --- 分区 1：选择文件 ---
            RowLayout {
                Layout.leftMargin: Tok.DesignTokens.space4
                Layout.rightMargin: Tok.DesignTokens.space4
                Layout.topMargin: Tok.DesignTokens.space4
                spacing: Tok.DesignTokens.space2

                Rectangle {
                    width: 18; height: 18
                    radius: 9
                    color: Tok.DesignTokens.accentPrimary
                    Text {
                        anchors.centerIn: parent
                        text: "1"
                        color: "white"
                        font.pixelSize: 10
                        font.bold: true
                    }
                }
                Text {
                    text: "选择算子定义文件"
                    color: Tok.DesignTokens.textPrimary
                    font.pixelSize: Tok.DesignTokens.fontSizeBase
                    font.bold: true
                    font.family: Tok.DesignTokens.fontFamily
                }
            }

            RowLayout {
                Layout.leftMargin: Tok.DesignTokens.space4
                Layout.rightMargin: Tok.DesignTokens.space4
                spacing: Tok.DesignTokens.space2

                // 文件路径显示
                Rectangle {
                    Layout.fillWidth: true
                    height: Tok.DesignTokens.controlHeight
                    color: Tok.DesignTokens.bgSurface
                    border.color: Tok.DesignTokens.borderDefault
                    radius: Tok.DesignTokens.radiusMd

                    Text {
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.leftMargin: Tok.DesignTokens.space3
                        anchors.rightMargin: Tok.DesignTokens.space3
                        text: selectedPath === "" ? "请选择 .qdvop 文件…" : selectedPath
                        color: selectedPath === "" ? Tok.DesignTokens.textPlaceholder : Tok.DesignTokens.textSecondary
                        font.pixelSize: Tok.DesignTokens.fontSizeSm
                        font.family: Tok.DesignTokens.fontMono
                        elide: Text.ElideMiddle
                        width: parent.width - Tok.DesignTokens.space3 * 2
                    }
                }

                // 浏览按钮
                Button {
                    text: "浏览…"
                    implicitHeight: Tok.DesignTokens.controlHeight
                    onClicked: fileDialog.open()
                    palette.buttonText: Tok.DesignTokens.textSecondary
                    background: Rectangle {
                        color: parent.hovered ? Tok.DesignTokens.bgHover : Tok.DesignTokens.bgSurface
                        border.color: Tok.DesignTokens.borderDefault
                        radius: Tok.DesignTokens.radiusMd
                    }
                }
            }

            // --- 分区 2：预览 ---
            RowLayout {
                Layout.leftMargin: Tok.DesignTokens.space4
                Layout.rightMargin: Tok.DesignTokens.space4
                Layout.topMargin: Tok.DesignTokens.space3
                spacing: Tok.DesignTokens.space2

                Rectangle {
                    width: 18; height: 18
                    radius: 9
                    color: Tok.DesignTokens.accentPrimary
                    Text {
                        anchors.centerIn: parent
                        text: "2"
                        color: "white"
                        font.pixelSize: 10
                        font.bold: true
                    }
                }
                Text {
                    text: "算子预览"
                    color: Tok.DesignTokens.textPrimary
                    font.pixelSize: Tok.DesignTokens.fontSizeBase
                    font.bold: true
                    font.family: Tok.DesignTokens.fontFamily
                }
            }

            // 预览内容卡片
            Rectangle {
                Layout.leftMargin: Tok.DesignTokens.space4
                Layout.rightMargin: Tok.DesignTokens.space4
                Layout.fillWidth: true
                Layout.minimumHeight: preview ? 160 : 60
                color: Tok.DesignTokens.bgSurface
                border.color: Tok.DesignTokens.borderDefault
                radius: Tok.DesignTokens.radiusMd

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Tok.DesignTokens.space3
                    spacing: Tok.DesignTokens.space1

                    // 空状态
                    Text {
                        visible: !preview
                        text: "选择文件后自动预览"
                        color: Tok.DesignTokens.textTertiary
                        font.pixelSize: Tok.DesignTokens.fontSizeSm
                        font.family: Tok.DesignTokens.fontFamily
                        Layout.alignment: Qt.AlignHCenter
                    }

                    // 算子名称
                    Text {
                        visible: preview
                        text: preview ? preview.def.cnName : ""
                        color: Tok.DesignTokens.textPrimary
                        font.pixelSize: Tok.DesignTokens.fontSizeLg
                        font.bold: true
                        font.family: Tok.DesignTokens.fontFamily
                    }

                    // 算子 type
                    Text {
                        visible: preview
                        text: preview ? "type: " + preview.def.type : ""
                        color: Tok.DesignTokens.accentPrimary
                        font.pixelSize: Tok.DesignTokens.fontSizeXs
                        font.family: Tok.DesignTokens.fontMono
                    }

                    // 元信息
                    GridLayout {
                        visible: preview
                        columns: 2
                        columnSpacing: Tok.DesignTokens.space2
                        rowSpacing: 2

                        Text { text: "分类"; color: Tok.DesignTokens.textTertiary; font.pixelSize: Tok.DesignTokens.fontSizeSm; font.family: Tok.DesignTokens.fontFamily }
                        Text { text: preview ? preview.def.category : ""; color: Tok.DesignTokens.textSecondary; font.pixelSize: Tok.DesignTokens.fontSizeSm; font.family: Tok.DesignTokens.fontFamily }

                        Text { text: "版本"; color: Tok.DesignTokens.textTertiary; font.pixelSize: Tok.DesignTokens.fontSizeSm; font.family: Tok.DesignTokens.fontFamily }
                        Text { text: preview ? preview.def.version : ""; color: Tok.DesignTokens.textSecondary; font.pixelSize: Tok.DesignTokens.fontSizeSm; font.family: Tok.DesignTokens.fontMono }

                        Text { text: "作者"; color: Tok.DesignTokens.textTertiary; font.pixelSize: Tok.DesignTokens.fontSizeSm; font.family: Tok.DesignTokens.fontFamily }
                        Text { text: preview ? preview.def.author : ""; color: Tok.DesignTokens.textSecondary; font.pixelSize: Tok.DesignTokens.fontSizeSm; font.family: Tok.DesignTokens.fontFamily }
                    }

                    // 输入端口
                    Text {
                        visible: preview && preview.def.inputs && preview.def.inputs.length > 0
                        text: "输入端口"
                        color: Tok.DesignTokens.textTertiary
                        font.pixelSize: Tok.DesignTokens.fontSizeXs
                        font.family: Tok.DesignTokens.fontFamily
                        Layout.topMargin: Tok.DesignTokens.space2
                    }
                    Repeater {
                        model: preview && preview.def.inputs ? preview.def.inputs : []
                        Text {
                            Layout.leftMargin: Tok.DesignTokens.space2
                            text: "• " + modelData.cnName + " (" + modelData.type + ")" + (modelData.required ? " *" : "")
                            color: Tok.DesignTokens.accentWarning
                            font.pixelSize: Tok.DesignTokens.fontSizeXs
                            font.family: Tok.DesignTokens.fontFamily
                        }
                    }

                    // 输出端口
                    Text {
                        visible: preview && preview.def.outputs && preview.def.outputs.length > 0
                        text: "输出端口"
                        color: Tok.DesignTokens.textTertiary
                        font.pixelSize: Tok.DesignTokens.fontSizeXs
                        font.family: Tok.DesignTokens.fontFamily
                    }
                    Repeater {
                        model: preview && preview.def.outputs ? preview.def.outputs : []
                        Text {
                            Layout.leftMargin: Tok.DesignTokens.space2
                            text: "• " + modelData.cnName + " (" + modelData.type + ")"
                            color: Tok.DesignTokens.accentSuccess
                            font.pixelSize: Tok.DesignTokens.fontSizeXs
                            font.family: Tok.DesignTokens.fontFamily
                        }
                    }
                }
            }

            // --- 分区 3：校验结果 ---
            RowLayout {
                Layout.leftMargin: Tok.DesignTokens.space4
                Layout.rightMargin: Tok.DesignTokens.space4
                Layout.topMargin: Tok.DesignTokens.space3
                spacing: Tok.DesignTokens.space2

                Rectangle {
                    width: 18; height: 18
                    radius: 9
                    color: Tok.DesignTokens.accentPrimary
                    Text {
                        anchors.centerIn: parent
                        text: "3"
                        color: "white"
                        font.pixelSize: 10
                        font.bold: true
                    }
                }
                Text {
                    text: "校验结果"
                    color: Tok.DesignTokens.textPrimary
                    font.pixelSize: Tok.DesignTokens.fontSizeBase
                    font.bold: true
                    font.family: Tok.DesignTokens.fontFamily
                }
            }

            // 冲突列表（红色）
            Rectangle {
                Layout.leftMargin: Tok.DesignTokens.space4
                Layout.rightMargin: Tok.DesignTokens.space4
                Layout.fillWidth: true
                Layout.minimumHeight: hasConflicts ? conflictsCol.height + Tok.DesignTokens.space3 * 2 : 0
                visible: hasConflicts
                color: Qt.rgba(1, 0.32, 0.32, 0.08)
                border.color: Qt.rgba(1, 0.32, 0.32, 0.3)
                radius: Tok.DesignTokens.radiusMd

                ColumnLayout {
                    id: conflictsCol
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.margins: Tok.DesignTokens.space3
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 2

                    Repeater {
                        model: preview && preview.conflicts ? preview.conflicts : []
                        Text {
                            text: "✗ [" + modelData.field + "] " + modelData.message
                            color: Tok.DesignTokens.accentError
                            font.pixelSize: Tok.DesignTokens.fontSizeSm
                            font.family: Tok.DesignTokens.fontFamily
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                        }
                    }
                }
            }

            // 警告列表（橙色）
            Rectangle {
                Layout.leftMargin: Tok.DesignTokens.space4
                Layout.rightMargin: Tok.DesignTokens.space4
                Layout.fillWidth: true
                Layout.minimumHeight: hasWarnings ? warningsCol.height + Tok.DesignTokens.space3 * 2 : 0
                visible: hasWarnings
                color: Qt.rgba(1, 0.84, 0.25, 0.08)
                border.color: Qt.rgba(1, 0.84, 0.25, 0.3)
                radius: Tok.DesignTokens.radiusMd

                ColumnLayout {
                    id: warningsCol
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.margins: Tok.DesignTokens.space3
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 2

                    Repeater {
                        model: preview && preview.warnings ? preview.warnings : []
                        Text {
                            text: "⚠ [" + modelData.field + "] " + modelData.message
                            color: Tok.DesignTokens.accentWarning
                            font.pixelSize: Tok.DesignTokens.fontSizeSm
                            font.family: Tok.DesignTokens.fontFamily
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                        }
                    }
                }
            }

            // 成功提示（无冲突无警告）
            Rectangle {
                Layout.leftMargin: Tok.DesignTokens.space4
                Layout.rightMargin: Tok.DesignTokens.space4
                Layout.fillWidth: true
                height: 32
                visible: preview && !hasConflicts && !hasWarnings
                color: Qt.rgba(0.41, 0.94, 0.68, 0.08)
                border.color: Qt.rgba(0.41, 0.94, 0.68, 0.3)
                radius: Tok.DesignTokens.radiusMd

                Text {
                    anchors.centerIn: parent
                    text: "✓ 校验通过，可以导入"
                    color: Tok.DesignTokens.accentSuccess
                    font.pixelSize: Tok.DesignTokens.fontSizeSm
                    font.family: Tok.DesignTokens.fontFamily
                }
            }
        }
    }

    // ============ 底部按钮区 ============
    Rectangle {
        id: footer
        anchors.bottom: parent.bottom
        width: parent.width
        height: 52
        color: Tok.DesignTokens.bgHeader
        border.color: Tok.DesignTokens.borderDefault

        RowLayout {
            anchors.fill: parent
            anchors.margins: Tok.DesignTokens.space3
            spacing: Tok.DesignTokens.space2

            Item { Layout.fillWidth: true }

            // 取消按钮
            Button {
                text: "取消"
                implicitHeight: Tok.DesignTokens.controlHeight
                onClicked: dlg.close()
                palette.buttonText: Tok.DesignTokens.textSecondary
                background: Rectangle {
                    color: parent.hovered ? Tok.DesignTokens.bgHover : Tok.DesignTokens.bgSurface
                    border.color: Tok.DesignTokens.borderDefault
                    radius: Tok.DesignTokens.radiusMd
                }
            }

            // 提交导入按钮（conflicts 非空时禁用）
            Button {
                text: hasConflicts ? "请先解决冲突" : "提交导入"
                implicitHeight: Tok.DesignTokens.controlHeight
                enabled: preview && !hasConflicts
                onClicked: doCommit()
                palette.buttonText: "white"
                background: Rectangle {
                    color: {
                        if (!enabled) return "#2A2A2A"
                        return parent.hovered ? Tok.DesignTokens.accentPrimaryHover : Tok.DesignTokens.accentPrimary
                    }
                    border.color: enabled ? Tok.DesignTokens.accentPrimary : Tok.DesignTokens.borderDefault
                    radius: Tok.DesignTokens.radiusMd
                }
            }
        }
    }

    // ============ FileDialog ============
    FileDialog {
        id: fileDialog
        title: "选择算子定义文件"
        nameFilters: ["算子定义文件 (*.qdvop)", "所有文件 (*)"]
        fileMode: FileDialog.OpenFile
        onAccepted: {
            selectedPath = selectedFile.toString().replace("file:///", "")
            doPreview()
        }
    }

    // ============ 逻辑函数 ============

    // 请求预览
    function doPreview() {
        if (!bridge || !selectedPath) return
        var olBridge = bridge.operatorLibraryBridge
        if (!olBridge) {
            console.warn("[OperatorImportDialog] operatorLibraryBridge 不可用")
            return
        }
        preview = olBridge.requestPreview(selectedPath)
        hasConflicts = preview && preview.conflicts && preview.conflicts.length > 0
        hasWarnings = preview && preview.warnings && preview.warnings.length > 0
    }

    // 提交导入
    function doCommit() {
        if (!preview) return
        var olBridge = bridge.operatorLibraryBridge
        if (!olBridge) return

        var ok = olBridge.commitImport(preview)
        if (ok) {
            // 成功：关闭对话框
            dlg.close()
            // 通知父组件显示 toast（通过信号或直接调 showToast）
            if (bridge.showToast) {
                bridge.showToast("算子导入成功：" + (preview.def ? preview.def.cnName : ""))
            }
        }
        // 失败时 importFailed 信号会触发，由父组件处理 toast
    }

    // 监听 importFailed 信号
    Connections {
        target: bridge && bridge.operatorLibraryBridge ? bridge.operatorLibraryBridge : null
        function onImportFailed(message) {
            if (bridge && bridge.showToast) {
                bridge.showToast("导入失败：" + message)
            }
        }
    }

    // 打开时重置状态
    onOpened: {
        preview = null
        selectedPath = ""
        hasConflicts = false
        hasWarnings = false
    }
}
