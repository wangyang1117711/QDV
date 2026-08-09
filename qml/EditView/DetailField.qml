// =====================================================================
// DetailField.qml — 模型详情字段组件（v2.6.0 模型库对话框）
//
// 功能：以"标签 + 值"两行结构展示模型详情的单个字段。
//   - 第一行：灰色小号标签
//   - 第二行：主色正文，超长自动省略，SHA256 等长字段允许换行
//
// 入口（由 ModelLibraryDialog 实例化）：
//   DetailField { label: "文件名"; text: "yolov5s.onnx" }
// =====================================================================

import QtQuick
import QtQuick.Layouts
import QDV.EditView 3.0 as Tok

ColumnLayout {
    id: root
    spacing: Tok.DesignTokens.space1

    // === 外部接口 ===
    property string labelText: ""    // 字段标签
    property string valueText: ""    // 字段值
    property bool wrapValue: false   // 值是否允许换行（SHA256 等长字段用）

    // 标签行（灰色小字）
    Text {
        text: root.labelText
        color: Tok.DesignTokens.textTertiary
        font.pixelSize: Tok.DesignTokens.fontSizeXs
        font.family: Tok.DesignTokens.fontFamily
        Layout.fillWidth: true
    }

    // 值行（主色正文）
    Text {
        text: root.valueText || "—"
        color: root.valueText.length > 0 ? Tok.DesignTokens.textPrimary
                                        : Tok.DesignTokens.textDisabled
        font.pixelSize: Tok.DesignTokens.fontSizeSm
        font.family: root.labelText === "SHA256" ? Tok.DesignTokens.fontMono
                                                 : Tok.DesignTokens.fontFamily
        Layout.fillWidth: true
        elide: root.wrapValue ? Text.ElideNone : Text.ElideRight
        wrapMode: root.wrapValue ? Text.WrapAnywhere : Text.NoWrap
    }
}
