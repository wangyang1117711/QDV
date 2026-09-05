// =====================================================================
// ExportDialog.qml — 导出对话框
// 将标注结果导出为与 QDV AI 算子对接的格式：
//   yolo_detect / yolo_seg / classification / ocr / coco / qdvann
// =====================================================================
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QDVAnnotator 1.0 as Tok

Dialog {
    id: dlg
    title: "导出数据集"
    modal: true
    width: 460; height: 460
    // 无系统按钮条：操作按钮已内置于 contentItem 底部，始终可见

    property string lastMsg: ""
    // S1：导出格式下拉由能力注册表 exports 驱动
    property var fmtModel: []
    function buildFmtModel() {
        var arr = []
        var list = capabilities.exports
        for (var i = 0; i < list.length; i++)
            arr.push({ text: list[i].label || list[i].key, val: list[i].key })
        return arr
    }
    Component.onCompleted: fmtModel = buildFmtModel()

    FolderDialog {
        id: folderDlg
        title: "选择导出目录"
        onAccepted: outPath.text = folderDlg.selectedFolder.toString().replace("file:///", "")
    }
    FileDialog {
        id: fileDlg
        title: "保存工程文件 (.qdvann)"
        fileMode: FileDialog.SaveFile
        nameFilters: ["QDV 标注工程 (*.qdvann)"]
        onAccepted: outPath.text = fileDlg.selectedFile.toString().replace("file:///", "")
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: Tok.DesignTokens.space3

        // 可选内容区：内容超出时独立滚动，不再挤压/遮挡底部操作按钮
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            ColumnLayout {
                width: parent.width
                spacing: Tok.DesignTokens.space3

        // 格式选择
        Label { text: "导出格式"; color: Tok.DesignTokens.textSecondary; font.family: Tok.DesignTokens.fontFamilyCJK }
        Tok.WideComboBox {
            id: fmt
            Layout.fillWidth: true
            textRole: "text"
            valueRole: "val"
            model: dlg.fmtModel
            onCurrentIndexChanged: updateOutPrompt()
            Component.onCompleted: updateOutPrompt()
            function updateOutPrompt() {
                        outLabel.text = (fmt.currentValue === "qdvann") ? "保存为 (.qdvann)" : "导出到目录"
                    }
        }

        // 输出路径
        RowLayout {
            Layout.fillWidth: true
            Label { id: outLabel; text: "导出到目录"; color: Tok.DesignTokens.textSecondary; font.family: Tok.DesignTokens.fontFamilyCJK }
            TextField {
                id: outPath
                Layout.fillWidth: true
                placeholderText: "选择输出位置"
                color: Tok.DesignTokens.textPrimary
                font.family: Tok.DesignTokens.fontFamilyCJK
                background: Rectangle { color: Tok.DesignTokens.bgSurface; border.color: Tok.DesignTokens.borderDefault; radius: 2 }
                readOnly: true
            }
            Button {
                text: "浏览"
                onClicked: (fmt.currentValue === "qdvann") ? fileDlg.open() : folderDlg.open()
            }
        }

        // 选项
        GridLayout {
            columns: 2
            Layout.fillWidth: true
            rowSpacing: 8; columnSpacing: 8

            Label { text: "训练/验证拆分"; color: Tok.DesignTokens.textSecondary; font.family: Tok.DesignTokens.fontFamilyCJK }
            TextField {
                id: valRatio
                text: "0.2"; Layout.fillWidth: true
                color: Tok.DesignTokens.textPrimary
                font.family: Tok.DesignTokens.fontFamilyCJK
                background: Rectangle { color: Tok.DesignTokens.bgSurface; border.color: Tok.DesignTokens.borderDefault; radius: 2 }
            }
            Label { text: "随机种子"; color: Tok.DesignTokens.textSecondary; font.family: Tok.DesignTokens.fontFamilyCJK }
            TextField {
                id: seed
                text: "12345"; Layout.fillWidth: true
                color: Tok.DesignTokens.textPrimary
                font.family: Tok.DesignTokens.fontFamilyCJK
                background: Rectangle { color: Tok.DesignTokens.bgSurface; border.color: Tok.DesignTokens.borderDefault; radius: 2 }
            }
            Label { text: "复制图像(否则仅引用)"; color: Tok.DesignTokens.textSecondary; font.family: Tok.DesignTokens.fontFamilyCJK }
            CheckBox { id: copyImgs; checked: true }
            Label { text: "生成分割掩膜"; color: Tok.DesignTokens.textSecondary; font.family: Tok.DesignTokens.fontFamilyCJK }
            CheckBox { id: withMask; checked: true }
        }

        Label {
            id: status
            Layout.fillWidth: true
            text: dlg.lastMsg
            color: statusOk ? Tok.DesignTokens.accentSuccess : Tok.DesignTokens.accentError
            font.family: Tok.DesignTokens.fontFamilyCJK
            font.pixelSize: Tok.DesignTokens.fontSizeSm
            wrapMode: Text.WordWrap
            property bool statusOk: true
        }
            }
        }

        // 底部固定操作按钮：始终可见，不随内容滚动/溢出而隐藏
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            Button {
                text: "关闭"
                Layout.preferredWidth: 80
                onClicked: dlg.close()
            }
            Button {
                text: "开始导出"
                highlighted: true
                font.bold: true
                Layout.preferredWidth: 96
                onClicked: doExport()
            }
        }
    }

    // 导出执行入口（校验 + 调用 session.exportDataset + 回显结果）
    function doExport() {
        if (!outPath.text) { dlg.lastMsg = "请先选择输出位置"; status.statusOk = false; return }
        var opt = {
            valRatio: parseFloat(valRatio.text) || 0.2,
            seed: parseInt(seed.text) || 12345,
            copyImages: copyImgs.checked,
            withMasks: withMask.checked
        }
        var ok = session.exportDataset(fmt.currentValue, outPath.text, opt)
        if (ok) {
            dlg.lastMsg = "导出成功：" + outPath.text
            status.statusOk = true
        } else {
            dlg.lastMsg = "导出失败：" + session.lastError
            status.statusOk = false
        }
    }

    function openFor() { dlg.lastMsg = ""; dlg.open() }
}
