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
    implicitHeight: column.implicitHeight + 8   // v5.0：16→8，紧凑化
    implicitWidth: 280                           // v5.0：300→280，匹配窄面板

    Accessible.role: Accessible.Group
    Accessible.name: "参数编辑面板"
    Accessible.description: "编辑所选算子的运行参数"

    /// 算子参数规范（来自 bridge.getOperatorMeta(type).params）
    property var params: []
    /// 当前节点参数值（{name: value, ...}）
    property var currentValues: ({})
    /// P1-B4-H1 联动：算子类型（由外部传入，用于 isParamVisible 判断）
    /// 调用方需绑定：PropertyPreviewPanel 传 selectedNode.type，OperatorEditorDialog 传 meta.type
    property string operatorType: ""
    /// 任意值变化时回传（外部用于 updateOperatorParams）
    signal valuesChanged(var newValues)
    /// 校验错误信号（bridge.validateParam 失败时）
    signal validationError(string name, string message)

    // 当前值内部状态（确保未传 currentValues 时不报错）
    property var _internalValues: ({})
    // v5.3.7 彻底修复闪回：快照机制 — 只在算子类型变化时从 currentValues 覆盖 _internalValues
    // 参数更新时完全依赖 _internalValues，不响应 currentValues 变化（打破循环覆盖）
    // 用哨兵值 "\u0000" 确保首次加载（operatorType="" 时）也触发快照
    property string _snapshotOperatorType: "\u0000"

    // 快照函数：仅在 operatorType 变化时从 currentValues 覆盖 _internalValues
    // 同时处理 onCurrentValuesChanged 和 onOperatorTypeChanged，避免初始化竞态
    function _maybeSnapshot() {
        if (!currentValues || Object.keys(currentValues).length === 0) return
        if (root.operatorType !== _snapshotOperatorType) {
            _snapshotOperatorType = root.operatorType
            _internalValues = JSON.parse(JSON.stringify(currentValues))
            _linkageTrigger++
        }
    }

    onCurrentValuesChanged: _maybeSnapshot()
    onOperatorTypeChanged: _maybeSnapshot()

    // 供外部调用（重置默认/撤销修改），直接覆盖 _internalValues 并刷新所有控件
    function resetValues(newValues) {
        _internalValues = JSON.parse(JSON.stringify(newValues))
        _linkageTrigger++
    }

    // P1-B4-H1 联动：触发器，每次 setValue 时自增，强制 visible 绑定重算
    // 因为 _internalValues 是 var 类型，内部 key 值修改不触发属性变化信号，
    // 需要一个显式的 int 属性来驱动 isParamVisible 重新求值
    property int _linkageTrigger: 0

    // 工具函数：根据 paramName 取当前值
    // 优先读 _internalValues（用户输入的乐观值），其次 currentValues（外部权威值），最后 fallback 到 defaultValue
    function getValue(name) {
        if (_internalValues && _internalValues.hasOwnProperty(name))
            return _internalValues[name];
        if (currentValues && currentValues.hasOwnProperty(name))
            return currentValues[name];
        // fallback 到 defaultValue
        for (var i = 0; i < params.length; ++i) {
            if (params[i].name === name) return params[i].defaultValue;
        }
        return null;
    }

    // P1-B4-H1 联动：判断参数是否应该显示
    // 基于 operatorType + 其他参数当前值 做条件显示
    // 新增带联动的算子时，在此处添加规则（无需修改 C++ 元数据）
    function isParamVisible(paramName) {
        var t = root.operatorType
        if (t === "ImageArithmetic") {
            // useScalar=true 显示 scalar，false 隐藏（语义为与上一张缓存图运算）
            if (paramName === "scalar") {
                return root.getValue("useScalar") === true
            }
        } else if (t === "ImagePreprocess") {
            // morphology=none 时隐藏 kernelSize（kernelSize 仅对形态学操作有意义）
            if (paramName === "kernelSize") {
                var m = root.getValue("morphology")
                return m !== undefined && m !== null && m !== "none" && m !== ""
            }
        } else if (t === "LineCircleDetect") {
            // detectType 切换：line/lineP/circle 显示不同参数子集
            var dt = root.getValue("detectType")
            if (paramName === "minLineLength" || paramName === "maxLineGap") {
                return dt === "lineP"
            }
            if (paramName === "minRadius" || paramName === "maxRadius" ||
                paramName === "dp" || paramName === "minDist" ||
                paramName === "param1" || paramName === "param2") {
                return dt === "circle"
            }
        } else if (t === "ImageMerge") {
            // alpha 仅在 alphaBlend 模式下有意义
            if (paramName === "alpha") {
                return root.getValue("mergeType") === "alphaBlend"
            }
        } else if (t === "ImageTransform") {
            // transformType 切换：resize/rotate/flip/affine/perspective 显示不同参数
            var tt = root.getValue("transformType")
            if (paramName === "targetWidth" || paramName === "targetHeight") return tt === "resize"
            if (paramName === "angle") return tt === "rotate"
            if (paramName === "flipCode") return tt === "flip"
            if (paramName === "scaleX" || paramName === "scaleY") {
                return tt === "affine" || tt === "perspective"
            }
        }
        // 默认：无联动规则的算子，所有参数都显示
        return true
    }

    // 工具函数：写入值并 emit
    // P1-A3 修复：setValue 时做本地范围校验，失败 emit validationError
    function setValue(name, val) {
        // 本地范围校验（仅 Int/Float 类型，与 C++ validateParam 保持一致）
        for (var i = 0; i < params.length; ++i) {
            if (params[i].name === name) {
                var spec = params[i]
                // Int=0, Float=1 类型做范围校验
                if (spec.type === 0 || spec.type === 1) {
                    var num = Number(val)
                    if (isNaN(num)) {
                        root.validationError(name, spec.cnName + " 必须是数字")
                        return
                    }
                    if (spec.minValue !== undefined && num < spec.minValue) {
                        root.validationError(name, spec.cnName + " 不能小于 " + spec.minValue)
                        return
                    }
                    if (spec.maxValue !== undefined && num > spec.maxValue) {
                        root.validationError(name, spec.cnName + " 不能大于 " + spec.maxValue)
                        return
                    }
                }
                // Enum=2 类型校验 optionKeys
                if (spec.type === 2 && spec.optionKeys && spec.optionKeys.length > 0) {
                    var strVal = String(val)
                    if (spec.optionKeys.indexOf(strVal) < 0) {
                        // 允许 int 索引（P0-4 已在 C++ 端兼容）
                        var intVal = parseInt(strVal)
                        if (isNaN(intVal) || intVal < 0 || intVal >= spec.optionKeys.length) {
                            root.validationError(name, spec.cnName + " 值 " + strVal + " 不在允许选项中")
                            return
                        }
                    }
                }
                break
            }
        }
        var v = _internalValues || {};
        v[name] = val;
        _internalValues = v;
        valuesChanged(v);
        // P1-B4-H1 联动：触发 visible 绑定重算（因为 var 属性内部变化不触发信号）
        _linkageTrigger++
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
            height: 28

            Label {
                id: intLabel
                text: spec.cnName + (spec.unit ? " (" + spec.unit + ")" : "")
                color: Tok.DesignTokens.textPrimary
                font.pixelSize: 12
                Layout.preferredWidth: 100

                SequentialAnimation on color {
                    id: intLabelAnim
                    running: false
                    ColorAnimation { to: "#69F0AE"; duration: 150 }
                    ColorAnimation { to: "#FFFFFF"; duration: 350 }
                }
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
                    intLabelAnim.running = true
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
            height: 28

            Label {
                id: floatLabel
                text: spec.cnName + (spec.unit ? " (" + spec.unit + ")" : "")
                color: Tok.DesignTokens.textPrimary
                font.pixelSize: 12
                Layout.preferredWidth: 100

                SequentialAnimation on color {
                    id: floatLabelAnim
                    running: false
                    ColorAnimation { to: "#69F0AE"; duration: 150 }
                    ColorAnimation { to: "#FFFFFF"; duration: 350 }
                }
            }
            TextField {
                id: doubleBox
                Layout.fillWidth: true
                Accessible.name: spec.cnName + " 浮点参数"
                Accessible.description: spec.help || ""
                // P1-B4-H3 修复：text 仅在 currentValue 变化时同步，避免用户输入过程中被覆盖
                // v4.0.1 修复（编辑界面空白根因）：删除 onCurrentValueChanged 信号处理器。
                // 原因：currentValue 定义在父级 floatItem（RowLayout）上，TextField 内部
                //       无法跨作用域引用其属性变化信号，QML 会抛出
                //       "Cannot assign to non-existent property onCurrentValueChanged"
                //       导致 ParamForm 整体加载失败 → PropertyPreviewPanel 不可用 → Main.qml 空白。
                // 兜底方案：_displayText 本身是绑定表达式，会自动响应 currentValue 变化重算，
                //          无需额外的信号处理器。
                text: doubleBox._displayText
                property string _displayText: {
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
                    // P1-B4-H3 修复：解析失败/空值/越界时回退到 currentValue 或 defaultValue
                    // 之前 isNaN 时静默忽略，导致字段显示与实际值不一致
                    var trimmed = text.trim()
                    if (trimmed === "") {
                        // 空值回退到默认值
                        var dv = spec.defaultValue !== undefined ? spec.defaultValue : 0
                        _displayText = Number(dv).toString()
                        root.setValue(floatItem.paramName, dv)
                        floatLabelAnim.running = true
                        return
                    }
                    var v = parseFloat(trimmed)
                    if (isNaN(v)) {
                        // 解析失败：回退到 currentValue 或 defaultValue，并触发校验错误信号
                        var fallback = currentValue !== undefined ? currentValue : spec.defaultValue
                        _displayText = (fallback !== undefined && fallback !== null) ? Number(fallback).toString() : "0"
                        root.validationError(floatItem.paramName, spec.cnName + " 不是有效的数字")
                        return
                    }
                    // 范围校验
                    if (spec.minValue !== undefined && v < spec.minValue) {
                        var mn = Number(spec.minValue)
                        _displayText = mn.toString()
                        root.setValue(floatItem.paramName, mn)
                        root.validationError(floatItem.paramName, spec.cnName + " 不能小于 " + spec.minValue + "，已回退到最小值")
                        floatLabelAnim.running = true
                        return
                    }
                    if (spec.maxValue !== undefined && v > spec.maxValue) {
                        var mx = Number(spec.maxValue)
                        _displayText = mx.toString()
                        root.setValue(floatItem.paramName, mx)
                        root.validationError(floatItem.paramName, spec.cnName + " 不能大于 " + spec.maxValue + "，已回退到最大值")
                        floatLabelAnim.running = true
                        return
                    }
                    // 正常路径：同步 _displayText 并提交
                    _displayText = v.toString()
                    root.setValue(floatItem.paramName, v)
                    floatLabelAnim.running = true
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
            height: 28

            // v5.3.9 修复：Loader 在 onLoaded 中注入 spec，而 ComboBox 的
            // Component.onCompleted 早于 spec 注入，导致初始 currentIndex 为 0。
            // 在 spec 变化后主动同步一次，确保重开对话框时显示真实保存值。
            // v5.3.9-2 强化：用 Qt.callLater 延迟同步，避免 model 尚未填充时同步失败。
            onSpecChanged: {
                Qt.callLater(function() {
                    if (combo) combo.syncIndexFromValue()
                })
            }

            Label {
                id: enumLabel
                text: spec.cnName
                color: Tok.DesignTokens.textPrimary
                font.pixelSize: 12
                Layout.preferredWidth: 100

                SequentialAnimation on color {
                    id: enumLabelAnim
                    running: false
                    ColorAnimation { to: "#69F0AE"; duration: 150 }
                    ColorAnimation { to: "#FFFFFF"; duration: 350 }
                }
            }
            ComboBox {
                id: combo
                Layout.fillWidth: true
                Accessible.name: spec.cnName + " 枚举参数"
                Accessible.description: spec.help || ""
                model: spec.options || []
                // v5.3.7：去掉 currentIndex 绑定表达式，改命令式更新
                // 关键修复：syncIndexFromValue 读 root.getValue()（最新内部值）
                // 而非 enumItem.currentValue（外部传入的过时值），避免闪回
                property bool _userEditing: false

                function syncIndexFromValue() {
                    // 读 root.getValue()：优先 _internalValues（用户输入/快照），其次 currentValues，最后 defaultValue
                    var dv = root.getValue(enumItem.paramName)
                    if (dv === null || dv === undefined) dv = spec.defaultValue
                    var targetIdx = 0
                    if (spec.optionKeys && spec.optionKeys.length > 0) {
                        var idx = spec.optionKeys.indexOf(String(dv))
                        if (idx >= 0) targetIdx = idx
                    } else if (typeof dv === "number") {
                        if (dv >= 0 && dv < (spec.options ? spec.options.length : 0))
                            targetIdx = dv
                    }
                    if (combo.currentIndex !== targetIdx) {
                        combo.currentIndex = targetIdx
                    }
                }

                Component.onCompleted: syncIndexFromValue()
                // v5.3.7：响应 _linkageTrigger 变化（快照/重置/setValue 后刷新）
                // 用户编辑时（_userEditing=true）不刷新，避免覆盖用户选择
                Connections {
                    target: root
                    // v5.3.7：_linkageTrigger 是 property int，信号名为 _linkageTriggerChanged
                    // Connections 中需用 on_linkageTriggerChanged（下划线前缀）
                    function on_linkageTriggerChanged() {
                        if (!combo._userEditing) syncIndexFromValue()
                    }
                    function onOperatorTypeChanged() {
                        if (!combo._userEditing) syncIndexFromValue()
                    }
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
                onActivated: function(index) {
                    // v5.3.7：使用 onActivated 的 index 参数，标记用户编辑中
                    combo._userEditing = true
                    var val
                    if (spec.optionKeys && spec.optionKeys.length > 0) {
                        val = spec.optionKeys[index]
                    } else {
                        val = index
                    }
                    root.setValue(enumItem.paramName, val)
                    enumLabelAnim.running = true
                    // 立即同步显示（不等 linkageTrigger），然后延迟清除标志
                    syncIndexFromValue()
                    Qt.callLater(function() { combo._userEditing = false })
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
            height: 28

            CheckBox {
                id: checkBox
                Layout.preferredWidth: 100
                Accessible.name: spec.cnName + " 开关"
                Accessible.description: spec.help || ""
                Accessible.role: Accessible.CheckBox
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
    //     valuePicked 信号直接回传 setValue（使用 FilePathField.paramName，从 spec 派生）。
    Component {
        id: filePathFieldComp
        FilePathField {
            onValuePicked: function(newVal) {
                // paramName 是 FilePathField 自身属性（从 spec.name 派生），在此作用域可访问
                root.setValue(paramName, newVal)
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
            height: 28

            Label {
                id: stringLabel
                text: spec.cnName
                color: Tok.DesignTokens.textPrimary
                font.pixelSize: 12
                Layout.preferredWidth: 100

                SequentialAnimation on color {
                    id: stringLabelAnim
                    running: false
                    ColorAnimation { to: "#69F0AE"; duration: 150 }
                    ColorAnimation { to: "#FFFFFF"; duration: 350 }
                }
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
                onEditingFinished: {
                    root.setValue(stringItem.paramName, text)
                    stringLabelAnim.running = true
                }
            }
        }
    }

    // 5: ROI — ROISelector 子控件（qml/EditView/ROISelector.qml）
    Component {
        id: roiSelectorComp
        Loader {
            id: roiLoader
            // 显式声明 currentValue 属性，使外部 Binding 能驱动并传播到内部 ROISelector
            property var currentValue
            source: "qrc:/qml/EditView/ROISelector.qml"
            // currentValue 变化时同步到已加载的 ROISelector
            onCurrentValueChanged: {
                if (item) {
                    item.currentValue = currentValue !== undefined && currentValue !== null
                                       ? String(currentValue) : ""
                }
            }
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
            height: 28

            Label {
                id: vectorLabel
                text: spec.cnName
                color: Tok.DesignTokens.textPrimary
                font.pixelSize: 12
                Layout.preferredWidth: 100

                SequentialAnimation on color {
                    id: vectorLabelAnim
                    running: false
                    ColorAnimation { to: "#69F0AE"; duration: 150 }
                    ColorAnimation { to: "#FFFFFF"; duration: 350 }
                }
            }
            TextField {
                id: vecField
                Layout.fillWidth: true
                Accessible.name: spec.cnName + " 向量参数"
                Accessible.description: spec.help || "逗号或换行分隔的值"
                placeholderText: "逗号或换行分隔"
                // P1-B4-H2 修复：兼容数组和字符串格式显示
                text: {
                    if (currentValue === undefined || currentValue === null) {
                        return spec.defaultValue || ""
                    }
                    // 数组 → 逗号分隔字符串
                    if (Array.isArray(currentValue)) {
                        return currentValue.join(", ")
                    }
                    return String(currentValue)
                }
                color: Tok.DesignTokens.textPrimary
                font.pixelSize: 12
                selectByMouse: true
                background: Rectangle {
                    color: vecField.activeFocus ? Tok.DesignTokens.bgHover : Tok.DesignTokens.bgSurface
                    border.color: vecField.activeFocus ? Tok.DesignTokens.borderFocus : Tok.DesignTokens.borderDefault
                    border.width: 1
                    radius: Tok.DesignTokens.radiusSm
                }
                onEditingFinished: {
                    // P1-B4-H2 修复：将字符串拆分为数组后传给 C++
                    // 之前直接传字符串，BranchNode::deserialize 期望 QJsonArray，收到字符串返回空数组
                    // 导致 trueBranch/falseBranch 永远为空，分支节点 invalid，方案数据损坏
                    var arr = text.split(/[,,\n]/).map(function(s) { return s.trim() })
                                 .filter(function(s) { return s !== "" })
                    root.setValue(vectorItem.paramName, arr)
                    vectorLabelAnim.running = true
                }
            }
        }
    }

    // ==================== 主 Repeater + Loader ====================

    ColumnLayout {
        id: column
        anchors.fill: parent
        anchors.margins: 6
        spacing: 4

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
                // P1-B4-H1 联动：根据 operatorType + 其他参数值条件显示
                // 读取 _linkageTrigger 强制 visible 在 setValue 后重算
                visible: {
                    root._linkageTrigger
                    return root.isParamVisible(modelData ? modelData.name : "")
                }
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
                // v5.3.7：onLoaded 时设置 currentValue，后续不随 currentValues 变化
                // v5.3.9-2 强化：ComboBox 额外显式同步一次，防止 onSpecChanged 时序问题
                onLoaded: {
                    if (item) {
                        item.spec = modelData
                        item.currentValue = root.getValue(modelData.name)
                        if (item.combo && item.combo.syncIndexFromValue) {
                            Qt.callLater(function() { item.combo.syncIndexFromValue() })
                        }
                    }
                }
                // v5.3.7：响应 operatorType 变化（节点切换）和 _linkageTrigger 变化（快照/重置）
                // 刷新 item.currentValue，让所有控件（不仅 ComboBox）同步
                Connections {
                    target: root
                    function onOperatorTypeChanged() {
                        if (paramLoader.item) {
                            paramLoader.item.currentValue = root.getValue(modelData.name)
                        }
                    }
                    function on_linkageTriggerChanged() {
                        if (paramLoader.item) {
                            paramLoader.item.currentValue = root.getValue(modelData.name)
                        }
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
