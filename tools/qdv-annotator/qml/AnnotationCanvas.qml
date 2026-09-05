// =====================================================================
// AnnotationCanvas.qml — 图像画布（缩放/平移 + 标注绘制）
//
// 直接复用项目 QDV.EditView 的 AnnotateOverlay 作为绘制层：
// - AnnotateOverlay 覆盖在图像显示矩形之上，尺寸=图像显示尺寸，
//   因此其内部坐标即【显示像素】；绘制产生后除以 scale 即【图像像素】，
//   与后端 AnnotationSession 的存储约定（图像像素整数）严格一致。
// - 分类任务无几何标注，隐藏绘制层，由 LabelPanel 承担标注交互。
// =====================================================================
import QtQuick
import QtQuick.Controls
import QDVAnnotator 1.0 as Tok

Item {
    id: root
    clip: true

    property var image: ({})
    property string activeTool: ""      // overlayType："rect" | "polygon" | "vec_rect" | ""
    property int activeLabelId: -1
    // 向量矩形创建时欲设定的根数（1~N，按 count 配色与标注）
    property int activeCount: 1
    // 当前选中绘制工具对应的【形状 key】（如 rect/vec_rect/polygon/freeform）。
    // 用于区分同一 overlayType 下的多个形状（polygon 与 freeform 共用 "polygon" 绘制族）。
    property string activeShapeKey: ""
    property string taskType: "detection"
    // 画布当前选中标注索引（-1=无），供标注列表双向联动（Task 6）
    property int selectedIndex: -1

    property real scale: 1
    property real panX: 0
    property real panY: 0
    readonly property int imgW: (image && image.width) ? image.width : 0
    readonly property int imgH: (image && image.height) ? image.height : 0

    function fit() {
        if (imgW === 0 || imgH === 0) return
        var s = Math.min(root.width / imgW, root.height / imgH) * 0.95
        if (s <= 0) s = 1
        root.scale = s
        root.panX = (root.width - imgW * s) / 2
        root.panY = (root.height - imgH * s) / 2
    }
    // 最大缩放上限：保证画布/覆盖层(尺寸= imgW*scale)不超过渲染器最大纹理尺寸，
    // 否则放大到一定程度会整块变黑（图片与标注消失）。上限随图像尺寸自适应。
    function maxZoom() {
        var m = Math.max(root.imgW, root.imgH, 1)
        var cap = 6000
        return Math.min(20, Math.max(0.05, cap / m))
    }
    function zoomAt(factor, cx, cy) {
        var old = root.scale
        var ns = Math.max(0.05, Math.min(root.maxZoom(), old * factor))
        // 以 (cx,cy) 为锚点缩放
        var ix = (cx - root.panX) / old
        var iy = (cy - root.panY) / old
        root.scale = ns
        root.panX = cx - ix * ns
        root.panY = cy - iy * ns
    }

    Rectangle {
        anchors.fill: parent
        color: Tok.DesignTokens.bgCanvas
    }

    // 本地路径 → QML Image 可识别的 file:// URL（必须转正斜杠，否则反斜杠被 QML 丢弃导致加载失败）
    function toFileUrl(p) {
        if (!p) return ""
        return "file:///" + String(p).replace(/\\/g, "/")
    }

    // ---- S1：由覆盖层 overlayType 反查形状 key（查能力注册表，不再硬编码）----
    // 覆盖层绘制产生的标注 a.type 即注册表中的 overlayType
    //（rect->"rect"、vec_rect->"vec_rect"、polygon->"polygon"）。
    // 新增形状只需在 capabilities.json 中登记 overlayType，此处自动识别。
    function shapeKeyForOverlay(type) {
        var list = capabilities.shapes
        for (var i = 0; i < list.length; i++)
            if (list[i].overlayType === type) return list[i].key
        return ""
    }
    // 由形状 key 取几何族（rotated_box / box / polygon），未登记回退 box
    function geoOf(key) {
        var info = capabilities.shapeInfo(key)
        return (info && info.geo) ? info.geo : "box"
    }

    Image {
        id: photo
        // 自动预处理开启时显示增强图（与原图同尺寸，标注坐标对齐不受影响）
        source: (session && session.enhancedViewPath)
                ? session.enhancedViewPath
                : (root.image && root.image.path ? root.toFileUrl(root.image.path) : "")
        x: root.panX; y: root.panY
        width: root.imgW * root.scale
        height: root.imgH * root.scale
        fillMode: Image.Stretch
        smooth: root.scale < 2
        cache: false
        asynchronous: false
        // 加载完成（或失败）都触发 fit：失败时也按已知尺寸布局，保证标注坐标可预期
        onStatusChanged: {
            if (status === Image.Ready || status === Image.Error) root.fit()
        }
    }

    // 边缘叠加层（增强后的弱边界，青色高亮），叠加在原图/增强图之上
    Image {
        id: edgeOverlayImg
        source: (session && session.edgeOverlayPath) ? session.edgeOverlayPath : ""
        x: root.panX; y: root.panY
        width: root.imgW * root.scale
        height: root.imgH * root.scale
        fillMode: Image.Stretch
        smooth: false
        cache: false
        visible: (session && session.edgeOverlayPath !== "")
        z: 1
    }

    // 候选框（弱边缘增强辅助标注结果）：橙色虚框 + score 标签
    Repeater {
        id: candRepeater
        model: (session && session.candidateRegions) ? session.candidateRegions : []
        z: 2
        delegate: Rectangle {
            x: root.panX + modelData.x * root.scale
            y: root.panY + modelData.y * root.scale
            width: modelData.w * root.scale
            height: modelData.h * root.scale
            color: "transparent"
            border.color: Tok.DesignTokens.candidateBorder
            border.width: 2
            Label {
                text: (modelData.score !== undefined ? modelData.score.toFixed(2) : "")
                color: Tok.DesignTokens.candidateBorder
                font.family: Tok.DesignTokens.fontFamilyCJK
                font.pixelSize: 10
                anchors.top: parent.top; anchors.left: parent.left
                background: Rectangle { color: "#00000088"; radius: 2 }
                padding: 1
            }
        }
    }

    Tok.AnnotateOverlay {
        id: overlay
        x: root.panX; y: root.panY
        width: root.imgW * root.scale
        height: root.imgH * root.scale
        visible: root.taskType !== "classification"
        annotations: overlayAnnos
        activeTool: (root.taskType === "classification") ? "" : root.activeTool
        defaultColor: (root.activeLabelId >= 0) ? session.colorForLabel(root.activeLabelId) : Tok.DesignTokens.accentPrimary
        activeCount: root.activeCount

        onAnnotationAdded: function (a) {
            var s = root.scale
            var map = { labelId: root.activeLabelId }
            // S1：按注册表 overlayType 反查形状 key 与几何族，驱动分派。
            // 同 overlayType 多形状（如 freeform 与 polygon 共用 "polygon"）优先用当前工具
            // 的 activeShapeKey 精确落 key，避免反查命中第一个形状造成串类。
            var shp = ""
            if (root.activeShapeKey && capabilities.isShapeEnabled(root.activeShapeKey)
                && capabilities.overlayTypeOf(root.activeShapeKey) === a.type)
                shp = root.activeShapeKey
            if (!shp) shp = root.shapeKeyForOverlay(a.type)
            if (!shp) shp = (a.type === "polygon") ? "polygon" : "rect"   // 兼容未登记
            map.shape = shp
            var geo = root.geoOf(shp)
            if (geo === "rotated_box") {
                // 方向矩形：中心/半长/半宽随 scale 缩放，角度/根数为图像像素原值
                map.x = Math.round(a.x / s); map.y = Math.round(a.y / s)
                map.angle = a.angle
                map.length1 = a.length1 / s
                map.length2 = a.length2 / s
                map.count = a.count
            } else if (geo === "polygon") {
                var pts = []
                for (var i = 0; i < a.points.length; i++)
                    pts.push({ x: Math.round(a.points[i].x / s), y: Math.round(a.points[i].y / s) })
                map.points = pts
            } else {
                map.x = Math.round(a.x / s); map.y = Math.round(a.y / s)
                map.w = Math.round(a.w / s); map.h = Math.round(a.h / s)
            }
            session.addAnnotation(map)
        }

        // ---- 画布编辑（Task 3）：移动/缩放/顶点编辑完成 → 显示像素÷scale 回写图像像素 ----
        onAnnotationUpdated: function (index, a) {
            var s = root.scale
            var oldList = session.currentAnnotations()
            if (index < 0 || index >= oldList.length) return
            var map = {}
            var old = oldList[index]
            for (var k in old) map[k] = old[k]     // 保留 labelId/text/shape 等原字段
            // S1：编辑仅重算几何/坐标，形状 key 保留原值（map 已含 old.shape）。
            //      这避免同 overlayType 多形状（freeform/polygon）编辑时被反查改串 key。
            var geo = map.shape ? root.geoOf(map.shape) : "box"
            if (map.shape === "vec_rect" || geo === "rotated_box") {
                // 方向矩形：角度/根数原值，中心/半长/半宽 /scale 回写图像像素
                map.x = Math.round(a.x / s); map.y = Math.round(a.y / s)
                map.angle = a.angle
                map.length1 = a.length1 / s
                map.length2 = a.length2 / s
                map.count = a.count
            } else if (geo === "polygon") {
                var pts = []
                for (var i = 0; i < a.points.length; i++)
                    pts.push({ x: Math.round(a.points[i].x / s), y: Math.round(a.points[i].y / s) })
                map.points = pts
            } else {
                map.x = Math.round(a.x / s); map.y = Math.round(a.y / s)
                map.w = Math.round(a.w / s); map.h = Math.round(a.h / s)
            }
            session.updateAnnotation(index, map)
            root.selectedIndex = index
        }
        // 覆盖层内删除（右键菜单 / Delete 键）
        onAnnotationDeleted: function (index) {
            session.removeAnnotation(index)
            if (root.selectedIndex === index) root.selectedIndex = -1
            else if (root.selectedIndex > index) root.selectedIndex = root.selectedIndex - 1
        }
        // 画布点击选中 → 供列表联动（Task 6.2）
        onAnnotationSelected: function (index) {
            root.selectedIndex = index
            session.selectedAnnotationIndex = index
        }
    }

    // 列表选中 → 画布覆盖层显示选中态（Task 6.3）
    Connections {
        target: session
        function onSelectedAnnotationIndexChanged() {
            overlay.selectedIndex = session.selectedAnnotationIndex
            root.selectedIndex = session.selectedAnnotationIndex
        }
    }

    // ---- 坐标变换：后端图像像素 -> 显示像素，重建覆盖层 ----
    property var overlayAnnos: []
    function rebuild() {
        if (root.taskType === "classification") { overlayAnnos = []; return }
        var list = session.currentAnnotations()
        var out = []
        for (var i = 0; i < list.length; i++) {
            var ann = list[i]
            var col = session.colorForLabel(ann.labelId)
            var lbl = (ann.labelId >= 0 && ann.labelId < session.labels.length) ? session.labels[ann.labelId] : ""
            // S1：按注册表形状 key 查几何族，驱动绘制分派（不再硬编码 vec_rect/polygon）
            var geo = root.geoOf(ann.shape)
            if (geo === "rotated_box") {
                out.push({ type: capabilities.overlayTypeOf(ann.shape),
                           x: ann.x * root.scale, y: ann.y * root.scale,
                           angle: ann.angle,
                           length1: ann.length1 * root.scale, length2: ann.length2 * root.scale,
                           count: (ann.count !== undefined) ? ann.count : 1,
                           color: col, label: lbl })
            } else if (geo === "polygon") {
                var pts = []
                for (var j = 0; j < ann.points.length; j++)
                    pts.push({ x: ann.points[j].x * root.scale, y: ann.points[j].y * root.scale })
                out.push({ type: "polygon", points: pts, closed: true, color: col, label: lbl })
            } else {
                out.push({ type: "rect", x: ann.x * root.scale, y: ann.y * root.scale,
                           w: ann.w * root.scale, h: ann.h * root.scale, color: col, label: lbl })
            }
        }
        overlayAnnos = out
    }
    Connections {
        target: session
        function onCurrentImageChanged() { rebuild() }
        function onImagesChanged() { rebuild() }
    }
    // 缩放/平移变化后标注显示坐标需重算，否则 fit() 后标注仍按旧 scale 绘制
    onScaleChanged: rebuild()
    onPanXChanged: rebuild()
    onPanYChanged: rebuild()
    Component.onCompleted: rebuild()

    // ---- 缩放/平移交互（中键拖拽平移，滚轮缩放）----
    MouseArea {
        id: nav
        anchors.fill: parent
        acceptedButtons: Qt.MiddleButton
        hoverEnabled: true
        property real lastX: 0
        property real lastY: 0
        // 悬停位置（显示像素），供坐标读数
        property real hoverX: -1
        property real hoverY: -1
        onPressed: function (m) { lastX = m.x; lastY = m.y; cursorShape = Qt.ClosedHandCursor }
        onReleased: cursorShape = Qt.ArrowCursor
        onPositionChanged: function (m) {
            hoverX = m.x; hoverY = m.y
            if (pressed) {
                root.panX += m.x - lastX; root.panY += m.y - lastY
                lastX = m.x; lastY = m.y
            }
        }
        onExited: { hoverX = -1; hoverY = -1 }
        onWheel: function (w) {
            var f = w.angleDelta.y > 0 ? 1.1 : 0.9
            root.zoomAt(f, w.x, w.y)
        }
    }

    // ---- 悬停位置图像像素坐标文本（Task 5.1）：图像外显示 “—” ----
    readonly property string coordText: {
        var hx = nav.hoverX, hy = nav.hoverY
        if (hx < 0 || imgW === 0 || imgH === 0 || root.scale <= 0)
            return "x: —   y: —"
        var ix = Math.floor((hx - root.panX) / root.scale)
        var iy = Math.floor((hy - root.panY) / root.scale)
        if (ix < 0 || iy < 0 || ix >= imgW || iy >= imgH)
            return "x: —   y: —"
        return "x: " + ix + "   y: " + iy
    }

    // ---- 右下角信息区：坐标读数 + 缩放控制条（Task 5.2）----
    Column {
        anchors.right: parent.right; anchors.bottom: parent.bottom
        anchors.margins: 8
        spacing: 6
        z: 10
        // 坐标读数条
        Label {
            id: coordLabel
            anchors.right: parent.right
            text: root.coordText
            color: Tok.DesignTokens.textSecondary
            font.family: Tok.DesignTokens.fontFamilyCJK
            font.pixelSize: Tok.DesignTokens.fontSizeXs
            background: Rectangle { color: Tok.DesignTokens.bgSurface; radius: 2 }
            padding: 4
        }
        // 缩放控制条（与滚轮/中键缩放共用同一 scale/panX/panY 状态）
        Row {
            anchors.right: parent.right
            spacing: 2
            Button {
                text: "适配"
                padding: 4; implicitWidth: 44
                font.pixelSize: 12
                onClicked: root.fit()
                ToolTip.text: "适配窗口"
            }
            Button {
                text: "100%"
                padding: 4; implicitWidth: 44
                font.pixelSize: 12
                onClicked: { root.scale = 1; root.panX = 0; root.panY = 0 }
                ToolTip.text: "实际大小"
            }
            Button {
                text: "+"
                padding: 4; implicitWidth: 30
                font.pixelSize: 14
                onClicked: root.zoomAt(1.2, root.width / 2, root.height / 2)
                ToolTip.text: "放大"
            }
            Button {
                text: "−"
                padding: 4; implicitWidth: 30
                font.pixelSize: 14
                onClicked: root.zoomAt(0.8, root.width / 2, root.height / 2)
                ToolTip.text: "缩小"
            }
            Label {
                text: Math.round(root.scale * 100) + "%"
                color: Tok.DesignTokens.textSecondary
                font.family: Tok.DesignTokens.fontFamilyCJK
                font.pixelSize: Tok.DesignTokens.fontSizeXs
                anchors.verticalCenter: parent.verticalCenter
                leftPadding: 6
            }
        }
    }
}
