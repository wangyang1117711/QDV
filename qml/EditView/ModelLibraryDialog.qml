// =====================================================================
// ModelLibraryDialog.qml — 模型库管理对话框（v2.6.0 Task 18）
//
// 功能：
//   1. 左侧：模型列表（ListView）+ 搜索框 + 导入/删除/验证按钮
//   2. 右侧：详情面板（选中模型时展示 file_name/sha256/size/version 等字段）
//   3. 底部：关闭按钮 + 操作状态提示
//
// 数据来源：
//   - bridge.getAvailableModels()  → QVariantList（来自 ModelManager::manifestModels）
//   - bridge.importModel(onnxPath) → 调用 ModelManager::addCustomModel
//   - bridge.deleteModel(modelId)  → 调用 ModelManager::removeCustomModelTransactional
//   - bridge.verifyModel(modelId)  → 调用 ModelManager::verifyModelIntegrity
//
// 入口：
//   var dlg = modelLibraryDialogComponent.createObject(root, { bridge: bridge })
//   dlg.open()
//
// 设计要点：
//   - 使用 Popup 而非 Window（遵循项目 v5.3 惯例：避免 Overlay 独立 QRhi 上下文）
//   - 全部色值/字号/间距走 DesignTokens 单例，无硬编码
// =====================================================================

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QDV.EditView 3.0 as Tok

