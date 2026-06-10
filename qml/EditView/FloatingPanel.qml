// =====================================================================
// FloatingPanel.qml — 可浮动/靠边隐藏的算子详情面板（v3.1.0）
//
// 功能：
// - 拖动标题栏移动面板
// - 靠边自动隐藏（任意边缘 < 30px 时隐藏，显示半透明箭头）
// - 点击箭头平滑展开（200-300ms 动画过渡）
// - 精确定位（基于 drawio 示意图坐标）
// =====================================================================

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QDV.EditView 3.0 as Tok

Rectangle {
    id: root

    // ============ 外部属性 ============
    property string panelTitle: "算子详情"
    // drawio 坐标：面板右下区 x=2630, y=1844, w=200, h=760
    // 基于主窗口 1310 的偏移量映射
    property real designX: 1320     // 2630 - 1310 = 1320（相对主窗口起始 x 的偏移）
    property real designY: 64       // 1844 - 1780 = 64（相对顶栏下边缘）
    property real designWidth: 200
    property real designHeight: 760

    // 运行时位置（首次使用设计坐标）
    property real panelX: designX
    property real panelY: designY

    // 边缘吸附阈值
    property real edgeThreshold: 30

    // ============ 状态 ============
    property bool isPinned: true      // 钉住（锁定位置）
    property bool isHidden: false     // 是否处于隐藏状态
    property int hiddenEdge: 0        // 0=无, 1=左, 2=右, 3=上, 4=下

    width: designWidth
    height: designHeight
    x: panelX
    y: panelY
    z: 100

    color: Tok.DesignTokens.bgPanel
    border.color: Tok.DesignTokens.borderDefault
    border.width: Tok.DesignTokens.borderWidth
    radius: Tok.DesignTokens.radiusLg

    // ============ 靠边检测 ============
    function checkEdgeProximity() {
        if (isPinned) return

        var parentW = parent ? parent.width : Screen.desktopAvailableWidth
        var parentH = parent ? parent.height : Screen.desktopAvailableHeight

        var distLeft   = panelX
        var distRight  = parentW - (panelX + width)
        var distTop    = panelY
        var distBottom = parentH - (panelY + height)

        if (distLeft < edgeThreshold && distLeft < distRight) {
            hiddenEdge = 1  // 靠左
            isHidden = true
        } else if (distRight < edgeThreshold && distRight < distLeft) {
            hiddenEdge = 2  // 靠右
            isHidden = true
        } else if (distTop < edgeThreshold && distTop < distBottom) {
            hiddenEdge = 3  // 靠上
            isHidden = true
        } else if (distBottom < edgeThreshold && distBottom < distTop) {
            hiddenEdge = 4  // 靠下
            isHidden = true
        } else {
            hiddenEdge = 0
            isHidden = false
        }
    }

    // ============ 展开动画 ============
    function expand() {
        if (!isHidden) return
        isHidden = false
        hiddenEdge = 0
        // 恢复到可视区域内
        ensureVisible()
    }

    function ensureVisible() {
        var parentW = parent ? parent.width : Screen.desktopAvailableWidth
        var parentH = parent ? parent.height : Screen.desktopAvailableHeight

        if (panelX < 0) panelX = 0
        if (panelY < 0) panelY = 0
        if (panelX + width > parentW) panelX = parentW - width
        if (panelY + height > parentH) panelY = parentH - height
    }

    // ============ 隐藏/显示动画 ============
    // 修复：Behavior 引用 dragArea 需判空，避免组件销毁阶段产生绑定警告
    Behavior on x {
        enabled: dragArea ? !dragArea.drag.active : true
        NumberAnimation { duration: 250; easing.type: Easing.OutCubic }
    }
    Behavior on y {
        enabled: dragArea ? !dragArea.drag.active : true
        NumberAnimation { duration: 250; easing.type: Easing.OutCubic }
    }
    Behavior on opacity {
        NumberAnimation { duration: 200; easing.type: Easing.InOutQuad }
    }

    // 隐藏状态下的位置偏移
    states: [
        State {
            name: "hidden_left"
            when: isHidden && hiddenEdge === 1
            PropertyChanges { target: root; x: -width + 24 }
            PropertyChanges { target: root; opacity: 0.85 }
        },
        State {
            name: "hidden_right"
            when: isHidden && hiddenEdge === 2
            PropertyChanges { target: root; x: parent ? parent.width - 24 : Screen.desktopAvailableWidth - 24 }
            PropertyChanges { target: root; opacity: 0.85 }
        },
        State {
            name: "hidden_top"
            when: isHidden && hiddenEdge === 3
            PropertyChanges { target: root; y: -height + 24 }
            PropertyChanges { target: root; opacity: 0.85 }
        },
        State {
            name: "hidden_bottom"
            when: isHidden && hiddenEdge === 4
            PropertyChanges { target: root; y: parent ? parent.height - 24 : Screen.desktopAvailableHeight - 24 }
            PropertyChanges { target: root; opacity: 0.85 }
        }
    ]

    // ============ 标题栏 ============
    Rectangle {
        id: titleBar
        width: parent.width
        height: 36
        color: Tok.DesignTokens.bgHeader
        radius: Tok.DesignTokens.radiusLg

        // 底部圆角切除
        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: Tok.DesignTokens.radiusLg
            color: Tok.DesignTokens.bgHeader
        }

        // 拖动区域
        MouseArea {
            id: dragArea
            anchors.fill: parent
            drag.target: root
            drag.minimumX: -root.width + 24
            drag.maximumX: (parent ? parent.width : Screen.desktopAvailableWidth) - 24
            drag.minimumY: 0
            drag.maximumY: (parent ? parent.height : Screen.desktopAvailableHeight) - root.height
            cursorShape: Qt.OpenHandCursor

            onPressed: cursorShape = Qt.ClosedHandCursor
            onReleased: {
                cursorShape = Qt.OpenHandCursor
                panelX = root.x
                panelY = root.y
                checkEdgeProximity()
            }
        }

        RowLayout {
            anchors.left: parent.left
            anchors.leftMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            spacing: 6

            Label {
                text: root.panelTitle
                color: Tok.DesignTokens.textPrimary
                font.pixelSize: Tok.DesignTokens.fontSizeSm
                font.bold: true
                font.family: Tok.DesignTokens.fontFamilyCJK
            }
        }

        RowLayout {
            anchors.right: parent.right
            anchors.rightMargin: 6
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2

            // 钉住按钮
            ToolButton {
                id: pinBtn
                text: isPinned ? "📌" : "📍"
                font.pixelSize: 12
                Layout.preferredWidth: 26
                Layout.preferredHeight: 26
                ToolTip.text: isPinned ? "取消固定" : "固定面板"
                ToolTip.visible: hovered

                background: Rectangle {
                    color: pinBtn.hovered ? Tok.DesignTokens.bgHover : "transparent"
                    radius: Tok.DesignTokens.radiusSm
                }
                onClicked: {
                    isPinned = !isPinned
                    if (!isPinned) checkEdgeProximity()
                }
            }

            // 关闭按钮
            ToolButton {
                id: closeBtn
                text: "✕"
                font.pixelSize: 12
                Layout.preferredWidth: 26
                Layout.preferredHeight: 26
                ToolTip.text: "关闭面板"
                ToolTip.visible: hovered

                background: Rectangle {
                    color: closeBtn.hovered ? Tok.DesignTokens.accentError : "transparent"
                    radius: Tok.DesignTokens.radiusSm
                }
                onClicked: root.visible = false
            }
        }
    }

    // ============ 展开提示箭头（隐藏态显示）============
    Rectangle {
        id: expandArrow
        visible: isHidden
        width: 20; height: 20
        radius: Tok.DesignTokens.radiusSm
        color: Qt.rgba(0.486, 0.302, 1.0, 0.5)  // 半透明紫色

        // 根据隐藏边缘定位
        anchors {
            horizontalCenter: hiddenEdge === 1 ? parent.right
                            : hiddenEdge === 2 ? parent.left
                            : parent.horizontalCenter
            verticalCenter: hiddenEdge === 3 ? parent.bottom
                          : hiddenEdge === 4 ? parent.top
                          : parent.verticalCenter
        }

        Label {
            anchors.centerIn: parent
            text: hiddenEdge === 1 ? "▶" : hiddenEdge === 2 ? "◀"
                : hiddenEdge === 3 ? "▼" : "▲"
            color: "#FFFFFF"
            font.pixelSize: 10
        }

        MouseArea {
            anchors.fill: parent
            anchors.margins: -4  // 扩大点击区域
            cursorShape: Qt.PointingHandCursor
            onClicked: root.expand()
        }
    }

    // ============ 内容区（由外部填充）============
    default property alias content: contentArea.data

    Item {
        id: contentArea
        anchors.top: titleBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 1
        clip: true
    }
}