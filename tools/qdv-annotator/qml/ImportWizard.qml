// =====================================================================
// ImportWizard.qml — 数据导入向导
//  - 导入图像文件夹（可同时建立初始标签）
//  - 打开 QDV 原生工程 (.qdvann)
//  - 导入 COCO（Label Studio / CVAT 导出互通）
// =====================================================================
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QDVAnnotator 1.0 as Tok

Dialog {
    id: dlg
    title: "导入数据"
    modal: true
    width: 440; height: 360
    standardButtons: Dialog.Close

    FolderDialog { id: folderDlg; title: "选择图像文件夹"; onAccepted: doFolder() }
    FileDialog {
        id: projDlg
        title: "打开工程"
        nameFilters: ["QDV 标注工程 (*.qdvann)", "所有文件 (*)"]
        onAccepted: {
            if (!session.openProject(selectedFile.toString().replace("file:///", "")))
                msg.text = "打开失败：" + session.lastError
            else { msg.text = "已打开工程"; dlg.close() }
        }
    }
    FileDialog {
        id: cocoDlg
        title: "导入 COCO"
        nameFilters: ["COCO JSON (*.json)", "所有文件 (*)"]
        onAccepted: {
            if (!session.importCoco(selectedFile.toString().replace("file:///", "")))
                msg.text = "导入失败：" + session.lastError
            else { msg.text = "COCO 导入完成"; dlg.close() }
        }
    }

    function doFolder() {
        var dir = folderDlg.selectedFolder.toString().replace("file:///", "")
        if (session.loadImageFolder(dir)) {
            var raw = initLabels.text.trim()
            if (raw) raw.split(/[,，]/).forEach(function (s) { var n = s.trim(); if (n) session.addLabel(n) })
            msg.text = "已导入 " + session.images.length + " 张图像"
            dlg.close()
        } else {
            msg.text = "导入失败：" + session.lastError
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: Tok.DesignTokens.space3
        Label { text: "1) 导入图像文件夹"; color: Tok.DesignTokens.textPrimary; font.bold: true; font.family: Tok.DesignTokens.fontFamilyCJK }
        TextField {
            id: initLabels
            Layout.fillWidth: true
            placeholderText: "导入后建立的初始标签，逗号分隔（如 缺陷,良品）"
            color: Tok.DesignTokens.textPrimary
            font.family: Tok.DesignTokens.fontFamilyCJK
            background: Rectangle { color: Tok.DesignTokens.bgSurface; border.color: Tok.DesignTokens.borderDefault; radius: 2 }
        }
        Button { text: "选择文件夹并导入"; Layout.fillWidth: true; onClicked: folderDlg.open() }

        Label { text: "2) 打开 QDV 工程 (.qdvann)"; color: Tok.DesignTokens.textPrimary; font.bold: true; font.family: Tok.DesignTokens.fontFamilyCJK }
        Button { text: "打开工程"; Layout.fillWidth: true; onClicked: projDlg.open() }

        Label { text: "3) 从 COCO 导入 (Label Studio / CVAT)"; color: Tok.DesignTokens.textPrimary; font.bold: true; font.family: Tok.DesignTokens.fontFamilyCJK }
        Button { text: "导入 COCO JSON"; Layout.fillWidth: true; onClicked: cocoDlg.open() }

        Label { id: msg; Layout.fillWidth: true; color: Tok.DesignTokens.accentInfo; font.family: Tok.DesignTokens.fontFamilyCJK; font.pixelSize: Tok.DesignTokens.fontSizeSm; wrapMode: Text.WordWrap }
        Item { Layout.fillHeight: true }
    }

    function openFor() { dlg.open() }
}
