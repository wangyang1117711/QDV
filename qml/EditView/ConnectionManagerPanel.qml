// =====================================================================
// ConnectionManagerPanel.qml — 连接管理面板（spec: editor-output-connection-optimization Task5）
//
// 功能：
// - 列出全体连线（源节点.fromPort → 目标节点.toPort）
// - 按节点/端口名称关键字筛选（fromId/toId 维度）
// - 删除单条连线（走 editViewBridge.disconnectEdge 可撤销命令）
// =====================================================================

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QDV.EditView 3.0 as Tok

Popup {
    id: root
    modal: false
    width: 500
    height: 560
    padding: 0
    // Popup 规则：不用 anchors，用 x/y 居中
    x: (parent.width - width) / 2
    y: (parent.height - height) / 2
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    // ============ 外部接口 ============
    property var bridge
    signal deleted()   // 删除某条连接后发出（供外部 Toast 提示）

    // ============ 数据 ============
    property var nodeNames: ({})   // {nodeId: node.type}
    property var modelCache: []    // 过滤后的连接行模型

    // 重建节点名映射
    function refreshNames() {
        var names = {}
        var nds = (root.bridge ? root.bridge.currentNodes : [])
        for (var i = 0; i < nds.length; i++) names[nds[i].id] = nds[i].type
        root.nodeNames = names
    }

    // 依据筛选关键字重建行模型
    function buildModel() {
        var conns = (root.bridge ? root.bridge.connections : [])
        var q = searchField.text.trim().toLowerCase()
        var out = []
        for (var i = 0; i < conns.length; i++) {
            var c = conns[i]
            var fromName = root.nodeNames[c.fromId] || c.fromId
            var toName = root.nodeNames[c.toId] || c.toId
            if (q !== "") {
                var hay = (fromName + " " + toName + " " + c.fromPort + " " + c.toPort).toLowerCase()
                if (hay.indexOf(q) < 0) continue
            }
            out.push({fromId: c.fromId, fromPort: c.fromPort,
                      toId: c.toId, toPort: c.toPort,
                      fromName: fromName, toName: toName})
        }
        return out
    }

    function refresh() {
        root.refreshNames()
        root.modelCache = root.buildModel()
    }

    onOpened: root.refresh()
    Connections {
        target: root.bridge
        function onConnectionsChanged() { root.refresh() }
        function onCurrentNodesChanged() { root.refresh() }
    }

    background: Rectangle {
        color: Tok.DesignTokens.bgPanel
        border.color: Tok.DesignTokens.accentPrimary
        border.width: 1
        radius: Tok.DesignTokens.radiusMd
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Tok.DesignTokens.space3
        spacing: 8

        // ============ 标题栏 ============
        RowLayout {
            Layout.fillWidth: true
            Label {
                text: "连接管理"
                color: Tok.DesignTokens.textPrimary
                font.bold: true
                font.pixelSize: Tok.DesignTokens.fontSizeLg
                font.family: Tok.DesignTokens.fontFamilyCJK
            }
            Item { Layout.fillWidth: true }
            Label {
                text: root.modelCache.length + " / " + (root.bridge ? root.bridge.connections.length : 0) + " 条"
                color: Tok.DesignTokens.textSecondary
                font.pixelSize: Tok.DesignTokens.fontSizeSm
            }
        }

        // ============ 筛选输入框 ============
        TextField {
            id: searchField
            Layout.fillWidth: true
            placeholderText: "按节点/端口名称筛选…"
            color: Tok.DesignTokens.textPrimary
            placeholderTextColor: Tok.DesignTokens.textDisabled
            selectByMouse: true
            onTextChanged: root.modelCache = root.buildModel()
        }

        // ============ 连接列表 ============
        ListView {
            id: connList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 4
            model: root.modelCache
            delegate: Rectangle {
                // modelData 由 ListView 模型提供（数组元素的每个连接对象）
                width: connList.width
                height: 34
                radius: Tok.DesignTokens.radiusSm
                color: (index % 2 === 0) ? Tok.DesignTokens.bgCanvas : "#1F1F1F"
                border.color: Tok.DesignTokens.borderDefault
                border.width: 1

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 4
                    spacing: 6

                    Label {
                        Layout.fillWidth: true
                        elide: Text.ElideMiddle
                        text: modelData.fromName + "." + modelData.fromPort
                              + "  →  " + modelData.toName + "." + modelData.toPort
                        color: Tok.DesignTokens.textPrimary
                        font.pixelSize: Tok.DesignTokens.fontSizeSm
                        font.family: Tok.DesignTokens.fontFamilyCJK
                    }

                    ToolButton {
                        text: "✕"
                        implicitWidth: 24; implicitHeight: 24
                        ToolTip.text: "删除此连接（可撤销）"
                        ToolTip.visible: hovered
                        contentItem: Label {
                            text: parent.text
                            color: Tok.DesignTokens.accentError
                            font.pixelSize: Tok.DesignTokens.fontSizeSm
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        background: Rectangle {
                            color: parent.hovered ? Tok.DesignTokens.bgHover : "transparent"
                            radius: Tok.DesignTokens.radiusSm
                        }
                        onClicked: {
                            if (root.bridge) {
                                root.bridge.disconnectEdge(modelData.fromId, modelData.fromPort,
                                                           modelData.toId, modelData.toPort)
                                root.deleted()
                            }
                        }
                    }
                }
            }
        }

        // ============ 空态提示 ============
        Label {
            visible: root.modelCache.length === 0
            Layout.alignment: Qt.AlignHCenter
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            text: "暂无连接"
            color: Tok.DesignTokens.textSecondary
            font.pixelSize: Tok.DesignTokens.fontSizeSm
        }

        // ============ 底部按钮 ============
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            Button {
                text: "关闭"
                onClicked: root.close()
            }
        }
    }
}