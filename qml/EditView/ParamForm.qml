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
    /// v5.4.0：模型库列表（由外部传入，用于 modelPath 参数的下拉选择）
    /// 调用方需绑定：通过 bridge.getRegisteredModels() 获取并赋值
    property var modelList: []
    /// v5.4.2：EditViewBridge（由外部传入，用于零样本检测算子 modelPath 下拉
    /// 与 modelType 联动过滤，见 getZeroShotModelsByType）
    property var bridge: null
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

    // 快照函数：从 currentValues 覆盖 _internalValues
    // 触发条件：
    //   1. operatorType 变化（切换算子类型）
    //   2. currentValues 变化且与 _internalValues 不一致（外部传入新的参数值，
    //      例如重新打开参数编辑器时从节点读取到的最新值）。
    // 条件 2 不会与 setValue 形成循环：setValue 更新 _internalValues 后发出
    // valuesChanged，父组件回传相同的 currentValues，此时 JSON 相同，不会重复快照。
    function _maybeSnapshot() {
        if (!currentValues || Object.keys(currentValues).length === 0) return
        if (root.operatorType !== _snapshotOperatorType) {
            _snapshotOperatorType = root.operatorType
            _internalValues = JSON.parse(JSON.stringify(currentValues))
            _linkageTrigger++
            return
        }
        // v5.4.1 修复模型路径等参数在重新打开编辑器后恢复默认值的问题：
        // 当 currentValues 与 _internalValues 不一致时，说明外部传入了新的权威值，
        // 需要同步到 _internalValues，确保下拉框等控件能正确显示保存值。
        var currJson = JSON.stringify(currentValues)
        var intJson = JSON.stringify(_internalValues)
        if (currJson !== intJson) {
            _internalValues = JSON.parse(currJson)
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

    // ==================== v2.8.0 参数帮助 tooltip 组件 ====================
    // 在每个参数右侧显示"?"图标（16x16，灰色圆形，白色问号）
    // hover 显示 ToolTip，内容来自 editViewBridge.getParamHelp(type, paramName)
    // 多行显示（Text.WordWrap），最长 200 字符自动截断换行
    // 调用方式：Loader 加载本 Component，并通过同名 property 传入 helpType/helpParam
    Component {
        id: paramHelpIconComp
        Item {
            id: helpIcon
            width: 16
            height: 16
            // 通过 Loader 上下文注入（parent 即 Loader，可直接读其 property）
            property string helpType: parent ? (parent.helpType || "") : ""
            property string helpParam: parent ? (parent.helpParam || "") : ""
            property string helpText: ""

            function updateHelpText() {
                try {
                    if (typeof editViewBridge !== "undefined" && editViewBridge
                            && helpType.length > 0 && helpParam.length > 0) {
                        var t = editViewBridge.getParamHelp(helpType, helpParam) || ""
                        // v2.8.0：最长 200 字符截断（防止超长 tooltip 撑爆界面）
                        if (t.length > 200) t = t.substring(0, 200) + "…"
                        helpText = t
                    } else {
                        helpText = ""
                    }
                } catch (e) {
                    helpText = ""
                }
            }

            Component.onCompleted: updateHelpText()
            onHelpTypeChanged: updateHelpText()
            onHelpParamChanged: updateHelpText()

            // 灰色圆形 + 白色问号；无帮助文本时半透明显示
            Rectangle {
                id: helpBg
                anchors.fill: parent
                radius: 8
                color: helpMa.containsMouse ? Tok.DesignTokens.accentInfo : Tok.DesignTokens.textDisabled
                opacity: helpIcon.helpText.length > 0
                         ? (helpMa.containsMouse ? 1.0 : 0.7)
                         : 0.25

                Behavior on opacity { NumberAnimation { duration: 120 } }

                Text {
                    anchors.centerIn: parent
                    text: "?"
                    color: "#FFFFFF"
                    font.pixelSize: 11
                    font.bold: true
                }
            }

            MouseArea {
                id: helpMa
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: helpIcon.helpText.length > 0 ? Qt.PointingHandCursor : Qt.ArrowCursor
            }

            ToolTip {
                id: helpTip
                parent: helpIcon
                visible: helpMa.containsMouse && helpIcon.helpText.length > 0
                delay: 200
                timeout: 10000
                x: 18
                y: -6
                width: 240   // 多行显示，固定宽度 240px，最长 200 字符自动换行

                contentItem: Text {
                    text: helpIcon.helpText
                    color: Tok.DesignTokens.textPrimary
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                    lineHeight: 1.3
                }

                background: Rectangle {
                    color: Tok.DesignTokens.bgPanel
                    border.color: Tok.DesignTokens.borderDefault
                    border.width: 1
                    radius: Tok.DesignTokens.radiusSm
                }
            }
        }
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
            // v2.8.0 参数帮助 tooltip：hover "?" 显示 getParamHelp 内容
            Loader {
                sourceComponent: paramHelpIconComp
                Layout.preferredWidth: 16
                Layout.preferredHeight: 16
                Layout.alignment: Qt.AlignVCenter
                property string helpType: root.operatorType
                property string helpParam: intItem.paramName
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
            // v2.8.0 参数帮助 tooltip：hover "?" 显示 getParamHelp 内容
            Loader {
                sourceComponent: paramHelpIconComp
                Layout.preferredWidth: 16
                Layout.preferredHeight: 16
                Layout.alignment: Qt.AlignVCenter
                property string helpType: root.operatorType
                property string helpParam: floatItem.paramName
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
            // v2.8.0 参数帮助 tooltip：hover "?" 显示 getParamHelp 内容
            Loader {
                sourceComponent: paramHelpIconComp
                Layout.preferredWidth: 16
                Layout.preferredHeight: 16
                Layout.alignment: Qt.AlignVCenter
                property string helpType: root.operatorType
                property string helpParam: enumItem.paramName
            }
        }
    }

    // v5.4.0: 模型路径选择组件 — 当参数名为 modelPath 时使用
    // 模型列表由 root.modelList 提供（外部通过 bridge.getRegisteredModels() 绑定）
    // v5.4.1 修复：采用与 enum ComboBox 相同的显式同步策略，避免 modelList 异步
    // 填充或 currentValues 变化时 currentIndex 绑定不刷新，导致保存值丢失。
    // v5.4.2：ZeroShotDetect 算子模型路径与模型类型联动，使用 bridge.getZeroShotModelsByType
    // 过滤，ComboBox 设为 editable 以支持手动输入路径。
    Component {
        id: modelPathComp
        RowLayout {
            id: modelItem
            property var spec
            property string paramName: spec.name
            property var currentValue
            spacing: 6
            Layout.fillWidth: true
            height: 32

            // v5.4.2：零样本模型候选列表（按模型类型过滤）
            property var _zeroShotModels: root.modelList || []

            // v5.4.2：刷新零样本模型列表（根据当前 modelType 重新过滤）
            function _refreshZeroShotModels() {
                if (root.operatorType === "ZeroShotDetect" && root.bridge) {
                    var mt = root.getValue("modelType") || ""
                    _zeroShotModels = root.bridge.getZeroShotModelsByType(mt)
                    console.log("[modelPathComp] ZeroShotDetect: modelType=" + mt
                                + " candidates=" + _zeroShotModels.length)
                } else {
                    _zeroShotModels = root.modelList || []
                }
            }

            // spec 注入完成后延迟同步一次，防止 ComboBox 创建时 modelList 尚未就绪
            onSpecChanged: {
                Qt.callLater(function() {
                    if (modelCombo) modelCombo.syncIndexFromValue()
                })
            }

            Label {
                text: spec.cnName + (spec.unit ? " (" + spec.unit + ")" : "")
                color: Tok.DesignTokens.textPrimary
                font.pixelSize: 12
                Layout.preferredWidth: 100
            }
            ComboBox {
                id: modelCombo
                Layout.fillWidth: true
                Accessible.name: spec.cnName + " 模型选择"
                Accessible.description: spec.help || "从模型库中选择已注册的模型"
                // v5.4.2：ZeroShotDetect 使用按类型过滤的零样本列表，其他算子使用通用模型列表
                model: modelItem._zeroShotModels || []
                textRole: "displayName"
                valueRole: "filePath"
                // v5.4.2：支持手动输入路径（用户需求：路径始终支持自定义修改）
                editable: true
                // v5.4.1：去掉 currentIndex 绑定表达式，改命令式同步
                // 关键：syncIndexFromValue 读 root.getValue()（最新内部值），
                // 而非 modelItem.currentValue（可能为过时值），避免闪回/丢失。
                property bool _userEditing: false

                function syncIndexFromValue() {
                    var cv = root.getValue(modelItem.paramName)
                    if (!cv || cv === "") {
                        if (currentIndex !== -1) currentIndex = -1
                        return
                    }
                    var list = modelItem._zeroShotModels || []
                    for (var i = 0; i < list.length; ++i) {
                        if (list[i].filePath === cv) {
                            if (currentIndex !== i) currentIndex = i
                            return
                        }
                    }
                    // 保存的路径不在当前模型列表中：保持 -1，displayText 会显示原值
                    if (currentIndex !== -1) currentIndex = -1
                }

                Component.onCompleted: {
                    console.log("[modelPathComp] modelList.length = " + (modelItem._zeroShotModels ? modelItem._zeroShotModels.length : 0))
                    _refreshZeroShotModels()
                    syncIndexFromValue()
                }
                // v5.4.1：响应 _linkageTrigger / operatorType / modelList 变化，
                // 在快照/重置/模型库刷新后重新同步 currentIndex。
                // v5.4.2：ZeroShotDetect 在 modelType 变化时刷新候选列表。
                Connections {
                    target: root
                    function on_linkageTriggerChanged() {
                        // v5.4.2：modelType 变化时刷新候选（setValue 触发 _linkageTrigger++）
                        if (root.operatorType === "ZeroShotDetect") {
                            modelItem._refreshZeroShotModels()
                        }
                        if (!modelCombo._userEditing) modelCombo.syncIndexFromValue()
                    }
                    function onOperatorTypeChanged() {
                        modelItem._refreshZeroShotModels()
                        if (!modelCombo._userEditing) modelCombo.syncIndexFromValue()
                    }
                    function onModelListChanged() {
                        modelItem._refreshZeroShotModels()
                        if (!modelCombo._userEditing) modelCombo.syncIndexFromValue()
                    }
                }

                displayText: currentIndex >= 0 ? currentText : (modelItem.currentValue || editText || "（选择模型）")

                // v5.4.2 下拉选择 → 设置 filePath
                onActivated: function(index) {
                    modelCombo._userEditing = true
                    var list = modelItem._zeroShotModels || []
                    if (index >= 0 && index < list.length) {
                        var path = list[index].filePath
                        root.setValue(modelItem.paramName, path)
                    }
                    syncIndexFromValue()
                    Qt.callLater(function() { modelCombo._userEditing = false })
                }

                // v5.4.2 手动输入 → 使用输入文本作为路径
                onAccepted: {
                    if (editable && currentIndex === -1 && text.trim() !== "") {
                        root.setValue(modelItem.paramName, text.trim())
                    }
                }

                background: Rectangle {
                    color: modelCombo.activeFocus ? Tok.DesignTokens.bgHover : Tok.DesignTokens.bgSurface
                    border.color: modelCombo.activeFocus ? Tok.DesignTokens.borderFocus : Tok.DesignTokens.borderDefault
                    border.width: 1
                    radius: Tok.DesignTokens.radiusSm
                }
                // v5.4.2：editable 模式下需用可编辑的 TextField 作为 contentItem，
                // 否则无法真正输入文字（Qt Basic 样式实现）。
                contentItem: TextField {
                    leftPadding: 8
                    topPadding: 0
                    bottomPadding: 0
                    verticalAlignment: Text.AlignVCenter
                    font.pixelSize: 12
                    color: Tok.DesignTokens.textPrimary
                    selectionColor: Tok.DesignTokens.accent
                    selectedTextColor: Tok.DesignTokens.textPrimary
                    selectByMouse: true
                    persistentSelection: true
                    placeholderText: "（选择模型）"
                    placeholderTextColor: Tok.DesignTokens.textPlaceholder
                    background: Item {}
                    // editable：显示编辑文本（为空时回退显示已保存路径）；
                    // 非 editable：显示 displayText（含已保存路径）
                    text: modelCombo.editable
                          ? (modelCombo.editText !== "" ? modelCombo.editText : (modelItem.currentValue || ""))
                          : modelCombo.displayText
                    enabled: modelCombo.editable
                    readOnly: modelCombo.down
                    onTextChanged: {
                        if (modelCombo.editable) modelCombo.editText = text
                    }
                    onAccepted: {
                        if (modelCombo.editable) modelCombo.accept()
                    }
                }
            }
            // v2.8.0 参数帮助 tooltip：hover "?" 显示 getParamHelp 内容
            Loader {
                sourceComponent: paramHelpIconComp
                Layout.preferredWidth: 16
                Layout.preferredHeight: 16
                Layout.alignment: Qt.AlignVCenter
                property string helpType: root.operatorType
                property string helpParam: modelItem.paramName
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
            // v2.8.0 参数帮助 tooltip：hover "?" 显示 getParamHelp 内容
            Loader {
                sourceComponent: paramHelpIconComp
                Layout.preferredWidth: 16
                Layout.preferredHeight: 16
                Layout.alignment: Qt.AlignVCenter
                property string helpType: root.operatorType
                property string helpParam: boolItem.paramName
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
            // v2.8.0 参数帮助 tooltip：hover "?" 显示 getParamHelp 内容
            Loader {
                sourceComponent: paramHelpIconComp
                Layout.preferredWidth: 16
                Layout.preferredHeight: 16
                Layout.alignment: Qt.AlignVCenter
                property string helpType: root.operatorType
                property string helpParam: stringItem.paramName
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
            // v2.8.0 参数帮助 tooltip：hover "?" 显示 getParamHelp 内容
            Loader {
                sourceComponent: paramHelpIconComp
                Layout.preferredWidth: 16
                Layout.preferredHeight: 16
                Layout.alignment: Qt.AlignVCenter
                property string helpType: root.operatorType
                property string helpParam: vectorItem.paramName
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
                    // v5.4.0：modelPath 参数使用模型选择下拉组件（从模型库选择已注册模型）
                    if (modelData.name === "modelPath") return modelPathComp
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

        // ===== spec 阶段一 Task 4：通用 ROI 控件（所有算子自动获得 ROI 参数）=====
        // 设计：在算子参数表单末尾固定显示 ROI 控件，绑定到 currentValues["roi"]
        // - 矩形格式："x,y,w,h"
        // - 多边形格式："poly:x1,y1,x2,y2,..."
        // - 空字符串 = 全图（无 ROI，向后兼容）
        // ROI 值通过 setValue("roi", ...) 回传，buildToolChainFromNodes 解析后调用 setRoi
        Loader {
            id: roiLoader
            Layout.fillWidth: true
            source: "qrc:/qml/EditView/ROISelector.qml"
            onLoaded: {
                if (item) {
                    item.spec = ({
                        name: "roi",
                        cnName: "ROI 区域",
                        help: "算子级通用 ROI，留空表示全图。矩形 x,y,w,h 或多边形 poly:x1,y1,..."
                    })
                    var cv = root.getValue("roi")
                    item.currentValue = (cv !== undefined && cv !== null) ? String(cv) : ""
                    item.valueEdited.connect(function(newVal) {
                        root.setValue("roi", newVal)
                    })
                }
            }
            // 响应算子切换与参数重置，刷新 currentValue
            Connections {
                target: root
                function onOperatorTypeChanged() {
                    if (roiLoader.item) {
                        var cv = root.getValue("roi")
                        roiLoader.item.currentValue = (cv !== undefined && cv !== null) ? String(cv) : ""
                    }
                }
                function on_linkageTriggerChanged() {
                    if (roiLoader.item) {
                        var cv = root.getValue("roi")
                        roiLoader.item.currentValue = (cv !== undefined && cv !== null) ? String(cv) : ""
                    }
                }
            }
        }
    }
}
