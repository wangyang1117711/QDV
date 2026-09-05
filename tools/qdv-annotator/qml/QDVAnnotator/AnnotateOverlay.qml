// =====================================================================
// AnnotateOverlay.qml — 图像标注覆盖层（本地版 v3.1.0 + 画布编辑）
//
// 来源：主工程 qml/EditView/AnnotateOverlay.qml（本地副本，允许独立演进）
// 本版在保留原有「绘制」能力基础上，新增【画布内编辑】：
//   - 编辑模式（activeTool === ""）：点击选中已有标注（虚线描边+控制点）
//   - 拖拽框体移动；拖拽控制点缩放（矩形/圆形）；拖拽顶点编辑（多边形）
//   - Delete 键 / 右键菜单删除选中标注
//   - 悬停态高亮（编辑模式）
//
// 坐标约定：覆盖层尺寸 = 图像显示尺寸，内部坐标即【显示像素】，
//           由 AnnotationCanvas 负责 显示像素 ÷ scale ↔ 图像像素 转换。
//
// 外部接口（与原版兼容）：
//   annotations / activeTool / defaultColor / lineWidth / handleSize
//   signal annotationAdded(ann)
//   signal annotationUpdated(int index, ann)   // 移动/缩放/顶点编辑完成
//   signal annotationSelected(int index)       // index=-1 表示取消选中
//   signal annotationDeleted(int index)
//   函数 clearAnnotations() / setActiveTool(tool) / removeSelected()
// =====================================================================

import QtQuick
import QtQuick.Controls
import QDVAnnotator 1.0 as Tok

