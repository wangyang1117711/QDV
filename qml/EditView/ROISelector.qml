// =====================================================================
// ROISelector.qml — ROI 选择器（v3.0.0 方案B Design Tokens 迁移）
//
// 设计：
// - 在 ParamForm 中作为 ParamType::ROI(5) 的子控件加载
// - 显示当前 ROI 文本（如 "(x=100, y=50, w=200, h=150)"）
// - "选择"按钮：M3.10 基础版打开内嵌文本编辑对话框
// - "清除"按钮：把 ROI 重置为 0/0/0/0
// - M5 阶段集成：与 RenderWidget 联动的画布拖框选择
// =====================================================================

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QDV.EditView 3.0 as Tok

Item {
    id: root
    implicitHeight: 32
    implicitWidth: 200

    /// 内部：spec
    property var spec
    /// 当前 ROI 值（字符串格式 "x,y,w,h"，与 QDV 内部约定一致）
    property string currentValue: ""

    // 与 ParamForm 兼容：暴露 paramName
    property string paramName: spec ? spec.name : ""

    signal valueEdited(string newValue)

    function _parseROI(text) {
        // "x,y,w,h" -> {x, y, w, h}
        if (!text) return null
        var parts = String(text).split(/[,;\s]+/)
        if (parts.length < 4) return null
        return {
            x: parseInt(parts[0]),
            y: parseInt(parts[1]),
            w: parseInt(parts[2]),
            h: parseInt(parts[3])
        }
    }

    function _formatROI(x, y, w, h) {
        return x + "," + y + "," + w + "," + h
    }

    RowLayout {
        anchors.fill: parent
        spacing: 6

        // ROI 文本输入框（可手动编辑）
        TextField {
            id: roiText
            Layout.fillWidth: true
            placeholderText: "x,y,w,h (如: 100,50,200,150)"
            text: root.currentValue
            color: Tok.DesignTokens.textPrimary
            font.pixelSize: Tok.DesignTokens.fontSizeBase
            font.family: Tok.DesignTokens.fontFamilyCJK
            selectByMouse: true
            background: Rectangle {
                color: roiText.activeFocus ? Tok.DesignTokens.bgHover : Tok.DesignTokens.bgSurface
                border.color: roiText.activeFocus ? Tok.DesignTokens.borderFocus : Tok.DesignTokens.borderDefault
                border.width: Tok.DesignTokens.borderWidth
                radius: Tok.DesignTokens.radiusSm
            }
            onEditingFinished: {
                if (text !== root.currentValue) {
                    root.valueEdited(text)
                }
            }
        }

        // 画布选择按钮（M5 阶段：与 RenderWidget 联动）
        ToolButton {
            text: "选择"
            enabled: true
            onClicked: {
                // M3.10 占位：弹出临时提示
                roiInfo.open()
            }
        }

        // 清除按钮
        ToolButton {
            text: "✕"
            enabled: root.currentValue.length > 0
            onClicked: {
                root.currentValue = ""
                root.valueEdited("")
            }
        }
    }

    // 简单的格式提示对话框（M3.10 占位）
    Popup {
        id: roiInfo
        modal: true
        anchors.centerIn: Overlay.overlay
        width: 360
        height: 180
        background: Rectangle {
            color: Tok.DesignTokens.bgPanel
            border.color: Tok.DesignTokens.accentPrimary
            border.width: 1
            radius: Tok.DesignTokens.radiusMd
        }
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 12
            spacing: 6
            Label {
                text: "ROI 选择（M3.10 基础版）"
                color: Tok.DesignTokens.accentPrimary
                font.bold: true
                font.pixelSize: 13
                font.family: Tok.DesignTokens.fontFamilyCJK
            }
            Label {
                text: "M3.10 阶段请直接编辑文本框：\n  x,y,w,h (整数像素坐标)"
                color: Tok.DesignTokens.textSecondary
                font.pixelSize: 11
                font.family: Tok.DesignTokens.fontFamilyCJK
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            Label {
                text: "M5 阶段将与 RenderWidget 联动，\n支持在画布上拖框选择 ROI。"
                color: Tok.DesignTokens.textPlaceholder
                font.pixelSize: 10
                font.family: Tok.DesignTokens.fontFamilyCJK
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            Item { Layout.fillHeight: true }
            Button {
                text: "关闭"
                Layout.alignment: Qt.AlignRight
                onClicked: roiInfo.close()
            }
        }
    }
}
