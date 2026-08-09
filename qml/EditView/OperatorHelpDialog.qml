// =====================================================================
// OperatorHelpDialog.qml — 算子完整说明弹窗（v2.8.0 算子帮助体系）
//
// 功能：
// - 通过 editViewBridge.getOperatorDoc(type) 获取算子完整文档
// - 分区展示：核心含义 / 适用场景 / 功能描述 / 参数详解 / 注意事项 / 关联算子
// - 关联算子可点击跳转（切换 operatorType 重新加载）
// - 空字段分区自动隐藏
// - Esc / 点击外部关闭
//
// 用法：
//   OperatorHelpDialog { operatorType: "ReadImage" }
//   或通过 openOperatorHelp(type) 辅助函数（Main.qml 中定义）
// =====================================================================

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QDV.EditView 3.0 as Tok

Popup {
    id: root
    property string operatorType: ""

    modal: true
    focus: true
    width: 540
    height: 640
    padding: 0
    x: (parent ? (parent.width - width) / 2 : 0)
    y: (parent ? (parent.height - height) / 2 : 0)
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    // 背景：深色面板 + 紫色描边，与项目其他弹窗一致
    background: Rectangle {
        color: Tok.DesignTokens.bgPanel
        border.color: Tok.DesignTokens.accentPrimary
        border.width: 1
        radius: Tok.DesignTokens.radiusLg
    }

    // ============ 文档数据（由 getOperatorDoc 填充）============
    property var doc: ({})
    function reloadDoc() {
        if (operatorType === "" || !editViewBridge) {
            root.doc = ({})
            return
        }
        try {
            root.doc = editViewBridge.getOperatorDoc(operatorType) || ({})
        } catch (e) {
            console.warn("[OperatorHelpDialog] getOperatorDoc failed:", e)
            root.doc = ({})
        }
    }

    onOperatorTypeChanged: reloadDoc()
    onAboutToShow: reloadDoc()

    // 参数类型整数 → 可读名称（与 C++ OperatorHelpProvider::paramTypeName 对齐）
    function paramTypeName(t) {
        switch (Number(t)) {
            case 0: return "Int"
            case 1: return "Double"
            case 2: return "Enum"
            case 3: return "Bool"
            case 4: return "String"
            case 5: return "Vector"
            case 6: return "Mat"
            default: return "Unknown"
        }
    }

    // 分类色块
    function categoryColorOf(cat) {
        return Tok.DesignTokens.categoryColor(cat || "")
    }

    // ============ 标题栏 ============
    Rectangle {
        id: header
        anchors.top: parent.top
        width: parent.width
        height: Tok.DesignTokens.headerBarHeight
        color: Tok.DesignTokens.bgHeader
        radius: Tok.DesignTokens.radiusLg

        // 顶部圆角处理：仅保留上半部分圆角
        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: parent.radius
            color: parent.color
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Tok.DesignTokens.space3
            anchors.rightMargin: Tok.DesignTokens.space2
            spacing: Tok.DesignTokens.space2

            // 分类色点
            Rectangle {
                width: 10; height: 10; radius: 5
                color: root.categoryColorOf(root.doc ? root.doc.category : "")
                Layout.alignment: Qt.AlignVCenter
            }

            // 中文名（主标题）
            Label {
                text: (root.doc && root.doc.cnName) ? root.doc.cnName : root.operatorType
                color: Tok.DesignTokens.textPrimary
                font.pixelSize: Tok.DesignTokens.fontSizeXl
                font.bold: true
                font.family: Tok.DesignTokens.fontFamilyCJK
                Layout.alignment: Qt.AlignVCenter
            }

            // Type（副标题，等宽字体）
            Label {
                text: root.operatorType
                color: Tok.DesignTokens.textTertiary
                font.pixelSize: Tok.DesignTokens.fontSizeSm
                font.family: Tok.DesignTokens.fontMono
                Layout.alignment: Qt.AlignVCenter
            }

            Item { Layout.fillWidth: true }

            // 关闭按钮
            Button {
                Layout.alignment: Qt.AlignVCenter
                flat: true
                text: "×"
                font.pixelSize: 18
                implicitWidth: 32
                implicitHeight: 28
                palette.buttonText: Tok.DesignTokens.textTertiary
                onClicked: root.close()
            }
        }
    }

    // ============ 内容区 ============
    ScrollView {
        id: scroll
        anchors.top: header.bottom
        anchors.bottom: parent.bottom
        width: parent.width
        clip: true
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

        ColumnLayout {
            width: scroll.width
            spacing: Tok.DesignTokens.space4

            // 顶部留白
            Item { Layout.preferredHeight: Tok.DesignTokens.space2; Layout.fillWidth: true }

            // --- 核心含义 ---
            Loader {
                Layout.fillWidth: true
                Layout.leftMargin: Tok.DesignTokens.space4
                Layout.rightMargin: Tok.DesignTokens.space4
                active: !!(root.doc && root.doc.coreMeaning && String(root.doc.coreMeaning).length > 0)
                sourceComponent: sectionComp
                onLoaded: {
                    item.sectionTitle = "核心含义"
                    item.sectionColor = Tok.DesignTokens.accentPrimary
                    // 用 Qt.binding 保证点击关联算子切换 doc 后正文实时刷新
                    item.sectionBody = Qt.binding(function(){ return root.doc ? root.doc.coreMeaning : "" })
                }
            }

            // --- 适用场景 ---
            Loader {
                Layout.fillWidth: true
                Layout.leftMargin: Tok.DesignTokens.space4
                Layout.rightMargin: Tok.DesignTokens.space4
                active: !!(root.doc && root.doc.scenario && String(root.doc.scenario).length > 0)
                sourceComponent: sectionComp
                onLoaded: {
                    item.sectionTitle = "适用场景"
                    item.sectionColor = Tok.DesignTokens.accentInfo
                    item.sectionBody = Qt.binding(function(){ return root.doc ? root.doc.scenario : "" })
                }
            }

            // --- 功能描述 ---
            Loader {
                Layout.fillWidth: true
                Layout.leftMargin: Tok.DesignTokens.space4
                Layout.rightMargin: Tok.DesignTokens.space4
                active: !!(root.doc && root.doc.description && String(root.doc.description).length > 0)
                sourceComponent: sectionComp
                onLoaded: {
                    item.sectionTitle = "功能描述"
                    item.sectionColor = Tok.DesignTokens.accentSuccess
                    item.sectionBody = Qt.binding(function(){ return root.doc ? root.doc.description : "" })
                }
            }

            // --- 参数详解 ---
            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Tok.DesignTokens.space4
                Layout.rightMargin: Tok.DesignTokens.space4
                spacing: Tok.DesignTokens.space2
                visible: !!(root.doc && root.doc.params && root.doc.params.length > 0)

                Label {
                    text: "参数详解"
                    color: Tok.DesignTokens.textPrimary
                    font.pixelSize: Tok.DesignTokens.fontSizeLg
                    font.bold: true
                    font.family: Tok.DesignTokens.fontFamilyCJK
                }

                Repeater {
                    model: (root.doc && root.doc.params) ? root.doc.params : []
                    delegate: Rectangle {
                        Layout.fillWidth: true
                        color: Tok.DesignTokens.bgSurface
                        border.color: Tok.DesignTokens.borderDefault
                        border.width: Tok.DesignTokens.borderWidth
                        radius: Tok.DesignTokens.radiusMd
                        implicitHeight: paramCol.implicitHeight + Tok.DesignTokens.space3 * 2
                        Layout.margins: 0

                        readonly property var param: modelData

                        ColumnLayout {
                            id: paramCol
                            anchors.fill: parent
                            anchors.margins: Tok.DesignTokens.space3
                            spacing: Tok.DesignTokens.space1

                            // 第一行：参数名 + 中文名 + 类型徽章
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Tok.DesignTokens.space2

                                Label {
                                    text: param.name || ""
                                    color: Tok.DesignTokens.textPrimary
                                    font.pixelSize: Tok.DesignTokens.fontSizeBase
                                    font.bold: true
                                    font.family: Tok.DesignTokens.fontMono
                                }
                                Label {
                                    text: param.cnName ? ("· " + param.cnName) : ""
                                    color: Tok.DesignTokens.textSecondary
                                    font.pixelSize: Tok.DesignTokens.fontSizeSm
                                    font.family: Tok.DesignTokens.fontFamilyCJK
                                    visible: !!(param.cnName)
                                }
                                Item { Layout.fillWidth: true }
                                // 类型徽章
                                Rectangle {
                                    visible: param.type !== undefined
                                    width: typeLbl.implicitWidth + 12
                                    height: 18
                                    radius: 9
                                    color: Qt.rgba(0.27, 0.18, 0.42, 0.5)  // 紫色半透明
                                    Label {
                                        id: typeLbl
                                        anchors.centerIn: parent
                                        text: root.paramTypeName(param.type)
                                        color: Tok.DesignTokens.accentPrimaryHover
                                        font.pixelSize: Tok.DesignTokens.fontSizeXs
                                        font.bold: true
                                        font.family: Tok.DesignTokens.fontMono
                                    }
                                }
                            }

                            // 第二行：默认值 / 取值范围 / 单位
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Tok.DesignTokens.space2
                                visible: {
                                    var hasDefault = param.defaultValue !== undefined && param.defaultValue !== ""
                                    var hasRange = (param.minValue !== undefined && param.minValue !== "")
                                                 || (param.maxValue !== undefined && param.maxValue !== "")
                                    var hasUnit = !!(param.unit)
                                    return hasDefault || hasRange || hasUnit
                                }

                                Label {
                                    visible: param.defaultValue !== undefined && param.defaultValue !== ""
                                    text: "默认: " + param.defaultValue
                                    color: Tok.DesignTokens.textTertiary
                                    font.pixelSize: Tok.DesignTokens.fontSizeXs
                                    font.family: Tok.DesignTokens.fontMono
                                }
                                Label {
                                    visible: (param.minValue !== undefined && param.minValue !== "")
                                           || (param.maxValue !== undefined && param.maxValue !== "")
                                    text: {
                                        var mn = (param.minValue !== undefined && param.minValue !== "") ? param.minValue : "-∞"
                                        var mx = (param.maxValue !== undefined && param.maxValue !== "") ? param.maxValue : "+∞"
                                        return "范围: [" + mn + ", " + mx + "]"
                                    }
                                    color: Tok.DesignTokens.textTertiary
                                    font.pixelSize: Tok.DesignTokens.fontSizeXs
                                    font.family: Tok.DesignTokens.fontMono
                                }
                                Label {
                                    visible: !!(param.unit)
                                    text: "单位: " + param.unit
                                    color: Tok.DesignTokens.textTertiary
                                    font.pixelSize: Tok.DesignTokens.fontSizeXs
                                    font.family: Tok.DesignTokens.fontFamilyCJK
                                }
                                Item { Layout.fillWidth: true }
                            }

                            // 第三行：参数帮助说明
                            Label {
                                Layout.fillWidth: true
                                visible: !!(param.help) && String(param.help).length > 0
                                text: param.help || ""
                                color: Tok.DesignTokens.textSecondary
                                font.pixelSize: Tok.DesignTokens.fontSizeSm
                                font.family: Tok.DesignTokens.fontFamilyCJK
                                lineHeight: Tok.DesignTokens.lineHeightRelax
                                lineHeightMode: Text.ProportionalHeight
                                wrapMode: Text.Wrap
                            }
                        }
                    }
                }
            }

            // --- 注意事项 ---
            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Tok.DesignTokens.space4
                Layout.rightMargin: Tok.DesignTokens.space4
                spacing: Tok.DesignTokens.space2
                visible: !!(root.doc && root.doc.caveats && root.doc.caveats.length > 0)

                Label {
                    text: "注意事项"
                    color: Tok.DesignTokens.textPrimary
                    font.pixelSize: Tok.DesignTokens.fontSizeLg
                    font.bold: true
                    font.family: Tok.DesignTokens.fontFamilyCJK
                }

                Repeater {
                    model: (root.doc && root.doc.caveats) ? root.doc.caveats : []
                    delegate: RowLayout {
                        Layout.fillWidth: true
                        spacing: Tok.DesignTokens.space2

                        Label {
                            text: "!"
                            color: Tok.DesignTokens.accentWarning
                            font.pixelSize: Tok.DesignTokens.fontSizeBase
                            font.bold: true
                            Layout.alignment: Qt.AlignTop
                        }
                        Label {
                            Layout.fillWidth: true
                            text: modelData
                            color: Tok.DesignTokens.textSecondary
                            font.pixelSize: Tok.DesignTokens.fontSizeSm
                            font.family: Tok.DesignTokens.fontFamilyCJK
                            lineHeight: Tok.DesignTokens.lineHeightRelax
                            lineHeightMode: Text.ProportionalHeight
                            wrapMode: Text.Wrap
                        }
                    }
                }
            }

            // --- 关联算子 ---
            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Tok.DesignTokens.space4
                Layout.rightMargin: Tok.DesignTokens.space4
                spacing: Tok.DesignTokens.space2
                visible: !!(root.doc && root.doc.relatedOperators && root.doc.relatedOperators.length > 0)

                Label {
                    text: "关联算子"
                    color: Tok.DesignTokens.textPrimary
                    font.pixelSize: Tok.DesignTokens.fontSizeLg
                    font.bold: true
                    font.family: Tok.DesignTokens.fontFamilyCJK
                }

                // 流式布局：用 Flow 容纳可点击的算子标签
                Flow {
                    Layout.fillWidth: true
                    spacing: Tok.DesignTokens.space2

                    Repeater {
                        model: (root.doc && root.doc.relatedOperators) ? root.doc.relatedOperators : []
                        delegate: Rectangle {
                            width: relatedLbl.implicitWidth + 20
                            height: 24
                            radius: 12
                            color: relatedArea.containsMouse
                                   ? Tok.DesignTokens.bgHover
                                   : Tok.DesignTokens.bgSurface
                            border.color: relatedArea.containsMouse
                                          ? Tok.DesignTokens.accentPrimary
                                          : Tok.DesignTokens.borderDefault
                            border.width: Tok.DesignTokens.borderWidth

                            Label {
                                id: relatedLbl
                                anchors.centerIn: parent
                                text: modelData
                                color: relatedArea.containsMouse
                                       ? Tok.DesignTokens.accentPrimaryHover
                                       : Tok.DesignTokens.textSecondary
                                font.pixelSize: Tok.DesignTokens.fontSizeXs
                                font.family: Tok.DesignTokens.fontMono
                            }
                            MouseArea {
                                id: relatedArea
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    // 点击关联算子 → 切换弹窗内容
                                    root.operatorType = modelData
                                }
                            }
                        }
                    }
                }
            }

            // 底部留白
            Item { Layout.preferredHeight: Tok.DesignTokens.space4; Layout.fillWidth: true }
        }
    }

    // ============ 通用分区组件（标题 + 正文）============
    Component {
        id: sectionComp
        ColumnLayout {
            id: sectionRoot
            property string sectionTitle: ""
            property string sectionBody: ""
            property color sectionColor: Tok.DesignTokens.accentPrimary
            Layout.fillWidth: true
            spacing: Tok.DesignTokens.space1

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: sectionTitleLbl.implicitHeight + 4
                color: "transparent"
                Rectangle {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    width: 3
                    height: 16
                    radius: 1.5
                    color: sectionRoot.sectionColor
                }
                Label {
                    id: sectionTitleLbl
                    anchors.left: parent.left
                    anchors.leftMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    text: sectionRoot.sectionTitle
                    color: Tok.DesignTokens.textPrimary
                    font.pixelSize: Tok.DesignTokens.fontSizeLg
                    font.bold: true
                    font.family: Tok.DesignTokens.fontFamilyCJK
                }
            }

            Label {
                Layout.fillWidth: true
                text: sectionRoot.sectionBody
                color: Tok.DesignTokens.textSecondary
                font.pixelSize: Tok.DesignTokens.fontSizeBase
                font.family: Tok.DesignTokens.fontFamilyCJK
                lineHeight: Tok.DesignTokens.lineHeightRelax
                lineHeightMode: Text.ProportionalHeight
                wrapMode: Text.Wrap
            }
        }
    }
}
