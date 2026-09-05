// =====================================================================
// AnnotationListPanel.qml — 右侧当前图像标注列表
//  - 列出标注项，支持删除
//  - OCR 任务：选中矩形标注可直接编辑转写文本
// =====================================================================
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QDVAnnotator 1.0 as Tok

Pane {
    id: root
    property string taskType: "detection"
    signal requestDelete(int index)

    padding: 0
    background: Rectangle { color: Tok.DesignTokens.bgPanel }

    property int selectedIndex: -1
    // 标注列表数据（响应式：currentAnnotations() 是函数调用不会自动刷新，须手动随信号更新）
    property var anns: session.currentAnnotations()
    Connections {
        target: session
        function onCurrentImageChanged() { root.anns = session.currentAnnotations() }
        function onImagesChanged() { root.anns = session.currentAnnotations() }
        function onCurrentImageIndexChanged() { root.anns = session.currentAnnotations() }
        // 画布选中标注 → 列表高亮并滚动定位（Task 6.2）
        function onSelectedAnnotationIndexChanged() {
            root.selectedIndex = session.selectedAnnotationIndex
            if (root.selectedIndex >= 0)
                list.positionViewAtIndex(root.selectedIndex, ListView.Center)
        }
    }
    Component.onCompleted: root.anns = session.currentAnnotations()

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        Label {
            text: "标注项"
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
                model: root.anns
                spacing: 2
                currentIndex: root.selectedIndex
                delegate: ItemDelegate {
                    id: del
                    width: list.width
                    height: 40
                    // 标注描述过长被省略时，悬停显示完整内容
                    ToolTip.visible: hovered && descLbl.truncated
                    ToolTip.text: descLbl.text
                    ToolTip.delay: 400
                    background: Rectangle {
                        color: (index === root.selectedIndex) ? Tok.DesignTokens.bgSurface
                              : (hovered ? Tok.DesignTokens.bgHover : "transparent")
                        border.color: (index === root.selectedIndex) ? Tok.DesignTokens.accentPrimary : "transparent"
                        border.width: 1
                    }
                    contentItem: RowLayout {
                        spacing: 6
                        Rectangle { implicitWidth: 10; implicitHeight: 10; radius: 2; color: session.colorForLabel(modelData.labelId) }
                        Label {
                            id: descLbl
                            Layout.fillWidth: true
                            text: {
                                var n = (modelData.labelId >= 0 && modelData.labelId < session.labels.length) ? session.labels[modelData.labelId] : "未分类"
                                // S1：形状名查能力注册表 label（未登记回退内置中文）
                                var sh = capabilities.shapeInfo(modelData.shape).label
                                if (!sh) sh = modelData.shape === "polygon" ? "多边形"
                                        : (modelData.shape === "rect" ? "矩形"
                                        : (modelData.shape === "vec_rect" ? "方向矩形" : modelData.shape))
                                n + " · " + sh
                            }
                            color: Tok.DesignTokens.textPrimary
                            font.family: Tok.DesignTokens.fontFamilyCJK
                            font.pixelSize: Tok.DesignTokens.fontSizeSm
                            elide: Text.ElideRight
                        }
                        ToolButton {
                            text: "✕"
                            onClicked: root.requestDelete(index)
                        }
                    }
                    onClicked: session.selectedAnnotationIndex = index
                }
            }
        }

        // OCR 转写编辑区
        Pane {
            visible: root.taskType === "ocr" && root.selectedIndex >= 0
            Layout.fillWidth: true
            padding: 8
            background: Rectangle { color: Tok.DesignTokens.bgHeader; border.color: Tok.DesignTokens.borderDefault }
            ColumnLayout {
                spacing: 6
                Label { text: "转写文本 (OCR)"; color: Tok.DesignTokens.textSecondary; font.family: Tok.DesignTokens.fontFamilyCJK; font.pixelSize: Tok.DesignTokens.fontSizeSm }
                TextField {
                    id: txt
                    Layout.fillWidth: true
                    color: Tok.DesignTokens.textPrimary
                    font.family: Tok.DesignTokens.fontFamilyCJK
                    background: Rectangle { color: Tok.DesignTokens.bgSurface; border.color: Tok.DesignTokens.borderDefault; radius: 2 }
                    // 选中项变化实时刷新转写文本（Task 7.1）：anns / selectedIndex 均为响应式属性
                    text: (root.selectedIndex >= 0 && root.anns && root.selectedIndex < root.anns.length)
                          ? (root.anns[root.selectedIndex].text || "") : ""
                    onAccepted: applyText()
                }
                Button {
                    text: "保存转写"
                    Layout.alignment: Qt.AlignRight
                    onClicked: applyText()
                }
            }
            function applyText() {
                if (root.selectedIndex < 0) return
                var a = session.currentAnnotations()[root.selectedIndex]
                a.text = txt.text
                session.updateAnnotation(root.selectedIndex, a)
            }
        }
    }
}
