// =====================================================================
// ImageListPanel.qml — 左侧图像列表（导入数据集浏览）
// =====================================================================
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QDVAnnotator 1.0 as Tok

Pane {
    id: root
    property int currentIndex: -1
    signal imageSelected(int index)

    padding: 0
    background: Rectangle { color: Tok.DesignTokens.bgPanel }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        Label {
            text: "图像 (" + session.images.length + ")"
            color: Tok.DesignTokens.textPrimary
            font.bold: true
            font.family: Tok.DesignTokens.fontFamilyCJK
            font.pixelSize: Tok.DesignTokens.fontSizeLg
            leftPadding: Tok.DesignTokens.space3
            topPadding: Tok.DesignTokens.space2
            bottomPadding: Tok.DesignTokens.space2
        }
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            ListView {
                id: list
                model: session.images
                spacing: 2
                currentIndex: root.currentIndex
                delegate: ItemDelegate {
                    id: del
                    width: list.width
                    height: 56
                    // 文件名过长被省略时，悬停显示完整文件名
                    ToolTip.visible: hovered && fnameLbl.truncated
                    ToolTip.text: modelData.fileName
                    ToolTip.delay: 400
                    background: Rectangle {
                        color: (index === root.currentIndex) ? Tok.DesignTokens.bgSurface
                              : (hovered ? Tok.DesignTokens.bgHover : "transparent")
                        border.color: (index === root.currentIndex) ? Tok.DesignTokens.accentPrimary : "transparent"
                        border.width: 1
                    }
                    contentItem: RowLayout {
                        spacing: 8
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Label {
                                id: fnameLbl
                                text: modelData.fileName
                                color: Tok.DesignTokens.textPrimary
                                font.family: Tok.DesignTokens.fontFamilyCJK
                                font.pixelSize: Tok.DesignTokens.fontSizeSm
                                elide: Text.ElideMiddle
                                Layout.fillWidth: true
                            }
                            // 标注状态
                            RowLayout {
                                spacing: 4
                                Rectangle {
                                    implicitWidth: 8; implicitHeight: 8; radius: 4
                                    color: {
                                        var n = (root.parent) ? 0 : 0
                                        var anns = modelData.annotations ? modelData.annotations.length : 0
                                        var cls = modelData.classLabels ? modelData.classLabels.length : 0
                                        (anns > 0 || cls > 0) ? Tok.DesignTokens.accentSuccess : Tok.DesignTokens.textDisabled
                                    }
                                }
                                Label {
                                    text: {
                                        var anns = modelData.annotations ? modelData.annotations.length : 0
                                        var cls = modelData.classLabels ? modelData.classLabels.length : 0
                                        (anns > 0 || cls > 0) ? (anns ? anns + " 标注" : cls + " 类") : "未标注"
                                    }
                                    color: Tok.DesignTokens.textTertiary
                                    font.family: Tok.DesignTokens.fontFamilyCJK
                                    font.pixelSize: Tok.DesignTokens.fontSizeXxs
                                }
                            }
                        }
                    }
                    onClicked: root.imageSelected(index)
                }
            }
        }
    }
}
