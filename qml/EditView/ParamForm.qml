// =====================================================================
// ParamForm.qml — 算子参数动态表单（v3.0.0 方案B Design Tokens 迁移）
//
// 设计：
// - 单文件包含 7 个 inline Component（C++ 端 ParamType 枚举对应 0-6）
// - 外部接口：
//     params:  [{type, name, cnName, defaultValue, minValue, maxValue,
//                step, options, optionKeys, help, unit}, ...]
//     currentValues: ({name: value, ...})   // 节点当前参数
//     onValuesChanged: ({name: value, ...}) // 任意值变化时回传完整 map
// - 复用场景：PropertyPreviewPanel（右侧预览）+ OperatorEditorDialog（模态详编）
//
// 7 个 inline Component:
//   0 Int      -> spinBoxComp      SpinBox
//   1 Float    -> doubleSpinBoxComp DoubleSpinBox
//   2 Enum     -> comboBoxComp     ComboBox
//   3 Bool     -> checkBoxComp     CheckBox
//   4 String   -> textFieldComp    TextField
//   5 ROI      -> roiSelectorComp  ROISelector 子控件
//   6 Vector   -> vectorFieldComp  TextField（逗号/换行分隔）
// =====================================================================

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QDV.EditView 3.0 as Tok

Item {
    id: root
    implicitHeight: column.implicitHeight + 16
    implicitWidth: 300

    /// 算子参数规范（来自 bridge.getOperatorMeta(type).params）
    property var params: []
    /// 当前节点参数值（{name: value, ...}）
    property var currentValues: ({})
    /// 任意值变化时回传（外部用于 updateOperatorParams）
    signal valuesChanged(var newValues)
    /// 校验错误信号（bridge.validateParam 失败时）
    signal validationError(string name, string message)

    // 当前值内部状态（确保未传 currentValues 时不报错）
    property var _internalValues: ({})
    onCurrentValuesChanged: _internalValues = currentValues || ({})

    // 工具函数：根据 paramName 取当前值
    function getValue(name) {
        if (currentValues && currentValues.hasOwnProperty(name))
            return currentValues[name];
        // fallback 到 defaultValue
        for (var i = 0; i < params.length; ++i) {
            if (params[i].name === name) return params[i].defaultValue;
        }
        return null;
    }

    // 工具函数：写入值并 emit
    function setValue(name, val) {
        var v = _internalValues || {};
        v[name] = val;
        _internalValues = v;
        valuesChanged(v);
    }

    // ==================== 7 个内联 Component ====================

    // 0: Int — SpinBox（整数）
    Component {
        id: spinBoxComp
        RowLayout {
            id: intItem
            property var spec
            property string paramName: spec.name
            property var currentValue
            spacing: 6
            Layout.fillWidth: true
            height: 32

            Label {
                text: spec.cnName + (spec.unit ? " (" + spec.unit + ")" : "")
                color: Tok.DesignTokens.textPrimary
                font.pixelSize: 12
                Layout.preferredWidth: 120
            }
            SpinBox {
                id: spinBox
                Layout.fillWidth: true
                Accessible.name: spec.cnName + " 整数参数"
                Accessible.description: spec.help || ""
                from: spec.minValue !== undefined ? spec.minValue : -2147483648
                to:   spec.maxValue !== undefined ? spec.maxValue :  2147483647
                stepSize: spec.step !== undefined ? spec.step : 1
                value: currentValue !== undefined ? currentValue : (spec.defaultValue || 0)
                editable: true
                background: Rectangle {
                    color: spinBox.activeFocus ? Tok.DesignTokens.bgHover : Tok.DesignTokens.bgSurface
                    border.color: spinBox.activeFocus ? Tok.DesignTokens.borderFocus : Tok.DesignTokens.borderDefault
                    border.width: 1
                    radius: Tok.DesignTokens.radiusSm
                }
                contentItem: TextInput {
                    text: spinBox.textFromValue(spinBox.value, spinBox.locale)
                    color: Tok.DesignTokens.textPrimary
                font.pixelSize: 12
                    horizontalAlignment: Qt.AlignHCenter
                    verticalAlignment: Qt.AlignVCenter
                    readOnly: !spinBox.editable
                    validator: spinBox.validator
                    inputMethodHints: Qt.ImhFormattedNumbersOnly
                }
                onValueChanged: {
                    root.setValue(intItem.paramName, value)
                }
            }
        }
    }

    // 1: Float — DoubleSpinBox（用 TextField 模拟，避免依赖 Qt.labs）
    Component {
        id: doubleSpinBoxComp
        RowLayout {
            id: floatItem
            property var spec
            property string paramName: spec.name
            property var currentValue
            spacing: 6
            Layout.fillWidth: true
            height: 32

            Label {
                text: spec.cnName + (spec.unit ? " (" + spec.unit + ")" : "")
                color: Tok.DesignTokens.textPrimary
                font.pixelSize: 12
                Layout.preferredWidth: 120
            }
            TextField {
                id: doubleBox
                Layout.fillWidth: true
                Accessible.name: spec.cnName + " 浮点参数"
                Accessible.description: spec.help || ""
                text: {
                    var v = currentValue !== undefined ? currentValue : spec.defaultValue
                    return v !== undefined && v !== null ? Number(v).toString() : "0"
                }
                color: Tok.DesignTokens.textPrimary
                font.pixelSize: 12
                horizontalAlignment: Qt.AlignRight
                selectByMouse: true
                background: Rectangle {
                    color: doubleBox.activeFocus ? Tok.DesignTokens.bgHover : Tok.DesignTokens.bgSurface
                    border.color: doubleBox.activeFocus ? Tok.DesignTokens.borderFocus : Tok.DesignTokens.borderDefault
                    border.width: 1
                    radius: Tok.DesignTokens.radiusSm
                }
                validator: DoubleValidator {
                    bottom: spec.minValue !== undefined ? spec.minValue : -1e308
                    top:    spec.maxValue !== undefined ? spec.maxValue :  1e308
                    decimals: 4
                }
                onEditingFinished: {
                    var v = parseFloat(text)
                    if (!isNaN(v)) {
                        root.setValue(floatItem.paramName, v)
                    }
                }
            }
        }
    }

    // 2: Enum — ComboBox（下拉枚举）
    Component {
        id: comboBoxComp
        RowLayout {
            id: enumItem
            property var spec
            property string paramName: spec.name
            property var currentValue
            spacing: 6
            Layout.fillWidth: true
            height: 32

            Label {
                text: spec.cnName
                color: Tok.DesignTokens.textPrimary
                font.pixelSize: 12
                Layout.preferredWidth: 120
            }
            ComboBox {
                id: combo
                Layout.fillWidth: true
                Accessible.name: spec.cnName + " 枚举参数"
                Accessible.description: spec.help || ""
                model: spec.options || []
                // 默认值匹配：尝试 defaultValue 对应 optionKeys 索引
                currentIndex: {
                    var dv = currentValue !== undefined ? currentValue : spec.defaultValue
                    if (spec.optionKeys && spec.optionKeys.length > 0) {
                        var idx = spec.optionKeys.indexOf(String(dv))
                        if (idx >= 0) return idx
                    }
                    if (typeof dv === "number") {
                        if (dv >= 0 && dv < (spec.options ? spec.options.length : 0))
                            return dv
                    }
                    return 0
                }
                background: Rectangle {
                    color: combo.activeFocus ? Tok.DesignTokens.bgHover : Tok.DesignTokens.bgSurface
                    border.color: Tok.DesignTokens.borderDefault
                    border.width: 1
                    radius: Tok.DesignTokens.radiusSm
                }
                contentItem: Label {
                    text: combo.displayText
                    color: Tok.DesignTokens.textPrimary
                font.pixelSize: 12
                    leftPadding: 8
                    verticalAlignment: Text.AlignVCenter
                }
                onActivated: {
                    // 优先用 optionKeys，否则用 index
                    var val
                    if (spec.optionKeys && spec.optionKeys.length > 0) {
                        val = spec.optionKeys[currentIndex]
                    } else {
                        val = currentIndex
                    }
                    root.setValue(enumItem.paramName, val)
                }
            }
        }
    }

    // 3: Bool — CheckBox
    Component {
        id: checkBoxComp
        RowLayout {
            id: boolItem
            property var spec
            property string paramName: spec.name
            property var currentValue
            spacing: 6
            Layout.fillWidth: true
            height: 32

            CheckBox {
                id: checkBox
                Layout.preferredWidth: 120
                Accessible.name: spec.cnName + " 开关"
                text: spec.cnName
                checked: currentValue !== undefined ? currentValue : (spec.defaultValue || false)
                contentItem: Label {
                    text: checkBox.text
                    color: Tok.DesignTokens.textPrimary
                    font.pixelSize: Tok.DesignTokens.fontSizeBase
                    leftPadding: checkBox.indicator.width + 6
                    verticalAlignment: Text.AlignVCenter
                }
                indicator: Rectangle {
                    implicitWidth: 18
                    implicitHeight: 18
                    x: checkBox.leftPadding
                    y: parent.height / 2 - height / 2
                    radius: Tok.DesignTokens.radiusSm
                    border.color: checkBox.checked ? Tok.DesignTokens.borderFocus : Tok.DesignTokens.borderDefault
                    color: checkBox.checked ? Tok.DesignTokens.borderFocus : Tok.DesignTokens.bgSurface
                }
                onToggled: root.setValue(boolItem.paramName, checked)
            }
            Item { Layout.fillWidth: true }   // 占位
        }
    }

    // 4a: FilePath — TextField + 浏览按钮（v3.2.0 算子参数编辑器增强）
    //     适用于参数名 == "filePath" 的 String 字段
    //     paramLoader 在 onLoaded 中注入 spec 和 currentValue；
    //     valuePicked 信号直接回传 setValue（不走 paramLoader）。
    Component {
        id: filePathFieldComp
        FilePathField {
            onValuePicked: function(newVal) {
                root.setValue(modelData ? (modelData.name || "") : "", newVal)
            }
        }
    }

    // 4: String — TextField
    Component {
        id: textFieldComp
        RowLayout {
            id: stringItem
            property var spec
            property string paramName: spec.name
            property var currentValue
            spacing: 6
            Layout.fillWidth: true
            height: 32

            Label {
                text: spec.cnName
                color: Tok.DesignTokens.textPrimary
                font.pixelSize: 12
                Layout.preferredWidth: 120
            }
            TextField {
                id: textField
                Layout.fillWidth: true
                Accessible.name: spec.cnName + " 文本参数"
                Accessible.description: spec.help || ""
                text: currentValue !== undefined && currentValue !== null
                      ? String(currentValue) : (spec.defaultValue || "")
                color: Tok.DesignTokens.textPrimary
                font.pixelSize: 12
                selectByMouse: true
                background: Rectangle {
                    color: textField.activeFocus ? Tok.DesignTokens.bgHover : Tok.DesignTokens.bgSurface
                    border.color: textField.activeFocus ? Tok.DesignTokens.borderFocus : Tok.DesignTokens.borderDefault
                    border.width: 1
                    radius: Tok.DesignTokens.radiusSm
                }
                onEditingFinished: root.setValue(stringItem.paramName, text)
            }
        }
    }

    // 5: ROI — ROISelector 子控件（qml/EditView/ROISelector.qml）
    Component {
        id: roiSelectorComp
        Loader {
            id: roiLoader
            source: "qrc:/qml/EditView/ROISelector.qml"
            // 加载后注入 spec + currentValue
            onLoaded: {
                if (item) {
                    item.spec = modelData
                    item.currentValue = currentValue !== undefined && currentValue !== null
                                       ? String(currentValue) : ""
                    item.valueEdited.connect(function(newVal) {
                        root.setValue(item.paramName, newVal)
                    })
                }
            }
        }
    }

    // 6: Vector — TextField（逗号/换行分隔字符串）
    Component {
        id: vectorFieldComp
        RowLayout {
            id: vectorItem
            property var spec
            property string paramName: spec.name
            property var currentValue
            spacing: 6
            Layout.fillWidth: true
            height: 32

            Label {
                text: spec.cnName
                color: Tok.DesignTokens.textPrimary
                font.pixelSize: 12
                Layout.preferredWidth: 120
            }
            TextField {
                id: vecField
                Layout.fillWidth: true
                Accessible.name: spec.cnName + " 向量参数"
                Accessible.description: spec.help || "逗号或换行分隔的值"
                placeholderText: "逗号或换行分隔"
                text: currentValue !== undefined && currentValue !== null
                      ? String(currentValue) : (spec.defaultValue || "")
                color: Tok.DesignTokens.textPrimary
                font.pixelSize: 12
                selectByMouse: true
                background: Rectangle {
                    color: vecField.activeFocus ? Tok.DesignTokens.bgHover : Tok.DesignTokens.bgSurface
                    border.color: vecField.activeFocus ? Tok.DesignTokens.borderFocus : Tok.DesignTokens.borderDefault
                    border.width: 1
                    radius: Tok.DesignTokens.radiusSm
                }
                onEditingFinished: root.setValue(vectorItem.paramName, text)
            }
        }
    }

    // ==================== 主 Repeater + Loader ====================

    ColumnLayout {
        id: column
        anchors.fill: parent
        anchors.margins: 8
        spacing: 6

        // 顶部帮助提示（如果有 hover，可扩展）
        Label {
            visible: false  // M5 阶段接 HoverHelp
            text: ""
            color: Tok.DesignTokens.textPlaceholder
            font.pixelSize: 10
        }

        Repeater {
            id: repeater
            model: root.params
            delegate: Loader {
                id: paramLoader
                Layout.fillWidth: true
                // 关键：根据 spec.type 选择 Component
                sourceComponent: {
                    if (!modelData) return null
                    // v3.2.0：filePath 字段走专用文件路径组件（带浏览按钮 + 路径记忆）
                    if (modelData.name === "filePath") return filePathFieldComp
                    switch (modelData.type) {
                        case 0: return spinBoxComp
                        case 1: return doubleSpinBoxComp
                        case 2: return comboBoxComp
                        case 3: return checkBoxComp
                        case 4: return textFieldComp
                        case 5: return roiSelectorComp
                        case 6: return vectorFieldComp
                        default: return textFieldComp
                    }
                }
                // 加载后把 spec + currentValue 注入
                onLoaded: {
                    if (item) {
                        item.spec = modelData
                        item.currentValue = root.getValue(modelData.name)
                    }
                }
            }
        }

        // 空状态
        Label {
            visible: root.params.length === 0
            text: "该算子无需配置参数"
            color: Tok.DesignTokens.textPlaceholder
            font.pixelSize: 11
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
        }
    }
}
