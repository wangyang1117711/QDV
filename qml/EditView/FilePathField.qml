// =====================================================================
// FilePathField.qml — 路径输入专用组件（v3.2.0 算子参数编辑器增强）
//
// 用途：
// - 替换 ParamForm 中 String 类型的 filePath 参数输入
// - 在 TextField 右侧追加「浏览…」按钮
// - 浏览时弹出 FileDialog，初始目录优先采用「上次使用目录」
// - 路径记忆：使用 Qt.labs.settings 跨会话保存
//
// P1-A8/A9/A10 修复：
// - 移除 onTextChanged 对 _text 的同步，修复手动输入路径无法回传
// - 移除覆盖 TextField 的右键 MouseArea，恢复标准复制/粘贴菜单
// - 移除硬编码开发机路径 D:\图片\6人影.png，改为空路径提示用户选择
//
// 外部接口：
//   spec:           OperatorParamMeta（含 name/cnName/defaultValue/help）
//   paramName:      参数名（外部注入后 binding 到 setValue）
//   currentValue:   节点当前参数值
//   onValuePicked:  用户浏览选择/输入确认后回传新值
// =====================================================================

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import Qt.labs.settings
import QDV.EditView 3.0 as Tok

RowLayout {
    id: root
    property var    spec
    property string paramName: spec ? (spec.name || "") : ""
    property var    currentValue
    spacing: 6
    Layout.fillWidth: true
    height: 32

    /// 新值回传：与 ParamForm.setValue(name, val) 协议对齐
    signal valuePicked(string newValue)

    // ============ 路径记忆（跨会话）============
    Settings {
        id: pathMemory
        category: "EditView/FilePath"
        property string lastImageDir: ""
    }

    // 当前输入框内容（P1-A10 修复：移除硬编码默认路径，改为空）
    property string _text: {
        if (currentValue !== undefined && currentValue !== null
                && String(currentValue) !== "")
            return String(currentValue)
        return ""
    }
    onCurrentValueChanged: {
        if (currentValue !== undefined && currentValue !== null
                && String(currentValue) !== "")
            _text = String(currentValue)
    }

    // ============ 字段名 Label ============
    Label {
        text: spec.cnName + (spec.unit ? " (" + spec.unit + ")" : "")
        color: Tok.DesignTokens.textPrimary
        font.pixelSize: 12
        font.family: Tok.DesignTokens.fontFamilyCJK
        Layout.preferredWidth: 120
    }

    // ============ 路径输入框（支持手动输入/粘贴）============
    TextField {
        id: pathField
        Layout.fillWidth: true
        Accessible.name: spec.cnName + " 路径输入"
        Accessible.description: spec.help || "支持手动输入或粘贴文件路径"
        placeholderText: spec.help || "支持 png/jpg/bmp/tiff 格式"
        placeholderTextColor: Tok.DesignTokens.textPlaceholder
        text: root._text
        color: Tok.DesignTokens.textPrimary
        font.pixelSize: 12
        font.family: Tok.DesignTokens.fontMono
        selectByMouse: true
        persistentSelection: true
        background: Rectangle {
            color: pathField.activeFocus ? Tok.DesignTokens.bgHover : Tok.DesignTokens.bgSurface
            border.color: pathField.activeFocus ? Tok.DesignTokens.borderFocus : Tok.DesignTokens.borderDefault
            border.width: 1
            radius: Tok.DesignTokens.radiusSm
        }
        // P1-A8 修复：onEditingFinished 无条件触发 valuePicked
        // 之前 onTextChanged 已同步 _text，导致 onEditingFinished 比较永远为 false，手动输入无法回传
        onEditingFinished: {
            root._text = text
            root.valuePicked(text)
        }
        // P1-B 阻断修复：TextField.menu 在 QtQuick.Controls 2 中不存在（是 Controls 1 的 API）
        // P1-A9 错误使用 menu 属性导致 QML 加载失败 → ParamForm/PropertyPreviewPanel/Main.qml 连锁失败 → EditView 空白
        // 改用 TapHandler 触发右键菜单（不阻止 TextField 内部鼠标处理，保留标准复制/粘贴）
        TapHandler {
            acceptedButtons: Qt.RightButton
            grabPermissions: TapHandler.CanTakeOverFromHandlersOfDifferentType
            onTapped: contextMenu.popup(pathField, eventPoint.position.x, eventPoint.position.y)
        }
    }

    // P1-B 阻断修复：右键上下文菜单（清空功能）
    Menu {
        id: contextMenu
        MenuItem {
            text: "清空"
            onTriggered: {
                pathField.clear()
                root._text = ""
                root.valuePicked("")
            }
        }
    }

    // ============ 浏览按钮 ============
    Button {
        id: browseBtn
        text: "浏览…"
        Accessible.name: "浏览文件路径"
        Layout.preferredWidth: 60
        Layout.preferredHeight: 32
        font.pixelSize: 11
        font.family: Tok.DesignTokens.fontFamilyCJK
        background: Rectangle {
            color: browseBtn.hovered ? Tok.DesignTokens.bgHover : Tok.DesignTokens.bgSurface
            border.color: Tok.DesignTokens.borderDefault
            border.width: 1
            radius: Tok.DesignTokens.radiusSm
        }
        contentItem: Label {
            text: browseBtn.text
            color: Tok.DesignTokens.textPrimary
            font: browseBtn.font
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        onClicked: fileDialog.open()
    }

    // ============ 文件选择对话框 ============
    FileDialog {
        id: fileDialog
        title: "选择图像文件"
        fileMode: FileDialog.OpenFile
        nameFilters: [
            "图像文件 (*.png *.jpg *.jpeg *.bmp *.tiff *.tif *.webp)",
            "PNG 图像 (*.png)",
            "JPEG 图像 (*.jpg *.jpeg)",
            "BMP 图像 (*.bmp)",
            "TIFF 图像 (*.tiff *.tif)",
            "WebP 图像 (*.webp)",
            "所有文件 (*)"
        ]
        // 初始目录：上次记忆 > 当前路径目录 > 用户主目录
        currentFolder: {
            if (pathMemory.lastImageDir && pathMemory.lastImageDir !== "")
                return "file:///" + pathMemory.lastImageDir.replace(/\\/g, "/")
            if (root._text && root._text !== "") {
                var idx = Math.max(root._text.lastIndexOf("\\"),
                                   root._text.lastIndexOf("/"))
                if (idx > 0) {
                    var dir = root._text.substring(0, idx)
                    return "file:///" + dir.replace(/\\/g, "/")
                }
            }
            // P1-A10 修复：fallback 到用户主目录而非硬编码路径
            return "file:///" + Qt.resolvedUrl("~").toString().substring(8)
        }
        onAccepted: {
            var picked = String(selectedFile)
            if (picked.startsWith("file:///")) picked = picked.substring(8)
            else if (picked.startsWith("file://")) picked = picked.substring(7)
            var sepIdx = Math.max(picked.lastIndexOf("\\"),
                                  picked.lastIndexOf("/"))
            if (sepIdx > 0) {
                pathMemory.lastImageDir = picked.substring(0, sepIdx)
            }
            pathField.text = picked
            root._text = picked
            root.valuePicked(picked)
        }
    }
}