Popup {
    id: dlg
    modal: true
    width: 820
    height: 600
    padding: 0
    // Popup 不支持 anchors，用 x/y 手动居中
    x: (parent.width - width) / 2
    y: (parent.height - height) / 2

    background: Rectangle {
        color: Tok.DesignTokens.bgPanel
        border.color: Tok.DesignTokens.accentPrimary
        border.width: 1
        radius: Tok.DesignTokens.radiusMd
    }

    // ============ 外部接口 ============
    property var bridge: null

    // 内部状态
    property var fullModelList: []       // 完整模型列表（getAvailableModels 返回）
    property var filteredList: []        // 过滤后列表（搜索框联动）
    property string currentModelId: ""   // 当前选中模型 ID（display_name 或 file_name 的 baseName）

    // ============ 打开时刷新 ============
    onOpened: refreshModels()
    Component.onCompleted: refreshModels()

    function refreshModels() {
        if (!bridge || !bridge.getAvailableModels) {
            fullModelList = []
            filteredList = []
            modelListView.model = filteredList
            return
        }
        var list = bridge.getAvailableModels() || []
        fullModelList = list
        applyFilter(searchField.text)
    }

    // 按搜索关键字过滤（匹配 file_name / display_name / description）
    function applyFilter(keyword) {
        if (!keyword || keyword.length === 0) {
            filteredList = fullModelList
        } else {
            var kw = keyword.toLowerCase()
            filteredList = fullModelList.filter(function(m) {
                var fn = (m.file_name || "").toLowerCase()
                var dn = (m.display_name || "").toLowerCase()
                var desc = (m.description || "").toLowerCase()
                return fn.indexOf(kw) >= 0 || dn.indexOf(kw) >= 0 || desc.indexOf(kw) >= 0
            })
        }
        modelListView.model = filteredList
    }

    // 从 manifest 条目提取 modelId（display_name 优先，否则 file_name 去后缀）
    function modelIdOf(entry) {
        if (!entry) return ""
        var dn = entry.display_name || ""
        if (dn.length > 0) return dn
        var fn = entry.file_name || ""
        if (fn.toLowerCase().endsWith(".onnx")) {
            return fn.substring(0, fn.length - 5)
        }
        return fn
    }

    // 格式化文件大小（字节 → KB/MB）
    function formatSize(bytes) {
        if (!bytes || bytes <= 0) return "—"
        if (bytes < 1024) return bytes + " B"
        if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(2) + " KB"
        return (bytes / 1024 / 1024).toFixed(2) + " MB"
    }

    // 显示操作状态（绿色成功 / 红色错误 / 蓝色信息）
    function showStatus(msg, level) {
        statusLabel.text = msg
        if (level === "success") {
            statusLabel.color = Tok.DesignTokens.accentSuccess
        } else if (level === "error") {
            statusLabel.color = Tok.DesignTokens.accentError
        } else {
            statusLabel.color = Tok.DesignTokens.accentInfo
        }
        statusTimer.restart()
    }

    // ============ 顶部标题栏 ============
    Rectangle {
        id: headerBar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: Tok.DesignTokens.headerBarHeight
        color: Tok.DesignTokens.bgHeader

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Tok.DesignTokens.space4
            anchors.rightMargin: Tok.DesignTokens.space4
            spacing: Tok.DesignTokens.space3

            Label {
                text: "模型库管理"
                color: Tok.DesignTokens.textPrimary
                font.pixelSize: Tok.DesignTokens.fontSizeLg
                font.bold: true
                font.family: Tok.DesignTokens.fontFamily
            }

            Item { Layout.fillWidth: true }

            Label {
                id: statusLabel
                text: ""
                color: Tok.DesignTokens.accentInfo
                font.pixelSize: Tok.DesignTokens.fontSizeSm
                font.family: Tok.DesignTokens.fontFamily
                visible: text.length > 0
                Layout.maximumWidth: 400
                elide: Text.ElideRight
            }

            // 状态文字 3 秒后自动清空
            Timer {
                id: statusTimer
                interval: 3000
                repeat: false
                onTriggered: statusLabel.text = ""
            }

            Button {
                text: "✕"
                Layout.preferredWidth: 32
                Layout.preferredHeight: 24
                flat: true
                ToolTip.text: "关闭"
                ToolTip.visible: hovered
                ToolTip.delay: 500
                onClicked: dlg.close()
            }
        }
    }

    // ============ 主布局：左列表 + 右详情 ============
    RowLayout {
        anchors.top: headerBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: Tok.DesignTokens.space4
        spacing: Tok.DesignTokens.space4

        // ---------- 左侧：模型列表区 ----------
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: 420
            spacing: Tok.DesignTokens.space2

            Label {
                text: "模型列表（" + filteredList.length + " / " + fullModelList.length + "）"
                color: Tok.DesignTokens.textSecondary
                font.pixelSize: Tok.DesignTokens.fontSizeSm
                font.bold: true
                font.family: Tok.DesignTokens.fontFamily
            }

            // 搜索框
            TextField {
                id: searchField
                Layout.fillWidth: true
                placeholderText: "搜索模型（文件名 / 描述）..."
                color: Tok.DesignTokens.textPrimary
                placeholderTextColor: Tok.DesignTokens.textPlaceholder
                font.pixelSize: Tok.DesignTokens.fontSizeSm
                font.family: Tok.DesignTokens.fontFamily
                background: Rectangle {
                    color: Tok.DesignTokens.bgSurface
                    border.color: searchField.activeFocus ? Tok.DesignTokens.borderFocus
                                                          : Tok.DesignTokens.borderDefault
                    border.width: 1
                    radius: Tok.DesignTokens.radiusSm
                }
                onTextChanged: applyFilter(text)
            }

            // 模型列表
            ListView {
                id: modelListView
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: filteredList
                currentIndex: -1

                // 空列表提示
                Text {
                    anchors.centerIn: parent
                    visible: modelListView.count === 0
                    text: "暂无模型，点击「导入模型」添加"
                    color: Tok.DesignTokens.textTertiary
                    font.pixelSize: Tok.DesignTokens.fontSizeSm
                    font.family: Tok.DesignTokens.fontFamily
                }

                delegate: Rectangle {
                    width: ListView.view.width
                    height: 64
                    color: ListView.isCurrentItem ? Tok.DesignTokens.bgHover
                                                  : (index % 2 === 0 ? "transparent"
                                                                     : Qt.rgba(1, 1, 1, 0.02))
                    border.width: ListView.isCurrentItem ? 1 : 0
                    border.color: Tok.DesignTokens.accentPrimary

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: Tok.DesignTokens.space2
                        spacing: 1

                        Text {
                            text: modelData.display_name || modelData.file_name || "（未命名）"
                            color: Tok.DesignTokens.textPrimary
                            font.pixelSize: Tok.DesignTokens.fontSizeSm
                            font.bold: true
                            font.family: Tok.DesignTokens.fontFamily
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                        }

                        Text {
                            text: modelData.description || "无描述"
                            color: Tok.DesignTokens.textTertiary
                            font.pixelSize: Tok.DesignTokens.fontSizeXs
                            font.family: Tok.DesignTokens.fontFamily
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                        }

                        Text {
                            text: formatSize(modelData.expected_size)
                            color: Tok.DesignTokens.textTertiary
                            font.pixelSize: Tok.DesignTokens.fontSizeXs
                            font.family: Tok.DesignTokens.fontMono
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            modelListView.currentIndex = index
                            dlg.currentModelId = dlg.modelIdOf(modelData)
                            detailPanel.updateDetail(modelData)
                        }
                    }
                }
            }

            // 操作按钮行
            RowLayout {
                Layout.fillWidth: true
                spacing: Tok.DesignTokens.space2

                Button {
                    text: "导入模型"
                    Layout.fillWidth: true
                    enabled: bridge !== null
                    onClicked: {
                        if (bridge && bridge.importModel) {
                            fileDialog.open()
                        }
                    }
                }

                Button {
                    text: "删除模型"
                    Layout.fillWidth: true
                    enabled: modelListView.currentIndex >= 0 && bridge && bridge.canDeleteModel()
                    onClicked: {
                        if (dlg.currentModelId.length === 0) {
                            dlg.showStatus("请先选中一个模型", "error")
                            return
                        }
                        // 内置模型（无 registered_at）禁止删除
                        var entry = filteredList[modelListView.currentIndex]
                        if (!entry || !entry.registered_at) {
                            dlg.showStatus("内置模型不可删除", "error")
                            return
                        }
                        // 权限检查（二次防御）
                        if (!bridge || !bridge.canDeleteModel()) {
                            dlg.showStatus("当前用户没有删除权限", "error")
                            return
                        }
                        deleteConfirmDialog.targetModelId = dlg.currentModelId
                        deleteConfirmDialog.targetModelName = entry.display_name || entry.file_name || dlg.currentModelId
                        deleteConfirmDialog.open()
                    }
                }

                Button {
                    text: "验证完整性"
                    Layout.fillWidth: true
                    enabled: modelListView.currentIndex >= 0
                    onClicked: {
                        if (dlg.currentModelId.length === 0) {
                            dlg.showStatus("请先选中一个模型", "error")
                            return
                        }
                        var ok = bridge.verifyModel(dlg.currentModelId)
                        if (ok) {
                            dlg.showStatus("完整性校验通过: " + dlg.currentModelId, "success")
                        } else {
                            dlg.showStatus("完整性校验失败: " + dlg.currentModelId, "error")
                        }
                    }
                }
            }
        }

        // ---------- 右侧：详情面板 ----------
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: 340
            spacing: Tok.DesignTokens.space2

            Label {
                text: "模型详情"
                color: Tok.DesignTokens.textSecondary
                font.pixelSize: Tok.DesignTokens.fontSizeSm
                font.bold: true
                font.family: Tok.DesignTokens.fontFamily
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: Tok.DesignTokens.bgSurface
                border.color: Tok.DesignTokens.borderDefault
                border.width: 1
                radius: Tok.DesignTokens.radiusSm

                ColumnLayout {
                    id: detailPanel
                    anchors.fill: parent
                    anchors.margins: Tok.DesignTokens.space3
                    spacing: Tok.DesignTokens.space2

                    property var currentModel: null

                    // 更新详情面板内容
                    function updateDetail(model) {
                        currentModel = model
                        var hasEntry = model !== null && model !== undefined
                        fnameField.valueText  = hasEntry ? (model.file_name || "") : ""
                        descField.valueText   = hasEntry ? (model.description || "") : ""
                        sizeField.valueText   = hasEntry ? dlg.formatSize(model.expected_size) : ""
                        shaField.valueText    = hasEntry ? (model.sha256 || "") : ""
                        versionField.valueText= hasEntry ? (model.version || "") : ""
                        labelsField.valueText = hasEntry ? (model.labels_file || "") : ""
                        registeredField.valueText = hasEntry ? (model.registered_at || "内置") : ""
                    }

                    // 清空详情
                    function clear() {
                        currentModel = null
                        fnameField.valueText = ""
                        descField.valueText = ""
                        sizeField.valueText = ""
                        shaField.valueText = ""
                        versionField.valueText = ""
                        labelsField.valueText = ""
                        registeredField.valueText = ""
                    }

                    // 详情字段列表（字段过多时由 Flickable 提供滚动）
                    Flickable {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        contentHeight: fieldsColumn.implicitHeight
                        contentWidth: width
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                        ColumnLayout {
                            id: fieldsColumn
                            width: parent.width
                            spacing: Tok.DesignTokens.space2

                            DetailField { id: fnameField;      labelText: "文件名";    Layout.fillWidth: true }
                            DetailField { id: descField;       labelText: "描述";      Layout.fillWidth: true }
                            DetailField { id: sizeField;       labelText: "大小";      Layout.fillWidth: true }
                            DetailField { id: shaField;        labelText: "SHA256";    Layout.fillWidth: true; wrapValue: true }
                            DetailField { id: versionField;    labelText: "版本";      Layout.fillWidth: true }
                            DetailField { id: labelsField;     labelText: "标签文件";  Layout.fillWidth: true }
                            DetailField { id: registeredField; labelText: "注册时间";  Layout.fillWidth: true }
                        }
                    }
                }
            }

            Button {
                text: "关闭"
                Layout.fillWidth: true
                onClicked: dlg.close()
            }
        }
    }

    // ============ 文件对话框（导入 ONNX 模型）============
    FileDialog {
        id: fileDialog
        title: "选择 ONNX 模型文件"
        fileMode: FileDialog.OpenFile
        nameFilters: ["ONNX 模型 (*.onnx)", "所有文件 (*)"]
        onAccepted: {
            if (!bridge || !bridge.importModel) {
                dlg.showStatus("bridge 不可用", "error")
                return
            }
            // 将 file:/// URL 转为本地路径
            var path = String(currentFile)
            if (path.startsWith("file:///")) {
                path = path.substring(8)
            } else if (path.startsWith("file://")) {
                path = path.substring(7)
            }
            // Windows 路径开头可能是 /C:/...，去掉前导斜杠
            if (path.length > 2 && path.charAt(0) === "/" && path.charAt(2) === ":") {
                path = path.substring(1)
            }
            var ok = bridge.importModel(path)
            if (ok) {
                dlg.showStatus("模型导入成功: " + path, "success")
                dlg.refreshModels()
            } else {
                dlg.showStatus("模型导入失败: " + path, "error")
            }
        }
    }

    // ============ 删除确认对话框 ============
    Dialog {
        id: deleteConfirmDialog
        modal: true
        title: "确认删除模型"
        // 在父 Popup 中居中显示
        x: (parent.width - width) / 2
        y: (parent.height - height) / 2
        width: 420

        // 外部传入
        property string targetModelId: ""
        property string targetModelName: ""
        // 是否同时清理关联数据（训练记录、评估报告、labels 等）
        property bool deleteRelatedData: true

        ColumnLayout {
            anchors.fill: parent
            spacing: Tok.DesignTokens.space3

            Label {
                text: "即将删除模型：" + deleteConfirmDialog.targetModelName
                color: Tok.DesignTokens.textPrimary
                font.pixelSize: Tok.DesignTokens.fontSizeSm
                font.family: Tok.DesignTokens.fontFamily
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }

            Label {
                text: "此操作不可逆。模型文件将被移动到 .trash 回收区，并从模型库中移除。"
                color: Tok.DesignTokens.accentError
                font.pixelSize: Tok.DesignTokens.fontSizeSm
                font.family: Tok.DesignTokens.fontFamily
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }

            CheckBox {
                id: relatedDataCheckBox
                checked: deleteConfirmDialog.deleteRelatedData
                text: "同时删除关联数据（训练记录、评估报告、标签文件等）"
                onCheckedChanged: deleteConfirmDialog.deleteRelatedData = checked
            }
        }

        standardButtons: Dialog.Ok | Dialog.Cancel

        onAccepted: {
            var modelId = deleteConfirmDialog.targetModelId
            if (modelId.length === 0) return

            // 删除过程中禁用删除按钮并显示状态
            dlg.showStatus("正在删除：" + modelId + "...", "info")

            var ok = bridge.deleteModel(modelId, deleteConfirmDialog.deleteRelatedData)
            if (ok) {
                dlg.showStatus("模型已删除: " + modelId, "success")
                detailPanel.clear()
                dlg.currentModelId = ""
                dlg.refreshModels()
            } else {
                dlg.showStatus("删除失败: " + modelId, "error")
            }
        }

        onRejected: {
            deleteConfirmDialog.targetModelId = ""
            deleteConfirmDialog.targetModelName = ""
        }
    }
}
