// =====================================================================
// FilePathField.qml — 路径输入专用组件（v3.2.0 算子参数编辑器增强）
//
// 用途：
// - 替换 ParamForm 中 String 类型的 filePath 参数输入
// - 在 TextField 右侧追加「浏览…」按钮
// - 浏览时弹出 FileDialog，初始目录优先采用「上次使用目录」
// - 路径记忆：使用 Qt.labs.settings 跨会话保存
// - 默认路径：D:\图片\6人影.png（首次使用时）
//
// 外部接口：
//   spec:           OperatorParamMeta（含 name/cnName/defaultValue/help）
//   paramName:      参数名（外部注入后 binding 到 setValue）
//   currentValue:   节点当前参数值
//   onValuePicked:  用户浏览选择/输入确认后回传新值
//
// 设计要点：
// - 复制 ParamForm 的 RowLayout + Label 风格，视觉一致
// - 浏览按钮在 path 为空 / 有值时均可用（覆盖补全场景）
// - 路径不合法时仅做软提示（不阻断），由上游算子执行时报错
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
    property string paramName: spec.name
    property var    currentValue
    spacing: 6
    Layout.fillWidth: true
    height: 32

    /// 新值回传：与 ParamForm.setValue(name, val) 协议对齐
    signal valuePicked(string newValue)

    /// 静态默认路径（首次启动时使用）
    readonly property string defaultImagePath: "D:\\图片\\6人影.png"

    // ============ 路径记忆（跨会话）============
    Settings {
        id: pathMemory
        // 文件：build/bin/appData/lastImageDir.ini
        // Key：lastImageDir，Value：用户上次选择的目录
        category: "EditView/FilePath"
        property string lastImageDir: ""
    }

    // 当前输入框内容
    property string _text: {
        if (currentValue !== undefined && currentValue !== null
                && String(currentValue) !== "")
            return String(currentValue)
        // 首次进入：显示默认路径
        return defaultImagePath
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
        // 提示文字（路径为空时显示）
        placeholderText: spec.help || "支持 png/jpg/bmp/tiff 格式"
        placeholderTextColor: Tok.DesignTokens.textPlaceholder
        text: root._text
        color: Tok.DesignTokens.textPrimary
        font.pixelSize: 12
        font.family: Tok.DesignTokens.fontMono
        selectByMouse: true
        // 启用复制/粘贴等标准编辑操作
        persistentSelection: true
        background: Rectangle {
            color: pathField.activeFocus ? Tok.DesignTokens.bgHover : Tok.DesignTokens.bgSurface
            border.color: pathField.activeFocus ? Tok.DesignTokens.borderFocus : Tok.DesignTokens.borderDefault
            border.width: 1
            radius: Tok.DesignTokens.radiusSm
        }
        onEditingFinished: {
            if (text !== root._text) {
                root._text = text
                root.valuePicked(text)
            }
        }
        onTextChanged: {
            if (text !== root._text) root._text = text
        }
        // 右键菜单：清空
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.RightButton
            onClicked: function(mouse) {
                clearMenu.popup()
            }
            Menu {
                id: clearMenu
                MenuItem {
                    text: "清空"
                    onTriggered: { pathField.text = ""; root._text = ""; root.valuePicked("") }
                }
                MenuItem {
                    text: "恢复默认路径"
                    onTriggered: {
                        pathField.text = root.defaultImagePath
                        root._text = root.defaultImagePath
                        root.valuePicked(root.defaultImagePath)
                    }
                }
            }
        }
    }

    // ============ 浏览按钮 ============
    Button {
        id: browseBtn
        text: "浏览…"
        Accessible.name: "浏览文件路径"
        // 紧凑尺寸，与 TextField 同行
        Layout.preferredWidth: 60
        Layout.preferredHeight: 32
        font.pixelSize: 11
        font.family: Tok.DesignTokens.fontFamilyCJK
        // 复用 ParamForm 中 String field 的 bgSurface 风格
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
        // 多格式过滤（与读图算子支持的格式一致）
        nameFilters: [
            "图像文件 (*.png *.jpg *.jpeg *.bmp *.tiff *.tif *.webp)",
            "PNG 图像 (*.png)",
            "JPEG 图像 (*.jpg *.jpeg)",
            "BMP 图像 (*.bmp)",
            "TIFF 图像 (*.tiff *.tif)",
            "WebP 图像 (*.webp)",
            "所有文件 (*)"
        ]
        // 初始目录：上次记忆 > 默认目录 > 用户主目录
        currentFolder: {
            if (pathMemory.lastImageDir && pathMemory.lastImageDir !== "")
                return "file:///" + pathMemory.lastImageDir.replace(/\\/g, "/")
            if (root._text && root._text !== "" && root._text.indexOf("\\") >= 0) {
                // 尝试提取当前路径的目录
                var idx = Math.max(root._text.lastIndexOf("\\"),
                                   root._text.lastIndexOf("/"))
                if (idx > 0) {
                    var dir = root._text.substring(0, idx)
                    return "file:///" + dir.replace(/\\/g, "/")
                }
            }
            return "file:///" + root.defaultImagePath.replace(/\\/g, "/")
        }
        onAccepted: {
            // FileDialog 返回的 selectedFile 是 file:/// URL
            var picked = String(selectedFile)
            // 转 Windows 本地路径
            if (picked.startsWith("file:///")) picked = picked.substring(8)
            else if (picked.startsWith("file://")) picked = picked.substring(7)
            // 记忆目录（取父目录）
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
