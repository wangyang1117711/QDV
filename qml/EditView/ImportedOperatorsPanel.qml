// =====================================================================
// ImportedOperatorsPanel.qml — 已导入算子面板（v2.7.0 O1a）
//
// 功能：
// - 顶部搜索框
// - ListView 列表：name + version + category + 删除按钮
// - 删除按钮 → 二次确认 → operatorLibraryBridge.removeImported(type)
// - 底部说明文字
//
// 入口：
//   var panel = importedOperatorsPanelComponent.createObject(root, {
//       bridge: bridge
//   })
//   panel.open()
// =====================================================================

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import QDV.EditView 3.0 as Tok

Popup {
    id: dlg
    modal: true
    width: 420
    height: 560
    padding: 0
    x: parent.width - width - 20
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
    property var importedList: []   // 已导入算子列表
    property string keyword: ""     // 搜索关键词

    // ============ 标题栏 ============
    Rectangle {
        id: header
        anchors.top: parent.top
        width: parent.width
        height: Tok.DesignTokens.headerBarHeight
        color: Tok.DesignTokens.bgHeader
        radius: Tok.DesignTokens.radiusLg

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Tok.DesignTokens.space4
            anchors.rightMargin: Tok.DesignTokens.space2
            spacing: Tok.DesignTokens.space2

            Text {
                text: "已导入算子"
                color: Tok.DesignTokens.textPrimary
                font.pixelSize: Tok.DesignTokens.fontSizeXl
                font.bold: true
                font.family: Tok.DesignTokens.fontFamily
            }

            // 数量标签
            Rectangle {
                width: 28; height: 18
                radius: 9
                color: Tok.DesignTokens.accentPrimary
                Text {
                    anchors.centerIn: parent
                    text: importedList.length
                    color: "white"
                    font.pixelSize: 10
                    font.bold: true
                }
            }

            Item { Layout.fillWidth: true }

            Button {
                flat: true
                text: "×"
                font.pixelSize: 18
                onClicked: dlg.close()
                palette.buttonText: Tok.DesignTokens.textTertiary
            }
        }
    }

    // ============ 搜索框 ============
    Rectangle {
        id: searchBar
        anchors.top: header.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Tok.DesignTokens.space3
        height: Tok.DesignTokens.searchBarHeight
        color: Tok.DesignTokens.bgSurface
        border.color: Tok.DesignTokens.borderDefault
        radius: Tok.DesignTokens.radiusMd

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Tok.DesignTokens.space2
            anchors.rightMargin: Tok.DesignTokens.space2
            spacing: Tok.DesignTokens.space1

            Text {
                text: "🔍"
                color: Tok.DesignTokens.textTertiary
                font.pixelSize: 12
            }

            TextField {
                Layout.fillWidth: true
                placeholderText: "搜索算子名称或 type…"
                color: Tok.DesignTokens.textSecondary
                font.pixelSize: Tok.DesignTokens.fontSizeBase
                font.family: Tok.DesignTokens.fontFamily
                background: Rectangle { color: "transparent" }
                onTextChanged: keyword = text
            }
        }
    }

    // ============ 列表区 ============
    ScrollView {
        id: scroll
        anchors.top: searchBar.bottom
        anchors.bottom: footerNote.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.topMargin: Tok.DesignTokens.space2
        clip: true
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

        ListView {
            id: listView
            anchors.fill: parent
            clip: true
            spacing: Tok.DesignTokens.space1

            // 空状态
            Text {
                anchors.centerIn: parent
                visible: listView.count === 0
                text: "暂无已导入算子\n点击「导入算子」按钮添加"
                color: Tok.DesignTokens.textTertiary
                font.pixelSize: Tok.DesignTokens.fontSizeSm
                font.family: Tok.DesignTokens.fontFamily
                horizontalAlignment: Text.AlignHCenter
            }

            model: filteredModel

            delegate: Rectangle {
                width: listView.width - Tok.DesignTokens.space3 * 2
                height: 48
                anchors.horizontalCenter: parent.horizontalCenter
                color: mouseArea.containsMouse ? Tok.DesignTokens.bgHover : Tok.DesignTokens.bgSurface
                border.color: Tok.DesignTokens.borderDefault
                radius: Tok.DesignTokens.radiusMd

                MouseArea {
                    id: mouseArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Tok.DesignTokens.space3
                    anchors.rightMargin: Tok.DesignTokens.space2
                    spacing: Tok.DesignTokens.space2

                    // 分类色标
                    Rectangle {
                        width: 4; height: 32
                        radius: 2
                        color: Tok.DesignTokens.categoryColor(modelData.category)
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0

                        Text {
                            text: modelData.cnName
                            color: Tok.DesignTokens.textPrimary
                            font.pixelSize: Tok.DesignTokens.fontSizeBase
                            font.bold: true
                            font.family: Tok.DesignTokens.fontFamily
                        }

                        RowLayout {
                            spacing: Tok.DesignTokens.space2
                            Text {
                                text: modelData.type
                                color: Tok.DesignTokens.accentPrimary
                                font.pixelSize: Tok.DesignTokens.fontSizeXs
                                font.family: Tok.DesignTokens.fontMono
                            }
                            Text {
                                text: "v" + modelData.version
                                color: Tok.DesignTokens.textTertiary
                                font.pixelSize: Tok.DesignTokens.fontSizeXs
                                font.family: Tok.DesignTokens.fontMono
                            }
                            Text {
                                text: modelData.category
                                color: Tok.DesignTokens.textTertiary
                                font.pixelSize: Tok.DesignTokens.fontSizeXs
                                font.family: Tok.DesignTokens.fontFamily
                            }
                        }
                    }

                    // 删除按钮
                    Button {
                        text: "删除"
                        flat: true
                        implicitHeight: 24
                        implicitWidth: 40
                        font.pixelSize: Tok.DesignTokens.fontSizeXs
                        palette.buttonText: Tok.DesignTokens.accentError
                        background: Rectangle {
                            color: parent.hovered ? Qt.rgba(1, 0.32, 0.32, 0.15) : "transparent"
                            border.color: parent.hovered ? Tok.DesignTokens.accentError : "transparent"
                            radius: Tok.DesignTokens.radiusSm
                        }
                        onClicked: confirmDelete(modelData.type, modelData.cnName)
                    }
                }
            }
        }
    }

    // ============ 底部说明 ============
    Rectangle {
        id: footerNote
        anchors.bottom: parent.bottom
        width: parent.width
        height: 40
        color: Tok.DesignTokens.bgHeader
        border.color: Tok.DesignTokens.borderDefault

        Text {
            anchors.centerIn: parent
            text: "删除的算子将在下次启动后彻底生效"
            color: Tok.DesignTokens.textTertiary
            font.pixelSize: Tok.DesignTokens.fontSizeXs
            font.family: Tok.DesignTokens.fontFamily
        }
    }

    // ============ 删除确认对话框 ============
    MessageDialog {
        id: deleteConfirmDialog
        title: "确认删除"
        text: ""
        buttons: MessageDialog.Ok | MessageDialog.Cancel
        onAccepted: doDelete()
    }

    // ============ 过滤模型 ============
    // 对 importedList 做关键词过滤
    property var filteredModel: {
        if (!importedList || importedList.length === 0) return []
        if (keyword === "") return importedList
        var kw = keyword.toLowerCase()
        var result = []
        for (var i = 0; i < importedList.length; ++i) {
            var item = importedList[i]
            if (item.cnName.toLowerCase().indexOf(kw) >= 0 ||
                item.type.toLowerCase().indexOf(kw) >= 0) {
                result.push(item)
            }
        }
        return result
    }

    // ============ 待删除的算子 type ============
    property string pendingDeleteType: ""

    // ============ 逻辑函数 ============

    // 刷新列表
    function refresh() {
        if (!bridge || !bridge.operatorLibraryBridge) return
        importedList = bridge.operatorLibraryBridge.listImported()
    }

    // 确认删除
    function confirmDelete(type, cnName) {
        pendingDeleteType = type
        deleteConfirmDialog.text = "确认删除已导入算子「" + cnName + "」？\n\n" +
                                   "删除后将在下次启动后彻底生效。"
        deleteConfirmDialog.open()
    }

    // 执行删除
    function doDelete() {
        if (!bridge || !bridge.operatorLibraryBridge || !pendingDeleteType) return
        var result = bridge.operatorLibraryBridge.removeImported(pendingDeleteType)
        if (result.ok) {
            if (bridge.showToast) {
                bridge.showToast("已删除算子（重启生效）")
            }
            refresh()
        } else {
            // 删除失败（画布依赖或其他原因）
            if (bridge.showToast) {
                bridge.showToast("删除失败：" + result.error)
            }
        }
        pendingDeleteType = ""
    }

    // 监听 importedChanged 信号，自动刷新
    Connections {
        target: bridge && bridge.operatorLibraryBridge ? bridge.operatorLibraryBridge : null
        function onImportedChanged(type, action) {
            refresh()
        }
    }

    // 打开时刷新
    onOpened: refresh()
}
