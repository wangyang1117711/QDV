// =====================================================================
// ROISelector.qml — ROI 选择器（v3.0.0 方案B Design Tokens 迁移）
//
// 设计：
// - 在 ParamForm 中作为 ParamType::ROI(5) 的子控件加载
// - 显示当前 ROI 文本（如 "(x=100, y=50, w=200, h=150)"）
// - "选择"按钮：M3.10 基础版打开内嵌文本编辑对话框
// - "清除"按钮：把 ROI 重置为空（全图）
// - M5 阶段集成：与 RenderWidget 联动的画布拖框选择
//
// spec 阶段一 Task 4 扩展：支持多边形 ROI
// - 矩形格式："x,y,w,h"（如 "100,50,200,150"）
// - 多边形格式："poly:x1,y1,x2,y2,x3,y3,..."（至少 3 个点）
// - 空字符串 = 全图（无 ROI，向后兼容）
// =====================================================================

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QDV.EditView 3.0 as Tok

Item {
    id: root
    implicitHeight: 32
    implicitWidth: 200

    // P1-B4-H8 a11y：无障碍属性
    Accessible.role: Accessible.Group
    Accessible.name: spec ? (spec.cnName + " ROI 选择器") : "ROI 选择器"
    Accessible.description: spec ? (spec.help || "输入 x,y,w,h 格式的矩形区域，或多边形 poly:x1,y1,...") : "ROI 区域选择"

    /// 内部：spec
    property var spec
    /// 当前 ROI 值（字符串格式）
    /// - 矩形："x,y,w,h"
    /// - 多边形："poly:x1,y1,x2,y2,..."
    /// - 空：全图（无 ROI）
    property string currentValue: ""

    // 与 ParamForm 兼容：暴露 paramName
    property string paramName: spec ? spec.name : ""

    signal valueEdited(string newValue)

    // 解析矩形 ROI："x,y,w,h" -> {type:"rect", x, y, w, h}
    function _parseRectROI(text) {
        if (!text) return null
        var parts = String(text).split(/[,;\s]+/).filter(function(s) { return s.length > 0 })
        if (parts.length < 4) return null
        var x = parseInt(parts[0])
        var y = parseInt(parts[1])
        var w = parseInt(parts[2])
        var h = parseInt(parts[3])
        if (isNaN(x) || isNaN(y) || isNaN(w) || isNaN(h)) return null
        return { type: "rect", x: x, y: y, w: w, h: h }
    }

    // 解析多边形 ROI："poly:x1,y1,x2,y2,..." -> {type:"polygon", points:[{x,y},...]}
    function _parsePolygonROI(text) {
        if (!text || String(text).indexOf("poly:") !== 0) return null
        var body = String(text).substring(5).trim()
        var parts = body.split(/[,;\s]+/).filter(function(s) { return s.length > 0 })
        if (parts.length < 6) return null  // 至少 3 个点（6 个数字）
        var points = []
        for (var i = 0; i + 1 < parts.length; i += 2) {
            var px = parseInt(parts[i])
            var py = parseInt(parts[i + 1])
            if (isNaN(px) || isNaN(py)) return null
            points.push({ x: px, y: py })
        }
        if (points.length < 3) return null
        return { type: "polygon", points: points }
    }

    // 兼容旧 _parseROI（矩形）
    function _parseROI(text) {
        var poly = _parsePolygonROI(text)
        if (poly) return poly
        return _parseRectROI(text)
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
            // P1-B4-H8 a11y
            Accessible.name: spec ? (spec.cnName + " ROI 坐标输入") : "ROI 坐标输入"
            Accessible.description: "矩形 x,y,w,h 或多边形 poly:x1,y1,x2,y2,..."
            placeholderText: "x,y,w,h 或 poly:x1,y1,..."
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
                // M3.10 占位：弹出格式提示
                roiInfo.open()
            }
        }

        // 清除按钮（重置为空 = 全图）
        ToolButton {
            text: "✕"
            enabled: root.currentValue.length > 0
            onClicked: {
                root.currentValue = ""
                root.valueEdited("")
            }
        }
    }

    // 简单的格式提示对话框（含多边形说明）
    Popup {
        id: roiInfo
        modal: true
        anchors.centerIn: Overlay.overlay
        width: 400
        height: 240
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
                text: "ROI 选择（支持矩形与多边形）"
                color: Tok.DesignTokens.accentPrimary
                font.bold: true
                font.pixelSize: 13
                font.family: Tok.DesignTokens.fontFamilyCJK
            }
            Label {
                text: "矩形 ROI：\n  x,y,w,h (整数像素坐标)\n  例如：100,50,200,150"
                color: Tok.DesignTokens.textSecondary
                font.pixelSize: 11
                font.family: Tok.DesignTokens.fontFamilyCJK
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            Label {
                text: "多边形 ROI：\n  poly:x1,y1,x2,y2,x3,y3,...\n  至少 3 个点，例如：poly:100,100,200,100,150,200"
                color: Tok.DesignTokens.textSecondary
                font.pixelSize: 11
                font.family: Tok.DesignTokens.fontFamilyCJK
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            Label {
                text: "留空 = 全图（无 ROI 限制）\nM5 阶段将与 RenderWidget 联动画布选择"
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
