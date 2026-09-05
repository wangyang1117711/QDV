// =====================================================================
// WideComboBox.qml — 内容自适应宽度下拉框
//
// 背景：QtQuick.Controls 的 ComboBox 默认弹出列表宽度 = 控件宽度
// （Basic 样式 popup.width: control.width），当选项文本较长（如模型名
// "YOLOv11n-OBB (旋转矩形)"、导出格式 "YOLO 有向矩形 OBB (yolo_obb) →
// 旋转矩形/RotatedDetectDl"）时，下拉项被横向截断，无法看清完整名称。
//
// 实现：覆盖 popup 使其宽度 = max(控件宽度, 最长选项文本宽度 + 内边距)，
// 且不超过屏幕宽度（多分辨率适配）。Basic 样式 delegate 宽度绑定
// ListView.view.width，弹层变宽后各选项文本自动完整显示；高度沿用原版
// 自适应（不超过窗口高度）。
//
// 特性：
//   1) 弹出列表宽度自适应最长选项，保证任意长度选项名完整显示；
//   2) 控件自身可视宽度不足、当前项被省略号截断时，悬停 ToolTip 显示
//      完整当前文本；
//   3) 完全兼容原生 ComboBox API（model/textRole/valueRole/currentIndex/
//      onActivated/onCurrentIndexChanged 等），可直接替换原生 ComboBox。
// =====================================================================
import QtQuick
import QtQuick.Controls
import QtQuick.Window

ComboBox {
    id: control

    // ---- 隐藏文本测量探针：以控件字体测量各选项文本宽度 ----
    Text {
        id: probe
        visible: false
        font: control.font
    }

    // 最长选项文本宽度（像素）
    function longestTextWidth() {
        var w = 0
        for (var i = 0; i < control.count; ++i) {
            probe.text = control.textAt(i)
            if (probe.implicitWidth > w) w = probe.implicitWidth
        }
        return w
    }

    // 当前项是否被控件可视宽度截断（elide 生效）
    function isTruncated() {
        return control.contentItem.truncated
    }

    // ---- 弹层：宽度自适应最长选项，且不超过屏幕宽度 ----
    // 注：宽度/高度不用绑定表达式（会产生 popup.width ↔ implicitHeight 绑定循环），
    // 改为打开瞬间用 JS 一次性计算赋值，既满足自适应又干净无警告。
    popup: Popup {
        id: pop
        y: control.height
        topMargin: 6
        bottomMargin: 6
        palette: control.palette

        onOpened: {
            // = (最长选项文本 + 左右内边距)，且不小于控件自身宽度；不超过屏幕宽度
            var w = Math.max(control.width,
                             control.longestTextWidth()
                             + control.leftPadding + control.rightPadding + 8)
            pop.width = Math.min(Screen.width - 8, w)
            // 高度 = 内容总高，上限为窗口剩余高度
            var maxH = Math.max(40, control.Window.height - topMargin - bottomMargin)
            pop.height = Math.min(contentItem.implicitHeight, maxH)
        }

        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.delegateModel
            currentIndex: control.highlightedIndex
            highlightMoveDuration: 0
            ScrollIndicator.vertical: ScrollIndicator {}
        }

        background: Rectangle {
            color: control.palette.window
        }
    }

    // 当前选中项完整内容悬停提示（内容被截断时）
    ToolTip {
        parent: control.contentItem
        visible: control.hovered && control.isTruncated()
        text: control.currentText
        delay: 400
    }
}
