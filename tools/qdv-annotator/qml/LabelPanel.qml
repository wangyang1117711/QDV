// =====================================================================
// LabelPanel.qml — 标签管理面板
//  - 几何任务：点击标签设为「当前激活标签」（新标注使用该类别）
//  - 分类任务：勾选标签即标记当前图像属于该类（支持多标签）
// =====================================================================
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QDVAnnotator 1.0 as Tok

Pane {
    id: root
    property int activeLabelId: -1
    property string taskType: "classification"
    signal labelSelected(int id)
    signal requestAdd(string name)
    signal requestRemove(int id)
    signal requestRename(int id, string name)

    padding: 0
    background: Rectangle { color: Tok.DesignTokens.bgPanel }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // 标题栏
        Label {
            text: "标签 (Labels)"
            color: Tok.DesignTokens.textPrimary
            font.bold: true
            font.family: Tok.DesignTokens.fontFamilyCJK
            font.pixelSize: Tok.DesignTokens.fontSizeLg
            leftPadding: Tok.DesignTokens.space3
            topPadding: Tok.DesignTokens.space2
            bottomPadding: Tok.DesignTokens.space2
        }

        // 添加标签
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Tok.DesignTokens.space2
            Layout.rightMargin: Tok.DesignTokens.space2
            Layout.bottomMargin: Tok.DesignTokens.space2
            TextField {
                id: newLabel
                Layout.fillWidth: true
                placeholderText: "新标签名"
                color: Tok.DesignTokens.textPrimary
                font.family: Tok.DesignTokens.fontFamilyCJK
                background: Rectangle {
                    color: Tok.DesignTokens.bgSurface
                    border.color: Tok.DesignTokens.borderDefault
                    radius: Tok.DesignTokens.radiusSm
                }
                onAccepted: { if (text) { root.requestAdd(text); text = "" } }
            }
            Button {
                text: "+"
                onClicked: { if (newLabel.text) { root.requestAdd(newLabel.text); newLabel.text = "" } }
            }
        }

        // 标签列表
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            ListView {
                id: list
                model: session.labels
                spacing: 2
                delegate: ItemDelegate {
                    id: del
                    width: list.width
                    height: 34
                    property bool isClassOn: root.taskType === "classification"
                        ? session.imageClassLabels().indexOf(index) >= 0 : (root.activeLabelId === index)
                    // 标签名过长被省略时，悬停显示完整标签名
                    ToolTip.visible: hovered && nameLbl.truncated
                    ToolTip.text: modelData
                    ToolTip.delay: 400
                    background: Rectangle {
                        color: del.hovered ? Tok.DesignTokens.bgHover
                              : (del.isClassOn ? Tok.DesignTokens.bgSurface : "transparent")
                        border.color: (root.taskType !== "classification" && root.activeLabelId === index)
                                     ? Tok.DesignTokens.accentPrimary : "transparent"
                        border.width: 1
                        radius: Tok.DesignTokens.radiusSm
                    }
                    contentItem: RowLayout {
                        spacing: 8
                        // 颜色块
                        Rectangle {
                    implicitWidth: 14; implicitHeight: 14; radius: 2
                    color: session.labelColors[index]
                }
                        // 分类任务：勾选；几何任务：名称
                        CheckBox {
                            visible: root.taskType === "classification"
                            checked: del.isClassOn
                            onToggled: {
                                var cur = session.imageClassLabels().map(function (v) { return v })
                                var pos = cur.indexOf(index)
                                if (checked && pos < 0) cur.push(index)
                                if (!checked && pos >= 0) cur.splice(pos, 1)
                                session.setImageClassLabels(cur)
                            }
                        }
                        Label {
                            id: nameLbl
                            Layout.fillWidth: true
                            text: modelData
                            color: Tok.DesignTokens.textPrimary
                            font.family: Tok.DesignTokens.fontFamilyCJK
                            elide: Text.ElideRight
                        }
                        // 删除
                        ToolButton {
                            text: "✕"
                            onClicked: root.requestRemove(index)
                        }
                    }
                    onClicked: if (root.taskType !== "classification") root.labelSelected(index)
                }
            }
        }
    }
}