Item {
    id: root

    // ============ 外部接口 ============
    property var annotations: []     // [{type:"rect"|"circle"|"polygon", x,y,w,h, color, label}]
    property string activeTool: ""   // "rect" | "circle" | "polygon" | ""(=编辑模式)
    property color defaultColor: Tok.DesignTokens.accentPrimary
    property real lineWidth: 2
    property real handleSize: Tok.DesignTokens.annHandleSize
    // 向量矩形：创建时欲设定的根数（1~N，按 count 配色与标注）
    property int activeCount: 1

    signal annotationAdded(var annotation)
    signal annotationUpdated(int index, var annotation)
    signal annotationSelected(int index)
    signal annotationDeleted(int index)

    // ============ 绘制/编辑状态 ============
    property point dragStart: Qt.point(0, 0)
    property point dragCurrent: Qt.point(0, 0)
    property bool isDrawing: false
    property var polygonPoints: []   // 多边形临时点
    property int selectedIndex: -1

    // 编辑模式状态
    property string editMode: ""     // "" | "move" | "resize" | "vertex"
    property int editHandle: -1      // 当前拖拽的控制点索引
    property int editVertex: -1      // 多边形顶点索引
    property var editAnn: null       // 正在编辑的标注对象
    property var editOrig: null      // 编辑前快照（用于增量计算）
    property point editStart: Qt.point(0, 0)
    property int hoverIndex: -1      // 悬停命中的标注（编辑模式）

    clip: true

    // 键盘：Delete 删除选中标注，Esc 取消选中（编辑模式）
    focus: true
    Keys.onPressed: function (event) {
        if (event.key === Qt.Key_Delete) {
            root.removeSelected()
            event.accepted = true
        } else if (event.key === Qt.Key_Escape) {
            if (root.selectedIndex >= 0) {
                root.selectedIndex = -1
                root.annotationSelected(-1)
            }
            event.accepted = true
        }
    }

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
                drawAnnotation(ctx, ann, i === selectedIndex, i === hoverIndex)
            }

            // 绘制正在进行的标注
            if (isDrawing) {
                drawActiveAnnotation(ctx)
            }
        }

        function drawAnnotation(ctx, ann, isSelected, isHovered) {
            ctx.strokeStyle = ann.color || root.defaultColor
            ctx.lineWidth = isSelected ? root.lineWidth + 1 : root.lineWidth
            ctx.fillStyle = Qt.rgba(
                (ann.color || root.defaultColor).r,
                (ann.color || root.defaultColor).g,
                (ann.color || root.defaultColor).b,
                0.15
            )

            // 选中态：亮紫虚线描边
            if (isSelected) {
                ctx.setLineDash([6, 4])
                ctx.strokeStyle = Tok.DesignTokens.annSelectedOutline
            } else if (isHovered) {
                ctx.strokeStyle = Tok.DesignTokens.annHover
            }
            ctx.setLineDash([])

            switch (ann.type) {
            case "vec_rect":
                // 向量矩形（带方向）：中心(x,y)+角度angle+半长length1+半宽length2+根数count
                // 颜色按根数区分（与参考标注工具一致）；方向箭头沿 +angle 指示矢量方向
                root.drawVecRect(ctx, ann, isSelected)
                break

            case "rect":
                ctx.beginPath()
                ctx.rect(ann.x, ann.y, ann.w, ann.h)
                ctx.fill()
                ctx.stroke()
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
            if (ann.type === "polygon") {
                for (var i = 0; i < ann.points.length; i++)
                    drawHandle(ctx, ann.points[i].x, ann.points[i].y)
                return
            }
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
            ctx.fillStyle = Tok.DesignTokens.annHandleFill
            ctx.strokeStyle = Tok.DesignTokens.annHandleBorder
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
            case "vec_rect":
                // 创建预览：中心锚定在按下点，沿拖拽方向双向拉伸长轴
                var vdx = dragCurrent.x - dragStart.x, vdy = dragCurrent.y - dragStart.y
                var vL = Math.hypot(vdx, vdy)
                if (vL >= 2) {
                    var vang = Math.atan2(vdy, vdx) * 180 / Math.PI
                    var vcol = root.countColor(root.activeCount)
                    ctx.save()
                    ctx.translate(dragStart.x, dragStart.y); ctx.rotate(vang * Math.PI / 180)
                    ctx.strokeStyle = vcol
                    ctx.fillStyle = root.hexToRgba(vcol, 0.08)
                    ctx.beginPath(); ctx.rect(-vL, -6, 2 * vL, 12); ctx.fill(); ctx.stroke()
                    ctx.beginPath(); ctx.moveTo(0, 0); ctx.lineTo(vL, 0); ctx.stroke()
                    ctx.restore()
                }
                break

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

    // ============ 命中检测 ============
    function pointInPoly(px, py, pts) {
        var inside = false
        for (var i = 0, j = pts.length - 1; i < pts.length; j = i++) {
            var xi = pts[i].x, yi = pts[i].y, xj = pts[j].x, yj = pts[j].y
            if (((yi > py) !== (yj > py)) && (px < (xj - xi) * (py - yi) / (yj - yi) + xi))
                inside = !inside
        }
        return inside
    }

    // 命中「已有标注」：返回最上层（后绘制）的索引，未命中返回 -1
    function hitTestAnnotation(mx, my) {
        for (var i = root.annotations.length - 1; i >= 0; i--) {
            var a = root.annotations[i]
            if (a.type === "vec_rect") {
                if (root.pointInVecRect(mx, my, a, 4)) return i
            } else if (a.type === "rect") {
                if (mx >= a.x && mx <= a.x + a.w && my >= a.y && my <= a.y + a.h) return i
            } else if (a.type === "circle") {
                var cx = a.x + a.w / 2, cy = a.y + a.h / 2
                var rx = a.w / 2, ry = a.h / 2
                var dx = (mx - cx) / rx, dy = (my - cy) / ry
                if (dx * dx + dy * dy <= 1) return i
            } else if (a.type === "polygon") {
                if (a.points && a.points.length >= 3 && pointInPoly(mx, my, a.points)) return i
            }
        }
        return -1
    }

    // 命中「选中标注的控制点」：矩形/圆形返回 0..7（TL,T,TR,L,R,BL,B,BR），
    // 多边形返回顶点索引 0..n-1；未命中返回 -1
    function hitTestHandle(mx, my) {
        if (selectedIndex < 0 || selectedIndex >= root.annotations.length) return -1
        var a = root.annotations[selectedIndex]
        var hs = root.handleSize / 2 + 2
        if (a.type === "vec_rect") {
            var vh = root.vecRectHandles(a)
            var handlePts = [vh.center, vh.len, vh.wid, vh.rot]   // 0=移动 1=拉长 2=拉宽 3=旋转
            for (var v = 0; v < handlePts.length; v++) {
                if (Math.abs(handlePts[v].x - mx) <= hs && Math.abs(handlePts[v].y - my) <= hs) return v
            }
            return -1
        }
        if (a.type === "polygon") {
            if (!a.points) return -1
            for (var k = 0; k < a.points.length; k++) {
                if (Math.abs(a.points[k].x - mx) <= hs && Math.abs(a.points[k].y - my) <= hs) return k
            }
            return -1
        }
        var pts = [
            [a.x, a.y], [a.x + a.w / 2, a.y], [a.x + a.w, a.y],
            [a.x, a.y + a.h / 2], [a.x + a.w, a.y + a.h / 2],
            [a.x, a.y + a.h], [a.x + a.w / 2, a.y + a.h], [a.x + a.w, a.y + a.h]
        ]
        for (var j = 0; j < 8; j++) {
            if (Math.abs(pts[j][0] - mx) <= hs && Math.abs(pts[j][1] - my) <= hs) return j
        }
        return -1
    }

    // 深拷贝标注（多边形需深拷贝 points）
    function copyAnn(a) {
        var c = {}
        for (var k in a) c[k] = a[k]
        if (a.type === "polygon" && a.points) {
            c.points = []
            for (var i = 0; i < a.points.length; i++) c.points.push({ x: a.points[i].x, y: a.points[i].y })
        }
        return c
    }

    function clamp(v, lo, hi) { return Math.max(lo, Math.min(hi, v)) }

    // ============ 向量矩形几何（同参考工具 RECT2：中心 + l1/l2 + angle）============
    // 根数 → 颜色：1绿/2蓝/3橙/4红（与参考标注工具一致）
    function countColor(count) {
        switch (count) {
        case 1: return "#46c23d"
        case 2: return "#1a9ae5"
        case 3: return "#ff9900"
        case 4: return "#ff3366"
        }
        return "#ffffff"
    }
    // "#RRGGBB" → Qt.rgba（用于带透明度的填充）
    function hexToRgba(hex, alpha) {
        var h = (hex.charAt(0) === "#") ? hex.substring(1) : hex
        var r = parseInt(h.substring(0, 2), 16)
        var g = parseInt(h.substring(2, 4), 16)
        var b = parseInt(h.substring(4, 6), 16)
        return Qt.rgba(r / 255, g / 255, b / 255, alpha)
    }
    // 框体局部坐标轴：u=长轴方向(+angle)，v=宽轴方向(顺时针 90°)
    function boxAxes(ann) {
        var th = ann.angle * Math.PI / 180
        return { ux: Math.cos(th), uy: Math.sin(th) }
    }
    // 选中控制点：center=0(移动) / len=1(拉长) / wid=2(拉宽) / rot=3(旋转)
    function vecRectHandles(ann) {
        var ax = root.boxAxes(ann)
        var lr = ann.length1 + 24
        return {
            center: Qt.point(ann.x, ann.y),
            len:    Qt.point(ann.x + ax.ux * ann.length1,     ann.y + ax.uy * ann.length1),
            wid:    Qt.point(ann.x - ax.uy * ann.length2,     ann.y + ax.ux * ann.length2),
            rot:    Qt.point(ann.x + ax.ux * lr,              ann.y + ax.uy * lr)
        }
    }
    // 点是否落在向量矩形内（含阈值），先把点投影到框体局部坐标
    function pointInVecRect(mx, my, a, thr) {
        var ax = root.boxAxes(a)
        var dx = mx - a.x, dy = my - a.y
        var lu = dx * ax.ux + dy * ax.uy
        var lv = dx * (-ax.uy) + dy * ax.ux
        return Math.abs(lu) <= a.length1 + thr && Math.abs(lv) <= a.length2 + thr
    }
    function drawVecRect(ctx, ann, isSelected) {
        var col = root.countColor(ann.count)
        ctx.save()
        ctx.translate(ann.x, ann.y); ctx.rotate(ann.angle * Math.PI / 180)
        ctx.strokeStyle = col
        ctx.lineWidth = isSelected ? root.lineWidth + 1 : root.lineWidth
        ctx.fillStyle = root.hexToRgba(col, 0.15)
        ctx.beginPath(); ctx.rect(-ann.length1, -ann.length2, 2 * ann.length1, 2 * ann.length2)
        ctx.fill(); ctx.stroke()
        // 方向向量（沿 +angle）与箭头
        ctx.beginPath(); ctx.moveTo(0, 0); ctx.lineTo(ann.length1, 0)
        ctx.lineWidth = isSelected ? root.lineWidth + 0.5 : root.lineWidth
        ctx.stroke()
        ctx.fillStyle = col
        ctx.beginPath()
        ctx.moveTo(ann.length1, 0); ctx.lineTo(ann.length1 - 9, -6); ctx.lineTo(ann.length1 - 9, 6)
        ctx.closePath(); ctx.fill()
        ctx.restore()
        // 中心点 + 根数文本（黑色描边保证可读性）
        ctx.strokeStyle = col; ctx.lineWidth = 2
        ctx.beginPath(); ctx.arc(ann.x, ann.y, 3.5, 0, Math.PI * 2); ctx.stroke()
        ctx.font = "bold 12px " + Tok.DesignTokens.fontFamilyCJK
        ctx.lineWidth = 3; ctx.strokeStyle = "#000000"; ctx.fillStyle = "#FFFFFF"
        var txt = String(ann.count)
        ctx.strokeText(txt, ann.x + 8, ann.y - 9)
        ctx.fillText(txt, ann.x + 8, ann.y - 9)
        if (isSelected) root.drawVecRectHandles(ctx, ann)
    }
    function drawVecRectHandles(ctx, ann) {
        var h = root.vecRectHandles(ann)
        var hs = root.handleSize / 2
        ctx.save(); ctx.strokeStyle = "#2d6cdf"; ctx.fillStyle = "#2d6cdf"; ctx.lineWidth = 2
        // 中心 → 旋转点的虚线连接线
        ctx.setLineDash([3, 2])
        ctx.beginPath(); ctx.moveTo(h.center.x, h.center.y); ctx.lineTo(h.rot.x, h.rot.y); ctx.stroke()
        ctx.setLineDash([])
        function dot(p) { ctx.beginPath(); ctx.arc(p.x, p.y, hs + 1, 0, Math.PI * 2); ctx.fill() }
        dot(h.center); dot(h.rot); dot(h.len); dot(h.wid)
        ctx.restore()
    }

    // 矩形/圆形缩放：h 为 0..7 控制点索引，保证最小尺寸
    function resizeRect(ann, orig, h, mx, my) {
        var x0 = orig.x, y0 = orig.y
        var x1 = orig.x + orig.w, y1 = orig.y + orig.h
        if (h === 0 || h === 1 || h === 2) y0 = clamp(my, 0, y1 - 5)
        if (h === 5 || h === 6 || h === 7) y1 = clamp(my, y0 + 5, root.height)
        if (h === 0 || h === 3 || h === 5) x0 = clamp(mx, 0, x1 - 5)
        if (h === 2 || h === 4 || h === 7) x1 = clamp(mx, x0 + 5, root.width)
        ann.x = x0; ann.y = y0; ann.w = x1 - x0; ann.h = y1 - y0
    }

    // ============ 交互层 ============
    MouseArea {
        id: drawArea
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        cursorShape: activeTool !== "" ? Qt.CrossCursor : Qt.ArrowCursor

        onPressed: function(mouse) {
            root.forceActiveFocus()
            if (mouse.button === Qt.RightButton) {
                // 右键：完成多边形（绘制中）或 编辑菜单（已选中标注）
                if (activeTool === "polygon" && polygonPoints.length >= 2) {
                    var ann = {
                        type: "polygon",
                        points: polygonPoints.slice(),
                        closed: true,
                        color: root.defaultColor,
                        label: "区域 " + (root.annotations.length + 1)
                    }
                    root.annotationAdded(ann)
                    polygonPoints = []
                    isDrawing = false
                    annotationCanvas.requestPaint()
                } else if (root.selectedIndex >= 0) {
                    ctxMenu.open()
                }
                return
            }

            // ---- 编辑模式（activeTool === ""）：选中/移动/缩放 ----
            if (activeTool === "") {
                var hh = root.hitTestHandle(mouse.x, mouse.y)
                if (root.selectedIndex >= 0 && hh >= 0) {
                    var selType = root.annotations[root.selectedIndex].type
                    if (selType === "polygon") {
                        editMode = "vertex"; editVertex = hh
                    } else if (selType === "vec_rect") {
                        // 0=移动中心 1=拉长 2=拉宽 3=旋转
                        editMode = (hh === 3) ? "rot" : (hh === 1) ? "len" : (hh === 2) ? "wid" : "move"
                        editHandle = hh
                        editVertex = -1
                    } else {
                        editMode = "resize"; editHandle = hh
                    }
                    editAnn = root.annotations[root.selectedIndex]
                    editOrig = root.copyAnn(editAnn)
                    editStart = Qt.point(mouse.x, mouse.y)
                    return
                }
                var hitIdx = root.hitTestAnnotation(mouse.x, mouse.y)
                if (hitIdx >= 0) {
                    // Ctrl+点击：直接删除命中的标注（无需先选中再按 Delete）
                    if (mouse.modifiers & Qt.ControlModifier) {
                        root.selectedIndex = -1
                        root.annotationSelected(-1)
                        root.annotationDeleted(hitIdx)
                        return
                    }
                    if (root.selectedIndex !== hitIdx) {
                        root.selectedIndex = hitIdx
                        root.annotationSelected(hitIdx)
                    }
                    editMode = "move"
                    editAnn = root.annotations[hitIdx]
                    editOrig = root.copyAnn(editAnn)
                    editStart = Qt.point(mouse.x, mouse.y)
                    return
                }
                // 空白处：取消选中
                if (root.selectedIndex >= 0) {
                    root.selectedIndex = -1
                    root.annotationSelected(-1)
                }
                return
            }

            // ---- 绘制模式 ----
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
            // 编辑模式悬停态
            if (activeTool === "" && editMode === "") {
                var hi = root.hitTestAnnotation(mouse.x, mouse.y)
                if (hi !== root.hoverIndex) { root.hoverIndex = hi; annotationCanvas.requestPaint() }
            }

            // ---- 编辑拖拽 ----
            if (editMode === "move") {
                var dx = mouse.x - editStart.x, dy = mouse.y - editStart.y
                if (editAnn.type === "polygon") {
                    for (var m = 0; m < editAnn.points.length; m++) {
                        editAnn.points[m].x = root.clamp(editOrig.points[m].x + dx, 0, root.width)
                        editAnn.points[m].y = root.clamp(editOrig.points[m].y + dy, 0, root.height)
                    }
                } else if (editAnn.type === "vec_rect") {
                    // 向量矩形移动中心：允许到画布边缘
                    editAnn.x = root.clamp(editOrig.x + dx, 0, root.width)
                    editAnn.y = root.clamp(editOrig.y + dy, 0, root.height)
                } else {
                    editAnn.x = root.clamp(editOrig.x + dx, 0, root.width - editAnn.w)
                    editAnn.y = root.clamp(editOrig.y + dy, 0, root.height - editAnn.h)
                }
                annotationCanvas.requestPaint()
                return
            }
            // 向量矩形编辑：旋转 / 拉长 / 拉宽（旋转轴与几何以编辑前快照为中心）
            if (editMode === "rot") {
                editAnn.angle = Math.atan2(
                    mouse.y - editOrig.y, mouse.x - editOrig.x) * 180 / Math.PI
                annotationCanvas.requestPaint()
                return
            }
            if (editMode === "len") {
                editAnn.length1 = Math.max(5, Math.hypot(
                    mouse.x - editOrig.x, mouse.y - editOrig.y))
                annotationCanvas.requestPaint()
                return
            }
            if (editMode === "wid") {
                var vax = root.boxAxes(editOrig)
                var vlv = (mouse.x - editOrig.x) * (-vax.uy) + (mouse.y - editOrig.y) * vax.ux
                editAnn.length2 = Math.max(2, Math.abs(vlv))
                annotationCanvas.requestPaint()
                return
            }
            if (editMode === "resize") {
                root.resizeRect(editAnn, editOrig, editHandle, mouse.x, mouse.y)
                annotationCanvas.requestPaint()
                return
            }
            if (editMode === "vertex") {
                editAnn.points[editVertex].x = root.clamp(mouse.x, 0, root.width)
                editAnn.points[editVertex].y = root.clamp(mouse.y, 0, root.height)
                annotationCanvas.requestPaint()
                return
            }

            // ---- 绘制实时预览（非多边形）----
            if (isDrawing && activeTool !== "" && activeTool !== "polygon") {
                dragCurrent = Qt.point(mouse.x, mouse.y)
                annotationCanvas.requestPaint()
                return
            }
            // 多边形临时连接线
            if (activeTool === "polygon" && polygonPoints.length > 0) {
                dragCurrent = Qt.point(mouse.x, mouse.y)
                annotationCanvas.requestPaint()
            }
        }

        onExited: { root.hoverIndex = -1; if (editMode === "") annotationCanvas.requestPaint() }

        onReleased: function(mouse) {
            // ---- 编辑完成：写回 ----
            if (editMode !== "") {
                if (root.selectedIndex >= 0 && editAnn) {
                    root.annotations[root.selectedIndex] = editAnn
                    root.annotationUpdated(root.selectedIndex, root.copyAnn(editAnn))
                }
                editMode = ""; editHandle = -1; editVertex = -1
                editAnn = null; editOrig = null
                return
            }

            if (!isDrawing || activeTool === "polygon") return

            isDrawing = false

            // 向量矩形创建：中心锚定在按下点，沿拖拽方向双向拉伸长轴，根数取 activeCount
            if (activeTool === "vec_rect") {
                var vAx = dragCurrent.x - dragStart.x, vAy = dragCurrent.y - dragStart.y
                var vLen = Math.hypot(vAx, vAy)
                if (vLen < 5) { annotationCanvas.requestPaint(); return }
                var vAnn = {
                    type: "vec_rect",
                    x: dragStart.x, y: dragStart.y,
                    angle: Math.atan2(vAy, vAx) * 180 / Math.PI,
                    length1: vLen, length2: 6,
                    count: root.activeCount,
                    color: root.countColor(root.activeCount),
                    label: "方向矩形"
                }
                root.annotationAdded(vAnn)
                annotationCanvas.requestPaint()
                return
            }

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

    // 右键菜单：删除选中标注
    Menu {
        id: ctxMenu
        MenuItem {
            text: "删除选中标注 (Delete)"
            onTriggered: root.removeSelected()
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
        editMode = ""; editAnn = null; editOrig = null
        selectedIndex = -1
        annotationCanvas.requestPaint()
    }

    function setActiveTool(toolName) {
        activeTool = toolName
        polygonPoints = []
        isDrawing = false
        editMode = ""; editAnn = null; editOrig = null
        if (activeTool !== "" ) { selectedIndex = -1; root.annotationSelected(-1) }
        annotationCanvas.requestPaint()
    }

    // 删除当前选中的标注（返回被删索引；无选中返回 -1）
    function removeSelected() {
        if (selectedIndex < 0 || selectedIndex >= root.annotations.length) return -1
        var idx = selectedIndex
        selectedIndex = -1
        root.annotations.splice(idx, 1)
        root.annotationDeleted(idx)
        annotationCanvas.requestPaint()
        return idx
    }

    // 外部设置选中索引（双向联动：列表 → 画布）
    function selectIndex(i) {
        var idx = (i >= 0 && i < root.annotations.length) ? i : -1
        if (idx !== selectedIndex) { selectedIndex = idx; annotationCanvas.requestPaint() }
    }
}
