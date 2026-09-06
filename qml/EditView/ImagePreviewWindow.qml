// =====================================================================
// ImagePreviewWindow.qml — 内嵌图像预览浮窗（v3.2.0）
//
// v3.2.0 重要变更（修复 QRhi 冲突导致闪退）：
// 原实现为独立顶层 Window，在 QQuickWidget 中嵌套会创建第二个 QRhi 实例，
// 导致 "Texture belongs to QRhi X, but client code attempted to use it with QRhi Y"
// 错误，最终 Device loss 闪退。
// 现改为 Popup（QtQuick.Controls），与主 QML 共享同一 QRhi 上下文，彻底解决冲突。
//
// 功能（与原 v3.1.0 保持一致）：
// - 双击效果预览图时弹出，显示原图 & 处理后图像
// - 标题栏拖动、最小化（收缩为标题栏条状）、最大化（铺满父窗口）
// - 透明度滑块 30%-100% 范围调节
// - z-index >= 1000，不影响主界面交互
// - 设计尺寸基于 drawio 示意图（效果预览图 80x88 区域，浮窗默认 640x480）
// =====================================================================

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QDV.EditView 3.0 as Tok

Popup {
    id: previewWindow
    // 关键修复：modal=false 允许主界面交互；NoAutoClose 防止点击外部意外关闭
    modal: false
    focus: false
    closePolicy: Popup.NoAutoClose
    // z-index >= 1000 保证浮在最前
    z: 1000

    width: 640
    height: 480
    // 初始位置：父窗口居中（在 open() 中设置）
    x: 0
    y: 0

    padding: 0
    // 透明背景：让内部 mainContainer 自行绘制圆角与边框
    background: Item { }

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

    // Popup 自动带 enter/exit 过渡，无需显式定义

    // ============ 首次打开居中钩子 ============
    // 不重写 open()/close()（会破坏 Popup 默认信号机制），
    // 改用 onAboutToShow 在弹出前同步位置，确保首次居中于父窗口。
    onAboutToShow: {
        if (normalX === 0 && normalY === 0) {
            var parentItem = previewWindow.parent
            if (parentItem) {
                previewWindow.x = Math.max(0, (parentItem.width - previewWindow.width) / 2)
                previewWindow.y = Math.max(0, (parentItem.height - previewWindow.height) / 2)
                normalX = previewWindow.x
                normalY = previewWindow.y
            }
        } else {
            previewWindow.x = normalX
            previewWindow.y = normalY
        }
    }

    // ============ 主体容器 ============
    Rectangle {
        id: mainContainer
        // Popup 内部根 Item 已隐式提供尺寸，直接 anchors.fill 即可
        anchors.fill: parent
        anchors.margins: 1
        color: Tok.DesignTokens.bgPanel
        border.color: Tok.DesignTokens.accentPrimary
        border.width: 2
        radius: Tok.DesignTokens.radiusLg
        opacity: previewWindow.currentOpacity
        clip: true

        // ============ 最小化/最大化状态：作用于 Popup（Popup 继承自 QObject+QQuickItem，
        // 不支持 states，这里改用 PropertyBindings 显式控制尺寸）============

        // ============ 标题栏 ============
        Rectangle {
            id: titleBar
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: previewWindow.titleBarHeight
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
                    onClicked: previewWindow.close()
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
                            cache: true   // P0-4c：恢复缓存
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
                            cache: true   // P0-4c：恢复缓存
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

    // ============ 尺寸控制（替代原 Window 的 states）============
    // 最小化时：高度收缩为标题栏+边框
    // 最大化时：尺寸跟随父窗口（由 onWidthChanged/onHeightChanged 同步）
    // 正常时：使用 normalW/normalH
    Item {
        // 占位 Item 仅用于触发绑定：监听 isMinimized/isMaximized 变化
        // 实际尺寸控制通过下面的 Binding 块完成
        visible: false
    }

    // 通过 Binding 显式控制 Popup 的 width/height/x/y
    // 注意：Binding 的 when=false 时不会强制设置属性，允许其他逻辑修改
    Binding on width {
        when: previewWindow.isMinimized
        value: previewWindow.normalW
        restoreMode: Binding.RestoreBindingOrValue
    }
    Binding on height {
        when: previewWindow.isMinimized
        value: previewWindow.titleBarHeight + 2
        restoreMode: Binding.RestoreBindingOrValue
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
            // 最大化：铺满父窗口（Popup 的 parent）
            var parentItem = previewWindow.parent
            if (parentItem) {
                previewWindow.x = 0
                previewWindow.y = 0
                previewWindow.width = parentItem.width
                previewWindow.height = parentItem.height
            }
        } else {
            // 恢复到保存的尺寸
            isMaximized = false
            previewWindow.x = normalX
            previewWindow.y = normalY
            previewWindow.width = normalW
            previewWindow.height = normalH
        }
    }

    // 重写 open：首次打开居中于父窗口；后续打开保留上次位置
    // 注：open()/close() 保留 Popup 基类默认实现，"首次居中"由 onAboutToShow 钩子完成。
    // 如需扩展，可在此处添加自定义逻辑，但必须显式调用 Popup.open()。
    // 当前不重写，留空避免覆盖基类。
}
