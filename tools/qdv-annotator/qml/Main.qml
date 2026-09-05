// =====================================================================
// Main.qml — QDV 数据标注工具 主窗口
//
// 三栏布局（对标 Label Studio / CVAT）：
//   左：图像列表   中：标注画布   右：标签 + 标注项
// 顶栏：任务切换 / 绘制工具 / 上一张下一张 / 导入导出
// 复用项目 QDV.EditView 的 AnnotateOverlay 与 DesignTokens。
// =====================================================================
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QDVAnnotator 1.0 as Tok

ApplicationWindow {
    id: win
    visible: true
    width: 1280; height: 800
    minimumWidth: 900; minimumHeight: 600
    title: "QDV 数据标注工具"
    color: Tok.DesignTokens.bgCanvas

    // ---- 本地 UI 状态 ----
    property string activeTool: "rect"
    // 当前绘制工具对应的形状 key（区分同 overlayType 的多形状，如 freeform/polygon）
    property string activeShapeKey: "rect"
    property int activeLabelId: -1
    // 向量矩形：创建时欲设定的根数（1/2/3/4，按 count 配色）
    property int activeCount: 1
    // 关闭确认流程：true 表示保存成功后自动退出（“保存并退出”）
    property bool saveThenQuit: false

    // 关闭时若有未保存更改，弹出 保存/不保存/取消 确认
    onClosing: function (close) {
        if (!session.dirty) return
        close.accepted = false
        quitDlg.open()
    }

    function applyTaskDefaults() {
        // 分类任务无几何标注：隐藏绘制层，activeTool 置空
        if (session.taskType === "classification") { activeTool = ""; activeShapeKey = ""; return }
        // 其余按注册表任务默认形状 + overlayType 设定当前绘制工具
        var def = capabilities.taskDefaultShape(session.taskType)
        if (def && capabilities.hasShape(def) && capabilities.isShapeEnabled(def)
            && win.isDrawableOverlay(capabilities.overlayTypeOf(def))) {
            activeShapeKey = def
            activeTool = capabilities.overlayTypeOf(def)
        } else {
            // 任务默认形状不可用：退化为第一个可绘制形状，否则置空
            var sk = win.firstDrawableShape(session.taskType)
            activeShapeKey = sk
            activeTool = sk ? capabilities.overlayTypeOf(sk) : ""
        }
        // 画布始终可见（便于查看图像），分类模式仅隐藏绘制层（覆盖层 visible 控制）
    }

    // 某 overlayType 是否有绘制实现（注册表/几何能力对应用户可画形状）
    function isDrawableOverlay(overlayType) {
        return ["rect", "polygon", "vec_rect"].indexOf(overlayType) >= 0
    }
    // 某几何族是否可画
    function isDrawableGeo(geo) {
        return ["box", "polygon", "rotated_box"].indexOf(geo) >= 0
    }
    // 当前任务下第一个可绘制且启用的形状 key（不做任务过滤，保证常用工具常显）
    function firstDrawableShape(task) {
        var list = capabilities.shapes
        for (var i = 0; i < list.length; i++) {
            var s = list[i]
            if (!s.enabled) continue
            if (!win.isDrawableOverlay(s.overlayType)) continue
            return s.key
        }
        return ""
    }
    // 某形状是否可显示为绘制工具：仅要求启用且可绘制（不限制任务，保持所有工具常显）
    function taskSupportsShape(s, task) {
        if (!s.enabled) return false
        return win.isDrawableOverlay(s.overlayType) ? true : false
    }
    Component.onCompleted: { applyTaskDefaults(); importWizard.openFor() }

    // 标签删除/重命名后修正激活标签（Task 7.2）
    Connections {
        target: session
        function onLabelsChanged() {
            if (win.activeLabelId >= session.labels.length)
                win.activeLabelId = session.labels.length - 1
        }
    }

    // 删除在用标签前二次确认（Task 7.5）：统计每类标注数
    function confirmRemoveLabel(id) {
        if (id < 0 || id >= session.labels.length) return
        var name = session.labels[id]
        var stat = session.statistics()
        var count = 0
        if (stat && stat.perClass && stat.perClass[name]) count = stat.perClass[name]
        if (count > 0) {
            delLabelDlg.pendingId = id
            delLabelDlg.labelName = name
            delLabelDlg.count = count
            delLabelDlg.open()
        } else {
            session.removeLabel(id)
        }
    }

    // 状态栏任务类型显示名
    property string taskLabel: {
        var t = session.taskType
        var list = capabilities.tasks
        for (var i = 0; i < list.length; i++) if (list[i].key === t) return list[i].label
        return t
    }

    // ===================== 顶栏 =====================
    header: ToolBar {
        height: Tok.DesignTokens.headerBarHeight + 12
        background: Rectangle { color: Tok.DesignTokens.bgHeader; border.color: Tok.DesignTokens.borderDefault; border.width: 1 }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 8; anchors.rightMargin: 8
            spacing: 8

            Label { text: "QDV 标注"; color: Tok.DesignTokens.accentPrimary; font.bold: true; font.family: Tok.DesignTokens.fontFamilyCJK; font.pixelSize: Tok.DesignTokens.fontSizeLg }

            // 任务类型（S1：由能力注册表 tasks 驱动，改配置文件即可加任务）
            Tok.WideComboBox {
                id: taskCombo
                textRole: "text"
                model: capabilities.tasks.map(function (t) { return { text: t.label, val: t.key } })
                Component.onCompleted: {
                    for (var i = 0; i < model.length; i++) if (model[i].val === session.taskType) currentIndex = i
                }
                onActivated: { session.setTaskType(model[currentIndex].val); applyTaskDefaults() }
            }

            // 绘制工具（几何任务；S1：工具按钮由注册表 enabled 形状动态生成，
            //             同一 overlayType 的多个形状（如 polygon/freeform）复用同一绘制实现）
            RowLayout {
                visible: session.taskType !== "classification"
                spacing: 4
                ToolButton {
                    text: "选择"
                    highlighted: activeTool === ""
                    onClicked: { activeTool = ""; activeShapeKey = "" }
                    ToolTip.text: "点击选中/移动/缩放标注，Delete 删除（快捷键 0）"
                }
                Repeater {
                    model: capabilities.shapes
                    delegate: ToolButton {
                        // 仅显示：启用、可绘制、且当前任务支持 的形状工具
                        visible: win.taskSupportsShape(modelData, session.taskType)
                        text: modelData.label
                        // 高亮当前工具：overlayType 与形状 key 均匹配（精确区分同 overlayType）
                        highlighted: activeTool === modelData.overlayType && activeShapeKey === modelData.key
                        onClicked: { activeTool = modelData.overlayType; activeShapeKey = modelData.key }
                        ToolTip.text: modelData.label + "（" + modelData.key + "）"
                    }
                }
                // 根数选择器（向量矩形：按注册表 overlayType 判定显示）
                Row {
                    spacing: 2
                    visible: activeTool === capabilities.overlayTypeOf("vec_rect")
                    Layout.fillHeight: true
                    Layout.alignment: Qt.AlignVCenter
                    Repeater {
                        model: [1, 2, 3, 4]
                        delegate: ToolButton {
                            text: "×" + modelData
                            highlighted: activeCount === modelData
                            implicitWidth: 34
                            onClicked: activeCount = modelData
                            ToolTip.text: "新建方向矩形丝线根数为 " + modelData
                        }
                    }
                }
            }

            Label { text: "激活标签:"; color: Tok.DesignTokens.textSecondary; font.family: Tok.DesignTokens.fontFamilyCJK }
            Rectangle {
                width: 14; height: 14; radius: 2
                color: (activeLabelId >= 0) ? session.colorForLabel(activeLabelId) : Tok.DesignTokens.textDisabled
            }
            Label {
                id: activeLbl
                text: (activeLabelId >= 0 && activeLabelId < session.labels.length) ? session.labels[activeLabelId] : "未选"
                color: Tok.DesignTokens.textPrimary; font.family: Tok.DesignTokens.fontFamilyCJK
                // 标签名过长时省略并悬停显示完整内容，避免挤压顶栏布局
                Layout.maximumWidth: 160
                elide: Text.ElideRight
                ToolTip.visible: activeLbl.truncated
                ToolTip.text: activeLbl.text
                ToolTip.delay: 400
            }

            Item { Layout.fillWidth: true }

            // 撤销/重做
            ToolButton { text: "撤销"; enabled: session.canUndo; onClicked: session.undo(); ToolTip.text: "撤销 (Ctrl+Z)" }
            ToolButton { text: "重做"; enabled: session.canRedo; onClicked: session.redo(); ToolTip.text: "重做 (Ctrl+Y)" }
            // 帮助入口
            ToolButton { text: "?"; onClicked: helpDlg.open(); ToolTip.text: "快捷键与操作帮助 (Ctrl+/)" }

            // 上一张/下一张
            ToolButton { text: "‹"; onClicked: if (session.currentImageIndex > 0) session.currentImageIndex = session.currentImageIndex - 1 }
            ToolButton { text: "›"; onClicked: if (session.currentImageIndex < session.images.length - 1) session.currentImageIndex = session.currentImageIndex + 1 }

            // 导入/导出/保存
            Button { text: "导入"; onClicked: importWizard.openFor() }
            Button { text: "打开"; onClicked: importWizard.openFor() }
            Button { text: "保存工程"; onClicked: fileSaveDlg.open() }
            Button { text: "导出"; highlighted: true; onClicked: exportDialog.openFor() }
            Button {
                text: "标训闭环"
                ToolTip.text: "标注→导出 dataset.yaml→拉起 yolo_train.py 一键训练"
                onClicked: trainingDialog.openFor()
            }
        }
    }

    // ===================== 主体三栏 =====================
    RowLayout {
        id: bodyRow
        anchors.fill: parent
        spacing: 1

        // 左：图像列表
        ImageListPanel {
            id: imgList
            Layout.preferredWidth: 240
            Layout.fillHeight: true
            currentIndex: session.currentImageIndex
            onImageSelected: session.currentImageIndex = index
        }

        // 中：画布
        AnnotationCanvas {
            id: canvas
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: 700
            image: session.currentImage
            taskType: session.taskType
            activeTool: win.activeTool
            activeShapeKey: win.activeShapeKey
            activeLabelId: win.activeLabelId
            activeCount: win.activeCount
        }

        // 右：标签 + 标注项
        ColumnLayout {
            id: rightCol
            Layout.preferredWidth: 320
            Layout.fillWidth: false
            Layout.fillHeight: true
            spacing: 1
            LabelPanel {
                id: labelPanel
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredHeight: 200   // 固定值，避免 parent.height 依赖触发递归布局
                taskType: session.taskType
                activeLabelId: win.activeLabelId
                onLabelSelected: win.activeLabelId = id
                onRequestAdd: session.addLabel(name)
                onRequestRemove: win.confirmRemoveLabel(id)
                onRequestRename: session.renameLabel(id, name)
            }
            PreprocessPanel {
                id: prepPanel
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredHeight: 150
                activeLabelId: win.activeLabelId
            }
            AnnotationListPanel {
                id: annList
                Layout.fillWidth: true
                Layout.fillHeight: true
                taskType: session.taskType
                onRequestDelete: session.removeAnnotation(index)
            }
        }
    }

    // ===================== 底部状态栏 =====================
    footer: ToolBar {
        height: 28
        background: Rectangle { color: Tok.DesignTokens.bgHeader }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 10; anchors.rightMargin: 10
            spacing: 16
            // 当前图序号/名称
            Label {
                text: (session.currentImageIndex >= 0)
                      ? (session.currentImageIndex + 1) + " / " + session.images.length + "   " + session.currentImage.fileName
                      : "无图像"
                color: Tok.DesignTokens.textTertiary
                font.family: Tok.DesignTokens.fontFamilyCJK
                font.pixelSize: Tok.DesignTokens.fontSizeXs
                elide: Text.ElideMiddle
                Layout.maximumWidth: 340
                // 文件名过长被省略时，悬停显示完整路径
                ToolTip.visible: truncated
                ToolTip.text: text
                ToolTip.delay: 400
            }
            // 当前图标注数（currentImage 为响应式属性，字段变化会触发重算）
            Label {
                text: "标注 " + (session.currentImage.annotations ? session.currentImage.annotations.length : 0)
                color: Tok.DesignTokens.textTertiary
                font.family: Tok.DesignTokens.fontFamilyCJK
                font.pixelSize: Tok.DesignTokens.fontSizeXs
            }
            // 任务类型
            Label {
                text: win.taskLabel
                color: Tok.DesignTokens.accentInfo
                font.family: Tok.DesignTokens.fontFamilyCJK
                font.pixelSize: Tok.DesignTokens.fontSizeXs
            }
            Item { Layout.fillWidth: true }
            // 脏状态（未保存）指示
            Label {
                text: "● 未保存"
                visible: session.dirty
                color: Tok.DesignTokens.dirtyIndicator
                font.family: Tok.DesignTokens.fontFamilyCJK
                font.pixelSize: Tok.DesignTokens.fontSizeXs
            }
            // 错误信息
            Label {
                text: session.lastError ? "错误: " + session.lastError : ""
                visible: session.lastError !== ""
                color: Tok.DesignTokens.accentError
                font.family: Tok.DesignTokens.fontFamilyCJK
                font.pixelSize: Tok.DesignTokens.fontSizeXs
                // 错误信息较长时省略显示，悬停查看完整内容，避免挤压状态栏
                elide: Text.ElideRight
                Layout.maximumWidth: 420
                ToolTip.visible: truncated
                ToolTip.text: text
                ToolTip.delay: 400
            }
        }
    }

    // ===================== 对话框 =====================
    ImportWizard { id: importWizard }
    ExportDialog { id: exportDialog }
    TrainingDialog { id: trainingDialog }
    FileDialog {
        id: fileSaveDlg
        title: "保存工程"
        fileMode: FileDialog.SaveFile
        nameFilters: ["QDV 标注工程 (*.qdvann)"]
        onAccepted: {
            if (session.saveProject(selectedFile.toString().replace("file:///", ""))) {
                // “保存并退出”：保存成功后继续关闭流程
                if (win.saveThenQuit) { win.saveThenQuit = false; win.close() }
            } else {
                console.warn("保存失败:", session.lastError)
            }
        }
    }

    // 关闭确认：存在未保存更改时询问 保存/不保存/取消
    Dialog {
        id: quitDlg
        modal: true
        title: "未保存的更改"
        width: 420
        contentItem: ColumnLayout {
            spacing: 12
            Label {
                text: "当前工程有未保存的更改，退出前要保存吗？"
                color: Tok.DesignTokens.textPrimary
                font.family: Tok.DesignTokens.fontFamilyCJK
                font.pixelSize: Tok.DesignTokens.fontSizeBase
                wrapMode: Text.Wrap
            }
            RowLayout {
                spacing: 8
                Item { Layout.fillWidth: true }
                Button {
                    text: "保存并退出"
                    highlighted: true
                    onClicked: { quitDlg.close(); win.saveThenQuit = true; fileSaveDlg.open() }
                }
                Button {
                    text: "不保存"
                    onClicked: { quitDlg.close(); session.clearDirty(); win.close() }
                }
                Button {
                    text: "取消"
                    onClicked: quitDlg.close()
                }
            }
        }
    }

    // 删除在用标签二次确认（Task 7.5）
    Dialog {
        id: delLabelDlg
        modal: true
        title: "删除标签"
        property int pendingId: -1
        property string labelName: ""
        property int count: 0
        width: 440
        contentItem: ColumnLayout {
            spacing: 12
            Label {
                text: "标签 \"" + delLabelDlg.labelName + "\" 正被 " + delLabelDlg.count
                      + " 个标注使用，删除后这些标注的类别将被移除。确定删除？"
                color: Tok.DesignTokens.textPrimary
                font.family: Tok.DesignTokens.fontFamilyCJK
                font.pixelSize: Tok.DesignTokens.fontSizeBase
                wrapMode: Text.Wrap
            }
            RowLayout {
                spacing: 8
                Item { Layout.fillWidth: true }
                Button {
                    text: "删除"
                    highlighted: true
                    onClicked: { session.removeLabel(delLabelDlg.pendingId); delLabelDlg.close() }
                }
                Button { text: "取消"; onClicked: delLabelDlg.close() }
            }
        }
    }

    // 快捷键帮助面板（Task 7.6）
    Dialog {
        id: helpDlg
        modal: true
        title: "快捷键与画布操作"
        width: 560
        contentItem: ColumnLayout {
            spacing: 8
            // 快捷键表
            GridLayout {
                columns: 2
                columnSpacing: 16
                rowSpacing: 6

                Label { text: "0 / 1 / 2"; font.family: Tok.DesignTokens.fontMono; font.pixelSize: Tok.DesignTokens.fontSizeSm; color: Tok.DesignTokens.accentInfo }
                Label { text: "切换 选择 / 矩形 / 多边形 工具"; font.family: Tok.DesignTokens.fontFamilyCJK; font.pixelSize: Tok.DesignTokens.fontSizeSm; color: Tok.DesignTokens.textSecondary }
                Label { text: "← / →"; font.family: Tok.DesignTokens.fontMono; font.pixelSize: Tok.DesignTokens.fontSizeSm; color: Tok.DesignTokens.accentInfo }
                Label { text: "上一张 / 下一张图像"; font.family: Tok.DesignTokens.fontFamilyCJK; font.pixelSize: Tok.DesignTokens.fontSizeSm; color: Tok.DesignTokens.textSecondary }
                Label { text: "Delete"; font.family: Tok.DesignTokens.fontMono; font.pixelSize: Tok.DesignTokens.fontSizeSm; color: Tok.DesignTokens.accentInfo }
                Label { text: "删除选中的标注"; font.family: Tok.DesignTokens.fontFamilyCJK; font.pixelSize: Tok.DesignTokens.fontSizeSm; color: Tok.DesignTokens.textSecondary }
                Label { text: "Esc"; font.family: Tok.DesignTokens.fontMono; font.pixelSize: Tok.DesignTokens.fontSizeSm; color: Tok.DesignTokens.accentInfo }
                Label { text: "取消选中"; font.family: Tok.DesignTokens.fontFamilyCJK; font.pixelSize: Tok.DesignTokens.fontSizeSm; color: Tok.DesignTokens.textSecondary }
                Label { text: "Ctrl+Z"; font.family: Tok.DesignTokens.fontMono; font.pixelSize: Tok.DesignTokens.fontSizeSm; color: Tok.DesignTokens.accentInfo }
                Label { text: "撤销"; font.family: Tok.DesignTokens.fontFamilyCJK; font.pixelSize: Tok.DesignTokens.fontSizeSm; color: Tok.DesignTokens.textSecondary }
                Label { text: "Ctrl+Y / Ctrl+Shift+Z"; font.family: Tok.DesignTokens.fontMono; font.pixelSize: Tok.DesignTokens.fontSizeSm; color: Tok.DesignTokens.accentInfo }
                Label { text: "重做"; font.family: Tok.DesignTokens.fontFamilyCJK; font.pixelSize: Tok.DesignTokens.fontSizeSm; color: Tok.DesignTokens.textSecondary }
                Label { text: "Ctrl+S / Ctrl+E"; font.family: Tok.DesignTokens.fontMono; font.pixelSize: Tok.DesignTokens.fontSizeSm; color: Tok.DesignTokens.accentInfo }
                Label { text: "保存工程 / 导出数据集"; font.family: Tok.DesignTokens.fontFamilyCJK; font.pixelSize: Tok.DesignTokens.fontSizeSm; color: Tok.DesignTokens.textSecondary }
                Label { text: "Ctrl+/"; font.family: Tok.DesignTokens.fontMono; font.pixelSize: Tok.DesignTokens.fontSizeSm; color: Tok.DesignTokens.accentInfo }
                Label { text: "打开本帮助"; font.family: Tok.DesignTokens.fontFamilyCJK; font.pixelSize: Tok.DesignTokens.fontSizeSm; color: Tok.DesignTokens.textSecondary }
            }
            Rectangle { width: parent.width; height: 1; color: Tok.DesignTokens.borderDefault }
            Label {
                text: "画布操作：左键绘制/选中/移动/缩放标注；中键拖拽平移；滚轮缩放；右下角可适配窗口/100%/放大/缩小"
                color: Tok.DesignTokens.textSecondary
                font.family: Tok.DesignTokens.fontFamilyCJK
                font.pixelSize: Tok.DesignTokens.fontSizeSm
                wrapMode: Text.Wrap
            }
        }
    }

    // ===================== 快捷键 =====================
    Shortcut { sequence: "Left"; onActivated: if (session.currentImageIndex > 0) session.currentImageIndex-- }
    Shortcut { sequence: "Right"; onActivated: if (session.currentImageIndex < session.images.length - 1) session.currentImageIndex++ }
    Shortcut { sequence: "0"; onActivated: if (session.taskType !== "classification") { win.activeTool = ""; win.activeShapeKey = "" } }
    // S1：快捷键按键对应固定形状 key，但 overlayType 经注册表解析（形状改名/换 overlayType 仍有效）
    Shortcut { sequence: "1"; onActivated: if (session.taskType !== "classification" && capabilities.hasShape("rect") && capabilities.isShapeEnabled("rect")) { win.activeShapeKey = "rect"; win.activeTool = capabilities.overlayTypeOf("rect") } }
    Shortcut { sequence: "3"; onActivated: if (session.taskType !== "classification" && capabilities.hasShape("vec_rect") && capabilities.isShapeEnabled("vec_rect")) { win.activeShapeKey = "vec_rect"; win.activeTool = capabilities.overlayTypeOf("vec_rect") } }
    Shortcut { sequence: "2"; onActivated: if (session.taskType === "segmentation" && capabilities.hasShape("polygon") && capabilities.isShapeEnabled("polygon")) { win.activeShapeKey = "polygon"; win.activeTool = capabilities.overlayTypeOf("polygon") } }
    Shortcut { sequence: "Ctrl+S"; onActivated: fileSaveDlg.open() }
    Shortcut { sequence: "Ctrl+E"; onActivated: exportDialog.openFor() }
    Shortcut { sequence: "Ctrl+Z"; onActivated: session.undo() }
    Shortcut { sequence: "Ctrl+Y"; onActivated: session.redo() }
    Shortcut { sequence: "Ctrl+Shift+Z"; onActivated: session.redo() }
    Shortcut { sequence: "Ctrl+/"; onActivated: helpDlg.open() }
}
