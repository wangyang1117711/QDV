// =====================================================================
// ImageViewer.qml — Halcon 风格图像浏览控件（v2.5.0 功能 4b）
//
// 功能：
// - 滚轮缩放（以鼠标为中心，0.1x ~ 20x）
// - 左键拖拽平移
// - 工具栏：适应窗口 / 1:1 / 放大 / 缩小 / 缩放百分比
// - 加载状态指示
//
// 用法：
//   ImageViewer {
//       imageSource: "file:///path/to/image.png"
//       anchors.fill: parent
//   }
//
// 参考：Halcon 图像预览窗口的缩放/平移交互
// =====================================================================

import QtQuick
import QtQuick.Controls
import QDV.EditView 3.0 as Tok

Rectangle {
    id: root
    color: Tok.DesignTokens.bgCanvas
    clip: true

    // ============ 外部接口 ============
    property url imageSource: ""
    property color bgColor: Tok.DesignTokens.bgCanvas
    property real minZoom: 0.1
    property real maxZoom: 20.0

    // ============ 内部状态 ============
    property real zoom: 1.0          // 当前缩放比例
    property real offsetX: 0         // 图像左上角 x（相对控件）
    property real offsetY: 0         // 图像左上角 y（相对控件）
    property real imageWidth: 0      // 图像原始宽度
    property real imageHeight: 0     // 图像原始高度
    property bool isDragging: false
    property real dragStartX: 0
    property real dragStartY: 0
    property real dragStartOffsetX: 0
    property real dragStartOffsetY: 0

    // ============ 图像元素 ============
    Image {
        id: image
        source: root.imageSource
        x: root.offsetX
        y: root.offsetY
        width: root.imageWidth * root.zoom
        height: root.imageHeight * root.zoom
        fillMode: Image.Stretch
        smooth: root.zoom < 2.0
        asynchronous: false  // v5.3：禁用异步加载，避免后台线程纹理与 QRhi 跨实例
        cache: false

        onStatusChanged: {
            if (status === Image.Ready) {
                root.imageWidth = sourceSize.width
                root.imageHeight = sourceSize.height
                fitToView()
            }
        }
    }

    // ============ 加载指示器 ============
    BusyIndicator {
        anchors.centerIn: parent
        running: image.status === Image.Loading
        visible: image.status === Image.Loading
    }

    // ============ 空图占位 ============
    Text {
        anchors.centerIn: parent
        text: "\uD83D\uDDBC\n\n未加载图像"
        color: Tok.DesignTokens.textSecondary
        font.pixelSize: 14
        horizontalAlignment: Text.AlignHCenter
        visible: imageSource.toString() === "" && image.status !== Image.Loading
    }

    // ============ 鼠标交互：滚轮缩放 + 拖拽平移 ============
    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton

        // 滚轮缩放（以鼠标为中心）
        onWheel: function(wheel) {
            if (image.status !== Image.Ready) return
            var oldZoom = root.zoom
            var newZoom = oldZoom
            if (wheel.angleDelta.y > 0) {
                newZoom = Math.min(root.maxZoom, oldZoom * 1.15)
            } else {
                newZoom = Math.max(root.minZoom, oldZoom / 1.15)
            }
            if (newZoom === oldZoom) return

            // 保持鼠标点对应的图像坐标不变
            // 鼠标在控件中的坐标 (mx, my)，对应图像坐标 ((mx-offsetX)/zoom, (my-offsetY)/zoom)
            // 缩放后新 offset = mx - imgCoordX * newZoom
            var mx = wheel.x
            var my = wheel.y
            var imgX = (mx - root.offsetX) / oldZoom
            var imgY = (my - root.offsetY) / oldZoom
            root.zoom = newZoom
            root.offsetX = mx - imgX * newZoom
            root.offsetY = my - imgY * newZoom
        }

        // 左键拖拽平移
        onPressed: function(mouse) {
            if (mouse.button === Qt.LeftButton) {
                root.isDragging = true
                root.dragStartX = mouse.x
                root.dragStartY = mouse.y
                root.dragStartOffsetX = root.offsetX
                root.dragStartOffsetY = root.offsetY
                cursorShape = Qt.ClosedHandCursor
            }
        }
        onPositionChanged: function(mouse) {
            if (root.isDragging) {
                root.offsetX = root.dragStartOffsetX + (mouse.x - root.dragStartX)
                root.offsetY = root.dragStartOffsetY + (mouse.y - root.dragStartY)
            }
        }
        onReleased: {
            root.isDragging = false
            cursorShape = Qt.ArrowCursor
        }

        // 右键重置为适应窗口
        onClicked: function(mouse) {
            if (mouse.button === Qt.RightButton) {
                fitToView()
            }
        }
    }

    // ============ 工具栏 ============
    Rectangle {
        id: toolbar
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.topMargin: 8
        anchors.rightMargin: 8
        width: 200
        height: 32
        color: Tok.DesignTokens.bgPanel
        opacity: 0.9
        radius: Tok.DesignTokens.radiusSm
        border.width: 1
        border.color: Tok.DesignTokens.borderDefault
        visible: image.status === Image.Ready

        Row {
            anchors.centerIn: parent
            spacing: 4

            // 适应窗口
            Button {
                text: "\uD83D\uDCC4"
                width: 28
                height: 24
                padding: 0
                ToolTip.text: "适应窗口"
                ToolTip.visible: hovered
                onClicked: fitToView()
            }

            // 1:1 原始尺寸
            Button {
                text: "1:1"
                width: 36
                height: 24
                padding: 0
                ToolTip.text: "原始尺寸"
                ToolTip.visible: hovered
                onClicked: resetZoom()
            }

            // 缩小
            Button {
                text: "\u2212"
                width: 24
                height: 24
                padding: 0
                ToolTip.text: "缩小"
                ToolTip.visible: hovered
                onClicked: zoomOut()
            }

            // 缩放百分比
            Text {
                width: 48
                height: 24
                verticalAlignment: Text.AlignVCenter
                horizontalAlignment: Text.AlignHCenter
                text: Math.round(root.zoom * 100) + "%"
                color: Tok.DesignTokens.textPrimary
                font.pixelSize: 11
            }

            // 放大
            Button {
                text: "+"
                width: 24
                height: 24
                padding: 0
                ToolTip.text: "放大"
                ToolTip.visible: hovered
                onClicked: zoomIn()
            }
        }
    }

    // ============ 函数 ============

    /// 适应窗口：计算最佳缩放比例使图像完整显示在控件中，居中
    function fitToView() {
        if (imageWidth === 0 || imageHeight === 0) return
        var sx = root.width / imageWidth
        var sy = root.height / imageHeight
        var fit = Math.min(sx, sy)
        root.zoom = Math.max(root.minZoom, Math.min(root.maxZoom, fit))
        root.offsetX = (root.width - imageWidth * root.zoom) / 2
        root.offsetY = (root.height - imageHeight * root.zoom) / 2
    }

    /// 重置为 1:1 原始尺寸，居中
    function resetZoom() {
        root.zoom = 1.0
        root.offsetX = (root.width - imageWidth) / 2
        root.offsetY = (root.height - imageHeight) / 2
    }

    /// 放大（以控件中心为基准）
    function zoomIn() {
        var oldZoom = root.zoom
        var newZoom = Math.min(root.maxZoom, oldZoom * 1.25)
        if (newZoom === oldZoom) return
        var cx = root.width / 2
        var cy = root.height / 2
        var imgX = (cx - root.offsetX) / oldZoom
        var imgY = (cy - root.offsetY) / oldZoom
        root.zoom = newZoom
        root.offsetX = cx - imgX * newZoom
        root.offsetY = cy - imgY * newZoom
    }

    /// 缩小（以控件中心为基准）
    function zoomOut() {
        var oldZoom = root.zoom
        var newZoom = Math.max(root.minZoom, oldZoom / 1.25)
        if (newZoom === oldZoom) return
        var cx = root.width / 2
        var cy = root.height / 2
        var imgX = (cx - root.offsetX) / oldZoom
        var imgY = (cy - root.offsetY) / oldZoom
        root.zoom = newZoom
        root.offsetX = cx - imgX * newZoom
        root.offsetY = cy - imgY * newZoom
    }

    /// 设置图像源
    function setImage(url) {
        root.imageSource = url
    }
}
