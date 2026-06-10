// =====================================================================
// OperatorEditorDialog.qml — 算子模态详编对话框（v3.0.0 方案B Design Tokens 迁移）
//
// 功能：
// - 复用 ParamForm.qml 渲染动态表单
// - 提供 应用/取消/重置 按钮
// - 打开时缓存当前值（取消 = 恢复缓存）
// - 应用：调 bridge.updateOperatorParams(...)
//
// 入口：
//   var dlg = operatorEditorDialogComponent.createObject(root, {
//       bridge: bridge,
//       nodeId: "abc-123"
//   })
//   dlg.open()
// =====================================================================

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import QDV.EditView 3.0 as Tok

Dialog {
    id: dlg
    modal: true
    standardButtons: Dialog.NoButton   // 自定义按钮
    width: 520
    height: 640
    title: "算子参数编辑器"
    padding: 0
    background: Rectangle {
        color: Tok.DesignTokens.bgPanel
        border.color: Tok.DesignTokens.accentPrimary
        border.width: 1
        radius: Tok.DesignTokens.radiusMd
    }

    // ============ 外部接口 ============
    property var bridge
    property string nodeId: ""
    // 当前节点 OperatorMeta + params 缓存
    property var meta: null
    property var workingValues: ({})  // 编辑过程中的当前值
    property var snapshotValues: ({})  // 打开时的快照（取消用）

    // 加载节点数据
    function load() {
        if (!bridge || !nodeId) return
        // 在 currentNodes 中找
        var nodes = bridge.currentNodes || []
        var found = null
        for (var i = 0; i < nodes.length; ++i) {
            if (nodes[i].id === nodeId) {
                found = nodes[i]
                break
            }
        }
        if (!found) {
            console.warn("[OperatorEditorDialog] 节点不存在:", nodeId)
            return
        }
        meta = bridge.getOperatorMeta(found.type)
        workingValues = JSON.parse(JSON.stringify(found.params || ({})))
        snapshotValues = JSON.parse(JSON.stringify(workingValues))
    }

    onNodeIdChanged: load()
    onBridgeChanged: load()
    Component.onCompleted: load()

    // ============ 布局 ============
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 0
        spacing: 0

        // 顶部标题栏
        Rectangle {
            Layout.fillWidth: true
            height: 40
            color: Tok.DesignTokens.bgHeader

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 8

                Rectangle {
                    width: 18; height: 18
                    color: Tok.DesignTokens.accentPrimary
                    radius: Tok.DesignTokens.radiusSm
                }
                Label {
                    text: dlg.meta ? dlg.meta.cnName + " 参数编辑器" : "参数编辑器"
                    color: Tok.DesignTokens.textPrimary
                    font.pixelSize: Tok.DesignTokens.fontSizeLg
                    font.bold: true
                    font.family: Tok.DesignTokens.fontFamilyCJK
                    Layout.fillWidth: true
                }
                Label {
                    text: dlg.nodeId ? "(" + dlg.nodeId.substring(0, 8) + "...)" : ""
                    color: Tok.DesignTokens.textPlaceholder
                    font.pixelSize: Tok.DesignTokens.fontSizeXs
                    font.family: Tok.DesignTokens.fontFamilyCJK
                }
            }
        }

        // 描述（如果有）
        Label {
            visible: dlg.meta && dlg.meta.description
            text: dlg.meta ? dlg.meta.description : ""
            color: Tok.DesignTokens.textTertiary
            font.pixelSize: Tok.DesignTokens.fontSizeSm
            font.family: Tok.DesignTokens.fontFamilyCJK
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            Layout.leftMargin: 12
            Layout.rightMargin: 12
            Layout.topMargin: 8
        }

        // 分类标签
        Label {
            visible: dlg.meta && dlg.meta.category
            text: dlg.meta ? "分类：" + dlg.meta.category : ""
            color: Tok.DesignTokens.accentPrimary
            font.pixelSize: Tok.DesignTokens.fontSizeXs
            font.family: Tok.DesignTokens.fontFamilyCJK
            Layout.leftMargin: 12
        }

        // 分隔
        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: Tok.DesignTokens.borderDefault
            Layout.topMargin: 6
        }

        // ParamForm 主体
        ScrollView {
            id: formScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            ScrollBar.vertical.policy: ScrollBar.AsNeeded

            ParamForm {
                id: form
                width: formScroll.width
                params: dlg.meta ? dlg.meta.params : []
                currentValues: dlg.workingValues
                onValuesChanged: function(newValues) {
                    dlg.workingValues = JSON.parse(JSON.stringify(newValues))
                }
                onValidationError: function(name, message) {
                    console.warn("[OperatorEditorDialog] 校验失败:", name, message)
                }
            }
        }

        // 底部按钮栏
        Rectangle {
            Layout.fillWidth: true
            height: 56
            color: Tok.DesignTokens.bgHeader

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                anchors.topMargin: 8
                anchors.bottomMargin: 8
                spacing: 8

                Button {
                    text: "重置默认"
                    flat: true
                    onClicked: {
                        if (!dlg.meta) return
                        var defaults = {}
                        for (var i = 0; i < dlg.meta.params.length; ++i) {
                            defaults[dlg.meta.params[i].name] = dlg.meta.params[i].defaultValue
                        }
                        dlg.workingValues = defaults
                    }
                }
                Button {
                    text: "撤销修改"
                    flat: true
                    onClicked: dlg.workingValues = JSON.parse(JSON.stringify(dlg.snapshotValues))
                }
                Item { Layout.fillWidth: true }
                Button {
                    text: "取消"
                    flat: true
                    onClicked: dlg.reject()
                }
                Button {
                    text: "应用"
                    highlighted: true
                    onClicked: {
                        if (dlg.bridge && dlg.nodeId) {
                            dlg.bridge.updateOperatorParams(dlg.nodeId, dlg.workingValues)
                        }
                        dlg.accept()
                    }
                }
            }
        }
    }
}
