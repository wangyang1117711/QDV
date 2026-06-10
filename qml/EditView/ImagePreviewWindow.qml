// =====================================================================
// ImagePreviewWindow.qml — 独立图像预览浮窗（v3.1.0）
//
// 功能：
// - 双击效果预览图时独立弹出，显示原图 & 处理后图像
// - 标题栏拖动、最小化（收缩为标题栏条状）、最大化（铺满工作区）
// - 透明度滑块 30%-100% 范围调节
// - 始终最前显示（z >= 1000），不影响主界面交互
// - 设计尺寸基于 drawio 示意图（效果预览图 80x88 区域，独立窗口默认 640x480）
// =====================================================================

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import QDV.EditView 3.0 as Tok

Window {
    id: previewWindow
    flags: Qt.Window | Qt.WindowStaysOnTopHint | Qt.FramelessWindowHint
    width: 640
    height: 480
    minimumWidth: 320
    minimumHeight: 200
    visible: false
    color: "transparent"

    // ============ 外部接口 ============
    property url   sourceImage: ""       // 原图路径
    property url   processedImage: ""    // 处理后图像路径
    property string imageTitle: "效果预览"
    property real  currentOpacity: 1.0   // 当前透明度

    // ============ 状态 ============
    property bool isMinimized: false
    property bool isMaximized: false
    property real normalX: 0
    property real normalY: 0
    property real normalW: 640
    property real normalH: 480

    // ============ 标题栏高度常量 ============
    readonly property real titleBarHeight: 36

    // ============ 拖拽状态 ============
    property point dragOffset: Qt.point(0, 0)

    // z-index >= 1000 通过 StaysOnTopHint 保证

    // ============ 主体容器 ============
    Rectangle {
        id: mainContainer
        anchors.fill: parent
        anchors.margins: 1
        color: Tok.DesignTokens.bgPanel
        border.color: Tok.DesignTokens.accentPrimary
        border.width: 2
        radius: Tok.DesignTokens.radiusLg
        opacity: previewWindow.currentOpacity
        clip: true

        // ============ 最小化/最大化状态：作用于 Window（QML Window 无 states 属性）============
        // 修复：把状态机迁移到内部 Rectangle 上，因为它继承自 Item
        states: [
            State {
                name: "minimized"
                when: previewWindow.isMinimized
                PropertyChanges {
                    target: previewWindow
                    width: previewWindow.normalW
                    height: previewWindow.titleBarHeight + 2
                }
            },
            State {
                name: "maximized"
                when: previewWindow.isMaximized && !previewWindow.isMinimized
                PropertyChanges {
                    target: previewWindow
                    x: 0
                    y: 0
                    width: Screen.desktopAvailableWidth
                    height: Screen.desktopAvailableHeight
                }
            }
        ]

        // ============ 标题栏 ============
        Rectangle {
            id: titleBar
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: titleBarHeight
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

            // 标题栏拖动
            MouseArea {
                anchors.fill: parent
                property point clickPos: Qt.point(0, 0)
                cursorShape: Qt.OpenHandCursor
                onPressed: {
                    if (previewWindow.isMaximized) {
                        // 从最大化拖动时先还原，并保持鼠标相对位置不变
                        previewWindow.isMaximized = false
                        previewWindow.x = mouseX
                        previewWindow.y = mouseY
                    }
                    clickPos = Qt.point(mouseX, mouseY)
                    cursorShape = Qt.ClosedHandCursor
                }
                onPositionChanged: {
                    var dx = mouseX - clickPos.x
                    var dy = mouseY - clickPos.y
                    previewWindow.x += dx
                    previewWindow.y += dy
                }
                onReleased: cursorShape = Qt.OpenHandCursor
                // 双击标题栏切换最大化
                onDoubleClicked: toggleMaximize()
            }

            RowLayout {
                anchors.left: parent.left
                anchors.leftMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                spacing: 6

                // 图标
                Rectangle {
                    width: 18; height: 18
                    radius: 3
                    color: Tok.DesignTokens.accentPrimary
                    Label {
                        anchors.centerIn: parent
                        text: "🖼"
                        font.pixelSize: 10
                    }
                }

                Label {
                    text: previewWindow.imageTitle
                    color: Tok.DesignTokens.textPrimary
                    font.pixelSize: Tok.DesignTokens.fontSizeSm
                    font.bold: true
                    font.family: Tok.DesignTokens.fontFamilyCJK
                }
            }

            // 窗口控制按钮 (— □ ✕)
            RowLayout {
                anchors.right: parent.right
                anchors.rightMargin: 4
                anchors.verticalCenter: parent.verticalCenter
                spacing: 2

                // 最小化 —
                ToolButton {
                    id: minBtn
                    text: "—"
                    font.pixelSize: 12
                    Layout.preferredWidth: 28
                    Layout.preferredHeight: 26
                    ToolTip.text: "最小化"
                    ToolTip.visible: hovered

                    background: Rectangle {
                        color: minBtn.hovered ? Tok.DesignTokens.bgHover : "transparent"
                        radius: Tok.DesignTokens.radiusSm
                    }
                    onClicked: toggleMinimize()
                }

                // 最大化 □
                ToolButton {
                    id: maxBtn
                    text: isMaximized ? "❐" : "□"
                    font.pixelSize: 12
                    Layout.preferredWidth: 28
                    Layout.preferredHeight: 26
                    ToolTip.text: isMaximized ? "还原" : "最大化"
                    ToolTip.visible: hovered

                    background: Rectangle {
                        color: maxBtn.hovered ? Tok.DesignTokens.bgHover : "transparent"
                        radius: Tok.DesignTokens.radiusSm
                    }
                    onClicked: toggleMaximize()
                }

                // 关闭 ✕
                ToolButton {
                    id: closeBtn
                    text: "✕"
                    font.pixelSize: 12
                    Layout.preferredWidth: 28
                    Layout.preferredHeight: 26
                    ToolTip.text: "关闭"
                    ToolTip.visible: hovered

                    background: Rectangle {
                        color: closeBtn.hovered ? Tok.DesignTokens.accentError : "transparent"
                        radius: Tok.DesignTokens.radiusSm
                    }
                    onClicked: previewWindow.visible = false
                }
            }
        }

        // ============ 透明度控制栏 ============
        Rectangle {
            id: opacityBar
            anchors.top: titleBar.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            height: 28
            color: Tok.DesignTokens.bgHeader
            visible: !isMinimized

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                spacing: 6

                Label {
                    text: "透明度"
                    color: Tok.DesignTokens.textSecondary
                    font.pixelSize: Tok.DesignTokens.fontSizeXs
                    font.family: Tok.DesignTokens.fontFamilyCJK
                }

                Slider {
                    id: opacitySlider
                    Layout.fillWidth: true
                    from: 0.3
                    to: 1.0
                    // 修复：避免 Slider.value ↔ currentOpacity 双向绑定循环
                    // 初始化时从 currentOpacity 拉取一次，之后由 onMoved 单向回写
                    value: Math.max(0.3, Math.min(1.0, previewWindow.currentOpacity))
                    stepSize: 0.01

                    background: Rectangle {
                        x: opacitySlider.leftPadding
                        y: opacitySlider.topPadding + opacitySlider.availableHeight / 2 - height / 2
                        implicitWidth: 120
                        implicitHeight: 4
                        width: opacitySlider.availableWidth
                        height: implicitHeight
                        radius: 2
                        color: Tok.DesignTokens.bgSurface

                        Rectangle {
                            width: opacitySlider.visualPosition * parent.width
                            height: parent.height
                            radius: 2
                            color: Tok.DesignTokens.accentPrimary
                        }
                    }

                    handle: Rectangle {
                        x: opacitySlider.leftPadding + opacitySlider.visualPosition * (opacitySlider.availableWidth - width)
                        y: opacitySlider.topPadding + opacitySlider.availableHeight / 2 - height / 2
                        implicitWidth: 14
                        implicitHeight: 14
                        radius: 7
                        color: opacitySlider.pressed ? Tok.DesignTokens.accentPrimaryHover : Tok.DesignTokens.accentPrimary
                        border.color: "#FFFFFF"
                        border.width: 1
                    }

                    // 仅在用户主动拖动时回写，避免与 value 绑定冲突
                    onMoved: previewWindow.currentOpacity = value
                }

                Label {
                    text: Math.round(opacitySlider.value * 100) + "%"
                    color: Tok.DesignTokens.textSecondary
                    font.pixelSize: Tok.DesignTokens.fontSizeXs
                    Layout.preferredWidth: 30
                    horizontalAlignment: Text.AlignRight
                }
            }
        }

        // ============ 图像显示区 ============
        Rectangle {
            id: imageArea
            anchors.top: opacityBar.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: statusBar.top
            color: Tok.DesignTokens.bgCanvas
            visible: !isMinimized
            clip: true

            // 双图并排显示
            RowLayout {
                anchors.centerIn: parent
                spacing: 12

                // 原图
                ColumnLayout {
                    spacing: 4
                    Layout.alignment: Qt.AlignCenter

                    Rectangle {
                        Layout.preferredWidth: Math.min(280, imageArea.width / 2 - 20)
                        Layout.preferredHeight: Layout.preferredWidth

                        color: "#2c3e50"
                        border.color: "#3a3a4e"
                        border.width: 1

                        Image {
                            id: originalImage
                            anchors.fill: parent
                            anchors.margins: 1
                            source: previewWindow.sourceImage
                            fillMode: Image.PreserveAspectFit
                            cache: false
                            visible: sourceImage != ""
                        }

                        Label {
                            anchors.centerIn: parent
                            text: "原图"
                            color: "#666"
                            font.pixelSize: 14
                            visible: originalImage.source == ""
                        }
                    }

                    Label {
                        text: "原图"
                        color: Tok.DesignTokens.textSecondary
                        font.pixelSize: Tok.DesignTokens.fontSizeXs
                        horizontalAlignment: Text.AlignHCenter
                        Layout.alignment: Qt.AlignHCenter
                    }
                }

                // 处理后的图
                ColumnLayout {
                    spacing: 4
                    Layout.alignment: Qt.AlignCenter

                    Rectangle {
                        Layout.preferredWidth: Math.min(280, imageArea.width / 2 - 20)
                        Layout.preferredHeight: Layout.preferredWidth

                        color: "#34495e"
                        border.color: "#3a3a4e"
                        border.width: 1

                        Image {
                            id: processedImg
                            anchors.fill: parent
                            anchors.margins: 1
                            source: previewWindow.processedImage
                            fillMode: Image.PreserveAspectFit
                            cache: false
                            visible: processedImage != ""
                        }

                        Label {
                            anchors.centerIn: parent
                            text: "滤波后"
                            color: "#666"
                            font.pixelSize: 14
                            visible: processedImg.source == ""
                        }
                    }

                    Label {
                        text: "滤波后"
                        color: Tok.DesignTokens.textSecondary
                        font.pixelSize: Tok.DesignTokens.fontSizeXs
                        horizontalAlignment: Text.AlignHCenter
                        Layout.alignment: Qt.AlignHCenter
                    }
                }
            }
        }

        // ============ 状态栏 ============
        Rectangle {
            id: statusBar
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            height: 22
            color: Tok.DesignTokens.bgHeader
            visible: !isMinimized

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8

                Label {
                    text: "双击标题栏切换最大化 | 拖拽标题栏移动窗口"
                    color: Tok.DesignTokens.textTertiary
                    font.pixelSize: Tok.DesignTokens.fontSizeXs
                    font.family: Tok.DesignTokens.fontFamilyCJK
                }

                Item { Layout.fillWidth: true }

                // 窗口尺寸标注
                Label {
                    text: Math.round(previewWindow.width) + "×" + Math.round(previewWindow.height)
                    color: Tok.DesignTokens.textTertiary
                    font.pixelSize: Tok.DesignTokens.fontSizeXs
                }
            }
        }
    }

    // ============ 功能方法 ============
    function toggleMinimize() {
        if (isMaximized) {
            isMaximized = false
        }
        if (!isMinimized) {
            // 保存当前尺寸，收缩为标题栏高度
            normalX = previewWindow.x
            normalY = previewWindow.y
            normalW = previewWindow.width
            normalH = previewWindow.height
            isMinimized = true
        } else {
            // 恢复到保存的尺寸
            isMinimized = false
            previewWindow.x = normalX
            previewWindow.y = normalY
            previewWindow.width = normalW
            previewWindow.height = normalH
        }
    }

    function toggleMaximize() {
        if (isMinimized) {
            isMinimized = false
        }
        if (!isMaximized) {
            // 保存当前位置和尺寸
            normalX = previewWindow.x
            normalY = previewWindow.y
            normalW = previewWindow.width
            normalH = previewWindow.height
            isMaximized = true
        } else {
            // 恢复到保存的尺寸
            isMaximized = false
            previewWindow.x = normalX
            previewWindow.y = normalY
            previewWindow.width = normalW
            previewWindow.height = normalH
        }
    }

    function open() {
        // 首次打开居中；后续打开保留上次位置（normalX/normalY 已在 open/close 周期维护）
        if (normalX === 0 && normalY === 0) {
            previewWindow.x = (Screen.desktopAvailableWidth - previewWindow.width) / 2
            previewWindow.y = (Screen.desktopAvailableHeight - previewWindow.height) / 2
            normalX = previewWindow.x
            normalY = previewWindow.y
        } else {
            previewWindow.x = normalX
            previewWindow.y = normalY
        }
        previewWindow.visible = true
        previewWindow.raise()
    }
}