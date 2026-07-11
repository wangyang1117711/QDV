// =====================================================================
// ShortcutsPanel.qml — 快捷键速查浮层（v4.0）
//
// 功能：
// - 按 ? 或 Ctrl+/ 弹出/关闭
// - 列出所有可用快捷键（分类：全局/画布/编辑器）
// - ESC 关闭
// =====================================================================

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QDV.EditView 3.0 as Tok

Popup {
    id: root
    anchors.centerIn: parent ? parent : Overlay.overlay
    width: 380
    height: 420
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    background: Rectangle {
        color: Tok.DesignTokens.bgPanel
        border.color: Tok.DesignTokens.accentPrimary
        border.width: 1
        radius: Tok.DesignTokens.radiusLg
    }

    // 入场动画
    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Tok.DesignTokens.durationNormal }
        NumberAnimation { property: "scale"; from: 0.9; to: 1.0; duration: Tok.DesignTokens.durationNormal; easing.type: Easing.OutCubic }
    }
    exit: Transition {
        NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Tok.DesignTokens.durationFast }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Tok.DesignTokens.space4
        spacing: Tok.DesignTokens.space3

        // 标题栏
        RowLayout {
            Layout.fillWidth: true
            spacing: Tok.DesignTokens.space2

            Rectangle {
                width: 16; height: 16; radius: 3
                color: Tok.DesignTokens.accentPrimary
            }
            Label {
                text: "快捷键速查"
                color: Tok.DesignTokens.textPrimary
                font.pixelSize: Tok.DesignTokens.fontSizeLg
                font.bold: true
                font.family: Tok.DesignTokens.fontFamilyCJK
                Layout.fillWidth: true
            }
            ToolButton {
                text: "✕"
                font.pixelSize: Tok.DesignTokens.fontSizeSm
                Layout.preferredWidth: 24
                Layout.preferredHeight: 24
                onClicked: root.close()
                background: Rectangle {
                    color: parent.hovered ? Tok.DesignTokens.bgHover : "transparent"
                    radius: Tok.DesignTokens.radiusSm
                }
            }
        }

        // 搜索
        Rectangle {
            Layout.fillWidth: true
            height: 28
            color: Tok.DesignTokens.bgSurface
            radius: Tok.DesignTokens.radiusSm
            border.color: searchField.activeFocus ? Tok.DesignTokens.borderFocus : Tok.DesignTokens.borderDefault
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Tok.DesignTokens.space2
                anchors.rightMargin: Tok.DesignTokens.space1
                spacing: Tok.DesignTokens.space1
                Label { text: "\u2315"; color: Tok.DesignTokens.textTertiary; font.pixelSize: 12 }
                TextField {
                    id: searchField
                    Layout.fillWidth: true
                    color: Tok.DesignTokens.textPrimary
                    font.pixelSize: Tok.DesignTokens.fontSizeBase
                    placeholderText: "搜索快捷键..."
                    placeholderTextColor: Tok.DesignTokens.textPlaceholder
                    background: Rectangle { color: "transparent" }
                    verticalAlignment: TextInput.AlignVCenter
                }
            }
        }

        // 快捷键列表
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            ScrollBar.vertical.policy: ScrollBar.AsNeeded

            ColumnLayout {
                width: parent.width - Tok.DesignTokens.space4
                spacing: Tok.DesignTokens.space2

                // 全局
                Label {
                    text: "全局"
                    color: Tok.DesignTokens.accentPrimary
                    font.bold: true
                    font.pixelSize: Tok.DesignTokens.fontSizeSm
                    font.family: Tok.DesignTokens.fontFamilyCJK
                }
                Repeater {
                    model: [
                        { key: "Ctrl+K", desc: "聚焦搜索栏" },
                        { key: "Ctrl+S", desc: "保存方案" },
                        { key: "Ctrl+O", desc: "加载方案" },
                        { key: "? / Ctrl+/", desc: "快捷键面板" },
                        { key: "Alt+1~8", desc: "切换导航栏" },
                    ]
                    delegate: shortcutDelegate
                }

                // 画布
                Label {
                    text: "画布"
                    color: Tok.DesignTokens.accentInfo
                    font.bold: true
                    font.pixelSize: Tok.DesignTokens.fontSizeSm
                    font.family: Tok.DesignTokens.fontFamilyCJK
                    Layout.topMargin: Tok.DesignTokens.space2
                }
                Repeater {
                    model: [
                        { key: "滚轮", desc: "缩放画布" },
                        { key: "Delete", desc: "删除选中节点" },
                        { key: "Escape", desc: "取消选择" },
                        { key: "Ctrl+Z", desc: "撤销" },
                        { key: "Ctrl+Y", desc: "重做" },
                    ]
                    delegate: shortcutDelegate
                }

                // 编辑器
                Label {
                    text: "编辑器"
                    color: Tok.DesignTokens.accentWarning
                    font.bold: true
                    font.pixelSize: Tok.DesignTokens.fontSizeSm
                    font.family: Tok.DesignTokens.fontFamilyCJK
                    Layout.topMargin: Tok.DesignTokens.space2
                }
                Repeater {
                    model: [
                        { key: "双击算子", desc: "添加到画布" },
                        { key: "双击节点", desc: "打开参数编辑器" },
                        { key: "右键节点", desc: "上下文菜单" },
                        { key: "拖拽节点", desc: "移动位置" },
                    ]
                    delegate: shortcutDelegate
                }
            }
        }

        // 底部提示
        Label {
            text: "按 ESC 或点击外部关闭"
            color: Tok.DesignTokens.textDisabled
            font.pixelSize: Tok.DesignTokens.fontSizeXs
            font.family: Tok.DesignTokens.fontFamilyCJK
            Layout.alignment: Qt.AlignHCenter
        }
    }

    // 快捷键行组件
    Component {
        id: shortcutDelegate
        RowLayout {
            width: parent ? parent.width : 360
            height: 28

            Rectangle {
                Layout.preferredWidth: 80
                Layout.preferredHeight: 22
                radius: Tok.DesignTokens.radiusSm
                color: Tok.DesignTokens.bgSurface
                border.color: Tok.DesignTokens.borderDefault
                border.width: 1

                Label {
                    anchors.centerIn: parent
                    text: modelData.key
                    color: Tok.DesignTokens.textPrimary
                    font.pixelSize: Tok.DesignTokens.fontSizeXs
                    font.family: Tok.DesignTokens.fontMono
                    font.bold: true
                }
            }
            Label {
                text: modelData.desc
                color: Tok.DesignTokens.textSecondary
                font.pixelSize: Tok.DesignTokens.fontSizeXs
                font.family: Tok.DesignTokens.fontFamilyCJK
                Layout.fillWidth: true
                Layout.leftMargin: Tok.DesignTokens.space2
            }
        }
    }
}
