// =====================================================================
// AnnotateOverlay.qml — 图像标注覆盖层（v3.1.0）
//
// 功能：
// - 在图像上方叠加矩形、圆形、多边形标注
// - 支持拖动创建/调整标注
// - 标注颜色、线条粗细可配置
// - 标注列表数据绑定
// =====================================================================

import QtQuick
import QtQuick.Controls
import QDV.EditView 3.0 as Tok

Item {
    id: root

    // ============ 外部接口 ============
    property var annotations: []     // [{type:"rect"|"circle"|"polygon", x,y,w,h, color, label}]
    property string activeTool: ""   // "rect" | "circle" | "polygon" | ""
    property color defaultColor: Tok.DesignTokens.accentPrimary
    property real lineWidth: 2
    property real handleSize: 8

    signal annotationAdded(var annotation)
    signal annotationUpdated(int index, var annotation)
    signal annotationSelected(int index)

    // ============ 绘制状态 ============
    property point dragStart: Qt.point(0, 0)
    property point dragCurrent: Qt.point(0, 0)
    property bool isDrawing: false
    property var polygonPoints: []   // 多边形临时点
    property int selectedIndex: -1

    clip: true

    // ============ 标注渲染层 ============
    Canvas {
        id: annotationCanvas
        anchors.fill: parent
        visible: true
        z: 5

        onPaint: {
            var ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)

            // 绘制已有标注
            for (var i = 0; i < root.annotations.length; i++) {
                var ann = root.annotations[i]
                drawAnnotation(ctx, ann, i === selectedIndex)
            }

            // 绘制正在进行的标注
            if (isDrawing) {
                drawActiveAnnotation(ctx)
            }
        }

        function drawAnnotation(ctx, ann, isSelected) {
            ctx.strokeStyle = ann.color || root.defaultColor
            ctx.lineWidth = isSelected ? root.lineWidth + 1 : root.lineWidth
            ctx.fillStyle = Qt.rgba(
                (ann.color || root.defaultColor).r,
                (ann.color || root.defaultColor).g,
                (ann.color || root.defaultColor).b,
                0.15
            )

            switch (ann.type) {
            case "rect":
                ctx.beginPath()
                ctx.rect(ann.x, ann.y, ann.w, ann.h)
                ctx.fill()
                ctx.stroke()
                // 选中态控制点
                if (isSelected) drawHandles(ctx, ann)
                break

            case "circle":
                var cx = ann.x + ann.w / 2
                var cy = ann.y + ann.h / 2
                var rx = ann.w / 2
                var ry = ann.h / 2
                ctx.beginPath()
                ctx.ellipse(cx, cy, rx, ry)
                ctx.fill()
                ctx.stroke()
                if (isSelected) drawHandles(ctx, ann)
                break

            case "polygon":
                if (!ann.points || ann.points.length < 2) break
                ctx.beginPath()
                ctx.moveTo(ann.points[0].x, ann.points[0].y)
                for (var p = 1; p < ann.points.length; p++) {
                    ctx.lineTo(ann.points[p].x, ann.points[p].y)
                }
                if (ann.closed) ctx.closePath()
                ctx.fill()
                ctx.stroke()
                if (isSelected && ann.points) {
                    for (var j = 0; j < ann.points.length; j++) {
                        drawHandle(ctx, ann.points[j].x, ann.points[j].y)
                    }
                }
                break
            }

            // 标签
            if (ann.label) {
                ctx.font = "11px " + Tok.DesignTokens.fontFamilyCJK
                ctx.fillStyle = "#FFFFFF"
                ctx.strokeStyle = "#000000"
                ctx.lineWidth = 3
                var labelY = ann.y > 18 ? ann.y - 6 : ann.y + ann.h + 16
                ctx.strokeText(ann.label, ann.x + 2, labelY)
                ctx.fillText(ann.label, ann.x + 2, labelY)
            }
        }

        function drawHandles(ctx, ann) {
            drawHandle(ctx, ann.x, ann.y)
            drawHandle(ctx, ann.x + ann.w, ann.y)
            drawHandle(ctx, ann.x, ann.y + ann.h)
            drawHandle(ctx, ann.x + ann.w, ann.y + ann.h)
            drawHandle(ctx, ann.x + ann.w / 2, ann.y)
            drawHandle(ctx, ann.x, ann.y + ann.h / 2)
            drawHandle(ctx, ann.x + ann.w, ann.y + ann.h / 2)
            drawHandle(ctx, ann.x + ann.w / 2, ann.y + ann.h)
        }

        function drawHandle(ctx, x, y) {
            ctx.fillStyle = "#FFFFFF"
            ctx.strokeStyle = root.defaultColor
            ctx.lineWidth = 1.5
            var hs = root.handleSize / 2
            ctx.fillRect(x - hs, y - hs, hs * 2, hs * 2)
            ctx.strokeRect(x - hs, y - hs, hs * 2, hs * 2)
        }

        function drawActiveAnnotation(ctx) {
            ctx.strokeStyle = root.defaultColor
            ctx.lineWidth = root.lineWidth
            ctx.setLineDash([4, 4])
            ctx.fillStyle = Qt.rgba(
                root.defaultColor.r,
                root.defaultColor.g,
                root.defaultColor.b,
                0.08
            )

            var sx = Math.min(dragStart.x, dragCurrent.x)
            var sy = Math.min(dragStart.y, dragCurrent.y)
            var sw = Math.abs(dragCurrent.x - dragStart.x)
            var sh = Math.abs(dragCurrent.y - dragStart.y)

            switch (activeTool) {
            case "rect":
                ctx.beginPath()
                ctx.rect(sx, sy, sw, sh)
                ctx.fill()
                ctx.stroke()
                break

            case "circle":
                var cx2 = dragStart.x
                var cy2 = dragStart.y
                var r2 = Math.sqrt(
                    Math.pow(dragCurrent.x - dragStart.x, 2) +
                    Math.pow(dragCurrent.y - dragStart.y, 2)
                )
                ctx.beginPath()
                ctx.ellipse(cx2, cy2, r2, r2)
                ctx.fill()
                ctx.stroke()
                break

            case "polygon":
                // 绘制已放置的点和到当前鼠标位置的虚线
                if (polygonPoints.length > 0) {
                    ctx.beginPath()
                    ctx.moveTo(polygonPoints[0].x, polygonPoints[0].y)
                    for (var p = 1; p < polygonPoints.length; p++) {
                        ctx.lineTo(polygonPoints[p].x, polygonPoints[p].y)
                    }
                    ctx.lineTo(dragCurrent.x, dragCurrent.y)
                    ctx.stroke()
                    // 顶点小方块
                    for (var k = 0; k < polygonPoints.length; k++) {
                        var hs2 = 3
                        ctx.fillRect(polygonPoints[k].x - hs2, polygonPoints[k].y - hs2, hs2 * 2, hs2 * 2)
                    }
                }
                break
            }

            ctx.setLineDash([])
        }
    }

    // ============ 交互层 ============
    MouseArea {
        id: drawArea
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        cursorShape: activeTool !== "" ? Qt.CrossCursor : Qt.ArrowCursor

        onPressed: function(mouse) {
            if (mouse.button === Qt.RightButton && activeTool === "polygon") {
                // 右键完成多边形
                if (polygonPoints.length >= 2) {
                    var ann = {
                        type: "polygon",
                        points: polygonPoints.slice(),
                        closed: true,
                        color: root.defaultColor,
                        label: "区域 " + (root.annotations.length + 1)
                    }
                    root.annotationAdded(ann)
                }
                polygonPoints = []
                isDrawing = false
                annotationCanvas.requestPaint()
                return
            }

            if (activeTool === "") return

            dragStart = Qt.point(mouse.x, mouse.y)
            dragCurrent = dragStart

            if (activeTool === "polygon") {
                // 左键添加点
                polygonPoints.push(Qt.point(mouse.x, mouse.y))
                isDrawing = true
                annotationCanvas.requestPaint()
            } else {
                isDrawing = true
            }
        }

        onPositionChanged: function(mouse) {
            if (!isDrawing || activeTool === "polygon") {
                // 多边形不实时绘制轮廓，只更新临时连接线
                if (activeTool === "polygon" && polygonPoints.length > 0) {
                    dragCurrent = Qt.point(mouse.x, mouse.y)
                    annotationCanvas.requestPaint()
                }
                return
            }

            dragCurrent = Qt.point(mouse.x, mouse.y)
            annotationCanvas.requestPaint()
        }

        onReleased: function(mouse) {
            if (!isDrawing || activeTool === "polygon") return

            isDrawing = false
            var sx = Math.min(dragStart.x, dragCurrent.x)
            var sy = Math.min(dragStart.y, dragCurrent.y)
            var sw = Math.abs(dragCurrent.x - dragStart.x)
            var sh = Math.abs(dragCurrent.y - dragStart.y)

            // 忽略过小的标注（误点击）
            if (sw < 5 && sh < 5) {
                annotationCanvas.requestPaint()
                return
            }

            var ann = {
                type: activeTool,
                x: sx, y: sy, w: sw, h: sh,
                color: root.defaultColor,
                label: (activeTool === "rect" ? "矩形 " : "圆形 ") + (root.annotations.length + 1)
            }
            root.annotationAdded(ann)
            annotationCanvas.requestPaint()
        }
    }

    // ============ 刷新触发 ============
    onAnnotationsChanged: annotationCanvas.requestPaint()
    onSelectedIndexChanged: annotationCanvas.requestPaint()

    // ============ 公开方法 ============
    function clearAnnotations() {
        root.annotations = []
        polygonPoints = []
        isDrawing = false
        annotationCanvas.requestPaint()
    }

    function setActiveTool(toolName) {
        activeTool = toolName
        polygonPoints = []
        isDrawing = false
        annotationCanvas.requestPaint()
    }
}