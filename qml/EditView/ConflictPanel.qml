// =====================================================================
// ConflictPanel.qml — 输入/输出项冲突指定面板（spec: editor-output-connection-optimization Task7）
//
// 功能：
// - 通过 editViewBridge.conflictDetector.detectAll() 拉取当前方案全部冲突
// - 按 kind 分组展示（dupAlias 黄 / typeMismatch 红 / multiInputAmbiguity 橙）
// - 每条冲突可从 candidates 中勾选指定"主方案"
// - 点击"应用"后：
//     · dupAlias            → 勾选端口 enabled=true、同组其他候选端口 enabled=false（走 updateOutputConfig 可撤销写回）
//     · multiInputAmbiguity → 保留勾选的上游绑定，删除其余候选绑定（走 disconnectEdge 可撤销）
//     · typeMismatch        → 删除该条不兼容连接（走 disconnectEdge 可撤销）
// - 空态显示"未检测到冲突"
// =====================================================================

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QDV.EditView 3.0 as Tok

Popup {
    id: root
    modal: false
    width: 580
    height: 640
    padding: 0
    // Popup 规则：不用 anchors，用 x/y 居中
    x: (parent.width - width) / 2
    y: (parent.height - height) / 2
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    // ============ 外部接口 ============
    property var bridge            // editViewBridge
    signal applied(int count)      // 应用成功后发出（供 Main 弹出 Toast）

    // ============ kind 元信息 ============
    readonly property var kindMeta: ({
        "dupAlias":             { title: "同名冲突",       color: "#FFD740" },
        "typeMismatch":         { title: "类型不兼容",     color: "#FF5252" },
        "multiInputAmbiguity":  { title: "多输入绑定歧义", color: "#FF9100" }
    })

    // ============ 数据 ============
    property var groups: []      // 按 kind 分组后的展示模型 [{kind,title,color,items:[...]}]
    property var picked: ({})    // {conflictIndex: chosenCandidateText}，指定主方案

    // 依据 detectAll() 重建分组模型
    function refresh() {
        var list = (root.bridge && root.bridge.conflictDetector)
                   ? root.bridge.conflictDetector.detectAll() : []
        var order = ["dupAlias", "typeMismatch", "multiInputAmbiguity"]
        var g = []
        for (var k = 0; k < order.length; k++) {
            var kind = order[k]
            var items = []
            for (var i = 0; i < list.length; i++) {
                if (list[i].kind === kind) {
                    var item = list[i]
                    item.__idx = i   // 全局冲突索引，作为 picked 的 key
                    items.push(item)
                }
            }
            if (items.length > 0) {
                g.push({ kind: kind,
                         title: root.kindMeta[kind].title,
                         color: root.kindMeta[kind].color,
                         items: items })
            }
        }
        root.groups = g
        root.picked = {}
    }

    // 解析 "id[port]" 格式的候选文本 → {id, port}
    function parseDot(s) {
        var m = /^(.+)\[([^\]]+)\]$/.exec(s)
        if (m) return { id: m[1], port: m[2] }
        return { id: s, port: "" }
    }

    // 应用：按已勾选的主方案消除冲突
    function apply() {
        if (!root.bridge) return
        var n = 0
        for (var gi = 0; gi < root.groups.length; gi++) {
            var group = root.groups[gi]
            for (var ci = 0; ci < group.items.length; ci++) {
                var c = group.items[ci]
                var chosen = root.picked[String(c.__idx)]
                if (chosen === undefined || chosen === null || chosen === "") continue

                if (c.kind === "dupAlias") {
                    // 同名冲突：勾选端口启用，同组其他候选端口禁用
                    var oc = root.bridge.getOutputConfig(c.nodeId)
                    if (oc) {
                        for (var di = 0; di < c.candidates.length; di++) {
                            var cand = c.candidates[di]
                            if (oc[cand] !== undefined) {
                                oc[cand].enabled = (cand === chosen)
                            }
                        }
                        root.bridge.updateOutputConfig(c.nodeId, oc)
                        n++
                    }
                } else if (c.kind === "multiInputAmbiguity") {
                    // 保留勾选的上游绑定，删除其余候选绑定
                    for (var bi = 0; bi < c.candidates.length; bi++) {
                        var b = c.candidates[bi]
                        if (b === chosen) continue
                        var p = root.parseDot(b)
                        root.bridge.disconnectEdge(p.id, p.port, c.nodeId, c.portName)
                        n++
                    }
                } else if (c.kind === "typeMismatch") {
                    // 删除该条不兼容连接
                    var pm = root.parseDot(chosen)
                    root.bridge.disconnectEdge(pm.id, pm.port, c.nodeId, c.portName)
                    n++
                }
            }
        }
        if (n > 0) {
            root.refresh()
            root.applied(n)
        }
    }

    onOpened: root.refresh()
    Connections {
        target: root.bridge
        function onConflictsChanged() { if (root.opened) root.refresh() }
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
        spacing: Tok.DesignTokens.space2

        // ============ 标题栏 ============
        RowLayout {
            Layout.fillWidth: true
            Label {
                text: "输入/输出冲突"
                color: Tok.DesignTokens.textPrimary
                font.bold: true
                font.pixelSize: Tok.DesignTokens.fontSizeLg
                font.family: Tok.DesignTokens.fontFamilyCJK
            }
            Item { Layout.fillWidth: true }
            Label {
                text: {
                    var total = 0
                    for (var i = 0; i < root.groups.length; i++) total += root.groups[i].items.length
                    return total + " 处冲突"
                }
                color: Tok.DesignTokens.textSecondary
                font.pixelSize: Tok.DesignTokens.fontSizeSm
            }
            ToolButton {
                text: "✕"
                implicitWidth: 26; implicitHeight: 26
                ToolTip.text: "关闭"
                ToolTip.visible: hovered
                contentItem: Label {
                    text: parent.text
                    color: Tok.DesignTokens.textSecondary
                    font.pixelSize: Tok.DesignTokens.fontSizeBase
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    color: parent.hovered ? Tok.DesignTokens.bgHover : "transparent"
                    radius: Tok.DesignTokens.radiusSm
                }
                onClicked: root.close()
            }
        }

        // ============ 分组列表 ============
        ScrollView {
            id: scroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            ScrollBar.vertical.policy: ScrollBar.AsNeeded

            ColumnLayout {
                id: col
                width: scroll.availableWidth
                spacing: Tok.DesignTokens.space2

                Repeater {
                    id: groupRepeater
                    model: root.groups
                    // 每组
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Tok.DesignTokens.space1

                        // 组标题
                        RowLayout {
                            Layout.fillWidth: true
                            Rectangle { width: 10; height: 10; radius: 2; color: modelData.color }
                            Label {
                                text: modelData.title
                                color: modelData.color
                                font.bold: true
                                font.pixelSize: Tok.DesignTokens.fontSizeBase
                                font.family: Tok.DesignTokens.fontFamilyCJK
                            }
                            Label {
                                text: "(" + modelData.items.length + ")"
                                color: Tok.DesignTokens.textSecondary
                                font.pixelSize: Tok.DesignTokens.fontSizeXs
                            }
                        }

                        // 组内各条冲突
                        Repeater {
                            id: conflictRepeater
                            model: modelData.items
                            delegate: Rectangle {
                                id: conflictCard
                                property var conflictItem: modelData
                                Layout.fillWidth: true
                                height: conflictCardCol.implicitHeight + Tok.DesignTokens.space3
                                radius: Tok.DesignTokens.radiusSm
                                color: "#1E1E1E"
                                border.color: root.kindMeta[conflictItem.kind].color
                                border.width: 1

                                ColumnLayout {
                                    id: conflictCardCol
                                    anchors.fill: parent
                                    anchors.margins: Tok.DesignTokens.space2
                                    spacing: Tok.DesignTokens.space1

                                    // 描述
                                    Label {
                                        Layout.fillWidth: true
                                        text: conflictItem.detail
                                        color: Tok.DesignTokens.textPrimary
                                        font.pixelSize: Tok.DesignTokens.fontSizeSm
                                        font.family: Tok.DesignTokens.fontFamilyCJK
                                        wrapMode: Text.WordWrap
                                    }
                                    // 节点/端口
                                    Label {
                                        Layout.fillWidth: true
                                        text: "节点 " + conflictItem.nodeId + " · 端口 " + conflictItem.portName
                                        color: Tok.DesignTokens.textTertiary
                                        font.pixelSize: Tok.DesignTokens.fontSizeXs
                                        font.family: Tok.DesignTokens.fontMono
                                        elide: Text.ElideMiddle
                                    }
                                    // 候选（勾选指定主方案）
                                    Label {
                                        text: "指定主方案（candidates）："
                                        color: Tok.DesignTokens.textSecondary
                                        font.pixelSize: Tok.DesignTokens.fontSizeXs
                                        font.family: Tok.DesignTokens.fontFamilyCJK
                                    }
                                    Repeater {
                                        id: candRepeater
                                        model: conflictItem.candidates
                                        delegate: CheckBox {
                                            id: candCb
                                            text: modelData
                                            Layout.fillWidth: true
                                            // 不绑定 checked，用 onToggled 手动维护单选
                                            contentItem: Label {
                                                text: candCb.text
                                                color: Tok.DesignTokens.textPrimary
                                                font.pixelSize: Tok.DesignTokens.fontSizeSm
                                                font.family: Tok.DesignTokens.fontMono
                                                leftPadding: candCb.indicator.width + 6
                                                verticalAlignment: Text.AlignVCenter
                                                elide: Text.ElideMiddle
                                            }
                                            indicator: Rectangle {
                                                implicitWidth: 16
                                                implicitHeight: 16
                                                x: candCb.leftPadding
                                                y: parent.height / 2 - height / 2
                                                radius: Tok.DesignTokens.radiusSm
                                                border.color: candCb.checked ? root.kindMeta[conflictItem.kind].color : Tok.DesignTokens.borderDefault
                                                color: candCb.checked ? root.kindMeta[conflictItem.kind].color : Tok.DesignTokens.bgSurface
                                            }
                                            onToggled: {
                                                var key = String(conflictItem.__idx)
                                                if (checked) {
                                                    // 单选：取消同组其他候选
                                                    for (var j = 0; j < conflictItem.candidates.length; j++) {
                                                        if (j !== index) {
                                                            var o = candRepeater.itemAt(j)
                                                            if (o) o.checked = false
                                                        }
                                                    }
                                                    root.picked[key] = modelData
                                                } else {
                                                    delete root.picked[key]
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // ============ 空态提示 ============
        Label {
            visible: root.groups.length === 0
            Layout.alignment: Qt.AlignHCenter
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            text: "未检测到冲突"
            color: Tok.DesignTokens.textSecondary
            font.pixelSize: Tok.DesignTokens.fontSizeBase
            font.family: Tok.DesignTokens.fontFamilyCJK
        }

        // ============ 底部按钮 ============
        RowLayout {
            Layout.fillWidth: true
            Label {
                Layout.fillWidth: true
                text: "勾选后点击\"应用\"即可指定主方案并消除冲突"
                color: Tok.DesignTokens.textTertiary
                font.pixelSize: Tok.DesignTokens.fontSizeXs
                font.family: Tok.DesignTokens.fontFamilyCJK
            }
            Item { Layout.fillWidth: true }
            Button {
                text: "应用"
                enabled: root.groups.length > 0
                onClicked: root.apply()
            }
            Button {
                text: "关闭"
                onClicked: root.close()
            }
        }
    }
}