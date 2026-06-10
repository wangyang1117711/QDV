// =====================================================================
// ResultTable.qml — 图像分析结果数据可视化组件（v3.1.0）
//
// 功能：
// - 以表格/列表形式展示图像处理结果
// - 支持图表渲染（Canvas 柱状图/折线图）
// - 支持结果高亮、排序、筛选
// - 与 AnnotateOverlay 联动：点击结果高亮对应标注
// =====================================================================

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QDV.EditView 3.0 as Tok

Rectangle {
    id: root

    // ============ 外部属性 ============
    property var analysisResults: []   // [{label, value, unit, type, confidence, bbox}]
    property var chartData: []         // [{name, value}] 用于图表
    property string chartTitle: ""
    property int highlightIndex: -1

    signal resultClicked(int index, var result)

    color: Tok.DesignTokens.bgPanel
    border.color: Tok.DesignTokens.borderDefault
    border.width: Tok.DesignTokens.borderWidth
    radius: Tok.DesignTokens.radiusMd
    clip: true

    // 高度自适应
    implicitHeight: contentLayout.implicitHeight + 16
    implicitWidth: 280

    ColumnLayout {
        id: contentLayout
        anchors.fill: parent
        anchors.margins: 8
        spacing: 8

        // 标题
        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            Rectangle {
                width: 3; height: 14
                color: Tok.DesignTokens.accentPrimary
                radius: 1
            }
            Label {
                text: "分析结果"
                color: Tok.DesignTokens.textPrimary
                font.pixelSize: Tok.DesignTokens.fontSizeSm
                font.bold: true
                font.family: Tok.DesignTokens.fontFamilyCJK
            }
            Item { Layout.fillWidth: true }
            Label {
                text: root.analysisResults.length + " 项"
                color: Tok.DesignTokens.textTertiary
                font.pixelSize: Tok.DesignTokens.fontSizeXs
                font.family: Tok.DesignTokens.fontFamilyCJK
            }
        }

        // ============ 图表区（柱状图/折线图） ============
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 120
            color: Tok.DesignTokens.bgSurface
            radius: Tok.DesignTokens.radiusSm
            visible: chartData.length > 0

            Canvas {
                id: chartCanvas
                anchors.fill: parent
                anchors.margins: 8

                onPaint: {
                    var ctx = getContext("2d")
                    var w = width
                    var h = height
                    ctx.clearRect(0, 0, w, h)

                    if (chartData.length === 0) return

                    // 最大值
                    var maxVal = 0
                    for (var i = 0; i < chartData.length; i++) {
                        if (chartData[i].value > maxVal) maxVal = chartData[i].value
                    }
                    if (maxVal === 0) maxVal = 1

                    // 绘制柱状图
                    var barW = (w - 8) / chartData.length - 6
                    var colors = ["#7c3aed", "#3b82f6", "#10b981", "#f59e0b", "#ef4444", "#ec4899"]

                    for (var j = 0; j < chartData.length; j++) {
                        var barH = (chartData[j].value / maxVal) * (h - 20)
                        var barX = 4 + j * (barW + 6)
                        var barY = h - barH - 14

                        // 柱子
                        ctx.fillStyle = colors[j % colors.length]
                        ctx.fillRect(barX, barY, barW, barH)

                        // 值标签
                        ctx.fillStyle = "#ccc"
                        ctx.font = "9px " + Tok.DesignTokens.fontFamilyCJK
                        ctx.textAlign = "center"
                        var valText = typeof chartData[j].value === "number"
                            ? chartData[j].value.toFixed(1) : chartData[j].value
                        ctx.fillText(valText, barX + barW / 2, barY - 4)

                        // 名称标签
                        var nameText = chartData[j].name || ""
                        if (nameText.length > 4) nameText = nameText.substring(0, 4)
                        ctx.fillText(nameText, barX + barW / 2, h - 2)
                    }
                }
            }
        }

        // ============ 表格头 ============
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 24
            color: Tok.DesignTokens.bgHeader
            radius: Tok.DesignTokens.radiusSm

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8

                Label {
                    text: "项目"
                    color: Tok.DesignTokens.textSecondary
                    font.pixelSize: Tok.DesignTokens.fontSizeXs
                    font.family: Tok.DesignTokens.fontFamilyCJK
                    font.bold: true
                    Layout.preferredWidth: 80
                }
                Label {
                    text: "数值"
                    color: Tok.DesignTokens.textSecondary
                    font.pixelSize: Tok.DesignTokens.fontSizeXs
                    font.family: Tok.DesignTokens.fontFamilyCJK
                    font.bold: true
                    Layout.preferredWidth: 80
                }
                Label {
                    text: "置信度"
                    color: Tok.DesignTokens.textSecondary
                    font.pixelSize: Tok.DesignTokens.fontSizeXs
                    font.family: Tok.DesignTokens.fontFamilyCJK
                    font.bold: true
                    Layout.preferredWidth: 50
                }
                Item { Layout.fillWidth: true }
            }
        }

        // ============ 结果列表 ============
        ListView {
            id: resultList
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(contentHeight, 300)
            model: root.analysisResults
            spacing: 1
            interactive: contentHeight > height
            clip: true

            delegate: Rectangle {
                width: resultList.width
                height: 30

                color: index === highlightIndex
                    ? Qt.rgba(0.486, 0.302, 1.0, 0.15)
                    : (index % 2 === 0 ? Tok.DesignTokens.bgPanel : Tok.DesignTokens.bgHeader)

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8

                    Label {
                        text: modelData.label || ""
                        color: Tok.DesignTokens.textPrimary
                        font.pixelSize: Tok.DesignTokens.fontSizeXs
                        font.family: Tok.DesignTokens.fontFamilyCJK
                        Layout.preferredWidth: 80
                        elide: Text.ElideRight
                    }
                    Label {
                        text: (modelData.value !== undefined ? modelData.value : "") +
                              (modelData.unit ? " " + modelData.unit : "")
                        color: Tok.DesignTokens.textPrimary
                        font.pixelSize: Tok.DesignTokens.fontSizeXs
                        font.family: Tok.DesignTokens.fontFamilyCJK
                        Layout.preferredWidth: 80
                        elide: Text.ElideRight
                    }
                    // 置信度进度条
                    Rectangle {
                        Layout.preferredWidth: 50
                        Layout.preferredHeight: 4
                        radius: 2
                        color: Tok.DesignTokens.bgSurface
                        visible: modelData.confidence !== undefined

                        Rectangle {
                            height: parent.height
                            radius: 2
                            width: parent.width * (modelData.confidence || 0)
                            color: (modelData.confidence || 0) >= 0.95 ? "#10b981"
                                 : (modelData.confidence || 0) >= 0.7 ? "#f59e0b"
                                 : "#ef4444"
                        }
                    }
                    Label {
                        text: modelData.confidence !== undefined
                            ? (modelData.confidence * 100).toFixed(0) + "%"
                            : ""
                        color: Tok.DesignTokens.textTertiary
                        font.pixelSize: Tok.DesignTokens.fontSizeXs
                        font.family: Tok.DesignTokens.fontFamilyCJK
                        visible: modelData.confidence !== undefined
                        Layout.preferredWidth: 30
                    }
                    Item { Layout.fillWidth: true }

                    // 类型标签
                    Rectangle {
                        visible: modelData.type !== undefined
                        color: getTypeColor(modelData.type)
                        radius: 2
                        Layout.preferredWidth: typeLabel.implicitWidth + 8
                        Layout.preferredHeight: 16
                        Label {
                            id: typeLabel
                            anchors.centerIn: parent
                            text: modelData.type || ""
                            color: "#FFF"
                            font.pixelSize: 9
                            font.family: Tok.DesignTokens.fontFamilyCJK
                        }
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.resultClicked(index, modelData)
                }
            }
        }
    }

    // 类型颜色映射
    function getTypeColor(type) {
        switch(type) {
        case "识别": return "#3b82f6"
        case "检测": return "#10b981"
        case "测量": return "#7c3aed"
        case "分类": return "#f59e0b"
        default: return "#6366f1"
        }
    }
}