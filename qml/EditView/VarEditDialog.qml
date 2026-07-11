// =====================================================================
// QDV VarEditDialog.qml（v2.6.0 控制变量新建/编辑对话框）
//
// 功能：
//   1. 新建变量：输入名称/类型/值/描述 → bridge.variableManager.createVariable
//   2. 编辑变量：修改值/描述（名称和类型不可改）→ bridge.variableManager.setValue
// =====================================================================

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QDV.EditView 3.0 as Tok

Popup {
    id: root
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    width: 360
    height: 280
    padding: 0

    property var bridge: null
    property bool isEditMode: false
    property string editingVarName: ""

    background: Rectangle {
        color: Tok.DesignTokens.bgPanel
        border.color: Tok.DesignTokens.borderDefault
        border.width: Tok.DesignTokens.borderWidth
        radius: Tok.DesignTokens.radiusMd
    }

    function openNew() {
        root.isEditMode = false
        root.editingVarName = ""
        nameField.text = ""
        nameField.readOnly = false
        typeCombo.currentIndex = 0
        typeCombo.enabled = true
        valueField.text = "0"
        descField.text = ""
        errorLabel.text = ""
        root.open()
    }

    function openEdit(varName) {
        if (!bridge || !bridge.variableManager) return
        var v = bridge.variableManager.variable(varName)
        if (!v || Object.keys(v).length === 0) return

        root.isEditMode = true
        root.editingVarName = varName
        nameField.text = v.name
        nameField.readOnly = true
        var typeIdx = ["Int", "Double", "String", "Bool"].indexOf(v.type)
        typeCombo.currentIndex = typeIdx >= 0 ? typeIdx : 0
        typeCombo.enabled = false
        valueField.text = String(v.value)
        descField.text = v.description || ""
        errorLabel.text = ""
        root.open()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Tok.DesignTokens.space4
        spacing: Tok.DesignTokens.space2

        // 标题
        Label {
            text: root.isEditMode ? "编辑变量" : "新建变量"
            color: Tok.DesignTokens.accentPrimary
            font.bold: true
            font.pixelSize: Tok.DesignTokens.fontSizeLg
        }

        // 名称
        Label {
            text: "变量名"
            color: Tok.DesignTokens.textSecondary
            font.pixelSize: Tok.DesignTokens.fontSizeSm
        }
        TextField {
            id: nameField
            Layout.fillWidth: true
            Layout.preferredHeight: 28
            placeholderText: "字母/数字/下划线，首字符为字母或下划线"
            color: Tok.DesignTokens.textPrimary
            font.pixelSize: Tok.DesignTokens.fontSizeSm
            background: Rectangle {
                color: Tok.DesignTokens.bgSurface
                border.color: nameField.activeFocus
                    ? Tok.DesignTokens.borderFocus
                    : Tok.DesignTokens.borderDefault
                border.width: 1
                radius: Tok.DesignTokens.radiusSm
            }
        }

        // 类型
        Label {
            text: "类型"
            color: Tok.DesignTokens.textSecondary
            font.pixelSize: Tok.DesignTokens.fontSizeSm
        }
        ComboBox {
            id: typeCombo
            Layout.fillWidth: true
            Layout.preferredHeight: 28
            model: ["Int", "Double", "String", "Bool"]
            font.pixelSize: Tok.DesignTokens.fontSizeSm
            onActivated: {
                // 切换类型时设置默认值
                switch (typeCombo.currentText) {
                    case "Int": valueField.text = "0"; break
                    case "Double": valueField.text = "0.0"; break
                    case "String": valueField.text = ""; break
                    case "Bool": valueField.text = "false"; break
                }
            }
        }

        // 值
        Label {
            text: "值"
            color: Tok.DesignTokens.textSecondary
            font.pixelSize: Tok.DesignTokens.fontSizeSm
        }
        TextField {
            id: valueField
            Layout.fillWidth: true
            Layout.preferredHeight: 28
            color: Tok.DesignTokens.textPrimary
            font.pixelSize: Tok.DesignTokens.fontSizeSm
            background: Rectangle {
                color: Tok.DesignTokens.bgSurface
                border.color: valueField.activeFocus
                    ? Tok.DesignTokens.borderFocus
                    : Tok.DesignTokens.borderDefault
                border.width: 1
                radius: Tok.DesignTokens.radiusSm
            }
        }

        // 描述
        Label {
            text: "描述（可选）"
            color: Tok.DesignTokens.textSecondary
            font.pixelSize: Tok.DesignTokens.fontSizeSm
        }
        TextField {
            id: descField
            Layout.fillWidth: true
            Layout.preferredHeight: 28
            placeholderText: "变量用途说明"
            color: Tok.DesignTokens.textPrimary
            font.pixelSize: Tok.DesignTokens.fontSizeSm
            background: Rectangle {
                color: Tok.DesignTokens.bgSurface
                border.color: descField.activeFocus
                    ? Tok.DesignTokens.borderFocus
                    : Tok.DesignTokens.borderDefault
                border.width: 1
                radius: Tok.DesignTokens.radiusSm
            }
        }

        // 错误提示
        Label {
            id: errorLabel
            color: "#FF6B6B"
            font.pixelSize: Tok.DesignTokens.fontSizeSm
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            visible: text !== ""
        }

        Item { Layout.fillHeight: true }

        // 按钮行
        RowLayout {
            Layout.fillWidth: true
            spacing: Tok.DesignTokens.space2

            Item { Layout.fillWidth: true }

            Button {
                text: "取消"
                Layout.preferredWidth: 70
                Layout.preferredHeight: 28
                font.pixelSize: Tok.DesignTokens.fontSizeSm
                onClicked: root.close()
            }

            Button {
                text: root.isEditMode ? "保存" : "创建"
                Layout.preferredWidth: 70
                Layout.preferredHeight: 28
                font.pixelSize: Tok.DesignTokens.fontSizeSm
                highlighted: true
                onClicked: {
                    if (!bridge || !bridge.variableManager) {
                        errorLabel.text = "变量管理器未初始化"
                        return
                    }

                    var name = nameField.text.trim()
                    var type = typeCombo.currentText
                    var valStr = valueField.text
                    var desc = descField.text

                    // 类型转换
                    var val
                    switch (type) {
                        case "Int":
                            val = parseInt(valStr)
                            if (isNaN(val)) { errorLabel.text = "无效的整数值"; return }
                            break
                        case "Double":
                            val = parseFloat(valStr)
                            if (isNaN(val)) { errorLabel.text = "无效的浮点值"; return }
                            break
                        case "String":
                            val = valStr
                            break
                        case "Bool":
                            val = (valStr === "true" || valStr === "1")
                            break
                    }

                    if (root.isEditMode) {
                        // 编辑模式：仅修改值和描述
                        if (!bridge.variableManager.setValue(root.editingVarName, val)) {
                            errorLabel.text = "修改变量值失败（类型不匹配？）"
                            return
                        }
                        if (desc !== "") {
                            bridge.variableManager.setDescription(root.editingVarName, desc)
                        }
                    } else {
                        // 新建模式
                        if (name === "") {
                            errorLabel.text = "变量名不能为空"
                            return
                        }
                        if (!bridge.variableManager.createVariable(name, type, val, desc)) {
                            errorLabel.text = "创建失败（变量名已存在或非法）"
                            return
                        }
                    }

                    root.close()
                }
            }
        }
    }
}
