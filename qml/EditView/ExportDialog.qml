// =====================================================================
// ExportDialog.qml — 算子流程导出对话框
//
// 功能：
// - 格式选择（DLL/EXE/Python 多选）
// - 导出名称/接口名称/输出路径配置
// - 方案嵌入/外置选择
// - 版本/作者/描述元信息
// - 可选自检
// - 导出进度显示
//
// 入口：
//   bridge.exportScheme({ exportName, interfaceName, outputPath, ... })
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
    width: 520
    height: 580
    padding: 0
    x: (parent.width - width) / 2
    y: (parent.height - height) / 2
    background: Rectangle {
        color: Tok.DesignTokens.bgPanel
        border.color: Tok.DesignTokens.accentPrimary
        border.width: 1
        radius: Tok.DesignTokens.radiusMd
    }

    // ============ 外部属性 ============
    property var bridge: null   // EditViewBridge 实例

    // ============ 内部状态 ============
    property bool exporting: false
    property int progress: 0

    // ============ 布局 ============
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 12

        // 标题
        Text {
            text: "导出算子流程"
            color: Tok.DesignTokens.textPrimary
            font.pixelSize: 18
            font.bold: true
            Layout.fillWidth: true
        }

        // 格式选择
        Text {
            text: "导出格式"
            color: Tok.DesignTokens.textSecondary
            font.pixelSize: 12
        }
        RowLayout {
            spacing: 16
            CheckBox {
                id: cbDll
                text: "DLL"
                checked: true
            }
            CheckBox {
                id: cbExe
                text: "EXE"
                checked: true
            }
            CheckBox {
                id: cbPython
                text: "Python"
                checked: true
            }
        }

        // 导出名称
        RowLayout {
            spacing: 8
            Text {
                text: "导出名称"
                color: Tok.DesignTokens.textSecondary
                font.pixelSize: 12
                Layout.preferredWidth: 80
            }
            TextField {
                id: tfExportName
                Layout.fillWidth: true
                text: "MyPipeline"
                placeholderText: "导出包目录名"
            }
        }

        // 接口名称
        RowLayout {
            spacing: 8
            Text {
                text: "接口名称"
                color: Tok.DesignTokens.textSecondary
                font.pixelSize: 12
                Layout.preferredWidth: 80
            }
            TextField {
                id: tfInterfaceName
                Layout.fillWidth: true
                text: "MyPipeline"
                placeholderText: "Python 类名 / 文档命名"
            }
        }

        // 输出路径
        RowLayout {
            spacing: 8
            Text {
                text: "输出路径"
                color: Tok.DesignTokens.textSecondary
                font.pixelSize: 12
                Layout.preferredWidth: 80
            }
            TextField {
                id: tfOutputPath
                Layout.fillWidth: true
                text: "D:/exports"
                placeholderText: "D:/exports"
            }
            Button {
                text: "浏览"
                onClicked: folderDialog.open()
            }
        }

        // 方案嵌入/外置
        RowLayout {
            spacing: 16
            Text {
                text: "方案"
                color: Tok.DesignTokens.textSecondary
                font.pixelSize: 12
                Layout.preferredWidth: 80
            }
            RadioButton {
                id: rbEmbed
                text: "嵌入 DLL/EXE"
                checked: true
            }
            RadioButton {
                id: rbExternal
                text: "外置文件"
            }
        }

        // 版本与作者
        RowLayout {
            spacing: 8
            Text {
                text: "版本"
                color: Tok.DesignTokens.textSecondary
                font.pixelSize: 12
                Layout.preferredWidth: 80
            }
            TextField {
                id: tfVersion
                Layout.preferredWidth: 100
                text: "1.0.0"
            }
            Text {
                text: "作者"
                color: Tok.DesignTokens.textSecondary
                font.pixelSize: 12
                Layout.leftMargin: 20
            }
            TextField {
                id: tfAuthor
                Layout.fillWidth: true
                placeholderText: "作者名"
            }
        }

        // 描述
        RowLayout {
            spacing: 8
            Text {
                text: "描述"
                color: Tok.DesignTokens.textSecondary
                font.pixelSize: 12
                Layout.preferredWidth: 80
            }
            TextField {
                id: tfDescription
                Layout.fillWidth: true
                placeholderText: "流程描述（可选）"
            }
        }

        // 自检按钮
        Button {
            text: "运行自检"
            enabled: !exporting
            onClicked: {
                if (bridge) {
                    var r = bridge.runExportSelfCheck("")
                    if (r.passed) {
                        toastText.text = "自检通过"
                    } else {
                        toastText.text = "自检未通过（不阻断导出）"
                    }
                    toastAnim.restart()
                }
            }
        }

        // 进度条
        ProgressBar {
            id: progressBar
            Layout.fillWidth: true
            visible: exporting || progress > 0
            value: progress / 100.0
        }

        Text {
            id: progressText
            text: progress > 0 ? "进度: " + progress + "%" : ""
            color: Tok.DesignTokens.textSecondary
            font.pixelSize: 11
            visible: exporting || progress > 0
        }

        Item { Layout.fillHeight: true }

        // 按钮栏
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            Button {
                text: "取消"
                enabled: !exporting
                onClicked: dlg.close()
            }
            Button {
                text: exporting ? "导出中..." : "导出"
                enabled: !exporting
                highlighted: true
                onClicked: doExport()
            }
        }
    }

    // Toast 提示
    Text {
        id: toastText
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottomMargin: 20
        color: Tok.DesignTokens.textPrimary
        font.pixelSize: 12
        opacity: 0
        SequentialAnimation on opacity {
            id: toastAnim
            running: false
            NumberAnimation { from: 0; to: 1; duration: 200 }
            PauseAnimation { duration: 2000 }
            NumberAnimation { from: 1; to: 0; duration: 200 }
        }
    }

    // ============ 文件夹选择对话框 ============
    FolderDialog {
        id: folderDialog
        title: "选择输出目录"
        onAccepted: {
            tfOutputPath.text = currentFolder.toString().replace("file:///", "")
        }
    }

    // ============ 导出逻辑 ============
    function doExport() {
        if (!bridge) return

        // 校验
        if (tfExportName.text.length === 0) {
            toastText.text = "请填写导出名称"
            toastAnim.restart()
            return
        }
        if (!cbDll.checked && !cbExe.checked && !cbPython.checked) {
            toastText.text = "至少选择一种格式"
            toastAnim.restart()
            return
        }

        // C 盘警告
        if (tfOutputPath.text.startsWith("C:/") || tfOutputPath.text.startsWith("C:\\")) {
            toastText.text = "警告：输出路径在 C 盘，建议改 D 盘"
            toastAnim.restart()
        }

        exporting = true
        progress = 0

        var config = {
            exportName: tfExportName.text,
            interfaceName: tfInterfaceName.text,
            outputPath: tfOutputPath.text,
            exportDll: cbDll.checked,
            exportExe: cbExe.checked,
            exportPython: cbPython.checked,
            embedScheme: rbEmbed.checked,
            generateDoc: true,
            generateExamples: true,
            version: tfVersion.text,
            author: tfAuthor.text,
            description: tfDescription.text
        }

        bridge.exportScheme(config)
    }

    // ============ 信号监听 ============
    Connections {
        target: bridge
        function onExportProgress(percent) {
            progress = percent
        }
        function onExportFinished(success, message) {
            exporting = false
            if (success) {
                toastText.text = "导出完成: " + message
                progress = 100
            } else {
                toastText.text = "导出失败: " + message
                progress = 0
            }
            toastAnim.restart()
        }
    }
}
