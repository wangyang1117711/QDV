// =====================================================================
// DeployDialog.qml — 部署（整链运行）结果模态对话框（v2.5.0 功能 3b）
//
// 功能：
// - 展示整链执行摘要：输入图像 / 总算子数 / 成功失败数 / 总耗时
// - 算子执行列表：每行显示算子名/状态/耗时/错误信息
// - 输出图像预览（复用 ImageViewer，支持缩放/平移）
// - 关闭 / 查看大图（打开 ImagePreviewWindow）
//
// 入口：
//   var dlg = deployDialogComponent.createObject(root, {
//       inputImagePath: "C:/path/to/input.png",
//       result: { success, totalTools, successCount, failCount,
//                 elapsedMs, outputImagePath, toolResults }
//   })
//   dlg.open()
// =====================================================================

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import QDV.EditView 3.0 as Tok

// v5.3：改为 Popup（而非 Dialog），避免 Dialog 的 Overlay 创建独立 QRhi 上下文。
Popup {
    id: dlg
    modal: true
    width: 900
    height: 680
    padding: 0
    // v5.3.1：Popup 不支持 anchors，用 x/y 手动居中
    x: (parent.width - width) / 2
    y: (parent.height - height) / 2
    background: Rectangle {
        color: Tok.DesignTokens.bgPanel
        border.color: Tok.DesignTokens.accentPrimary
        border.width: 1
        radius: Tok.DesignTokens.radiusMd
    }

    // ============ 外部接口 ============
    property string inputImagePath: ""
    property var result: ({})   // runScheme 返回的 QVariantMap

    // ============ 顶部摘要栏 ============
    header: Rectangle {
        height: 56
        color: Tok.DesignTokens.bgPanelHeader
        radius: Tok.DesignTokens.radiusMd

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            spacing: 12

            // 状态图标（成功/失败）
            Text {
                text: dlg.result && dlg.result.success ? "\u2705" : "\u274C"
                font.pixelSize: 20
                Layout.alignment: Qt.AlignVCenter
            }

            Text {
                text: dlg.result && dlg.result.success ? "部署执行完成" : "部署执行失败"
                color: Tok.DesignTokens.textPrimary
                font.pixelSize: 16
                font.bold: true
                Layout.alignment: Qt.AlignVCenter
            }

            Item { Layout.fillWidth: true }

            // 总耗时
            Text {
                text: "总耗时：" + (dlg.result ? dlg.result.elapsedMs : 0) + " ms"
                color: Tok.DesignTokens.textSecondary
                font.pixelSize: 12
                Layout.alignment: Qt.AlignVCenter
            }
        }
    }

    // ============ 主内容 ============
    contentItem: ColumnLayout {
        spacing: 0
        Layout.fillWidth: true
        Layout.fillHeight: true

        // ----- 摘要信息区 -----
        Rectangle {
            Layout.fillWidth: true
            Layout.margins: 12
            height: 84
            color: Tok.DesignTokens.bgCanvas
            radius: Tok.DesignTokens.radiusSm
            border.width: 1
            border.color: Tok.DesignTokens.borderDefault

            RowLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 24

                // 输入图像
                ColumnLayout {
                    spacing: 2
                    Text {
                        text: "输入图像"
                        color: Tok.DesignTokens.textSecondary
                        font.pixelSize: 11
                    }
                    Text {
                        text: {
                            var p = dlg.inputImagePath
                            var idx = p.lastIndexOf("/")
                            return idx >= 0 ? p.substring(idx + 1) : p
                        }
                        color: Tok.DesignTokens.textPrimary
                        font.pixelSize: 12
                        elide: Text.ElideRight
                        Layout.preferredWidth: 200
                        ToolTip.text: dlg.inputImagePath
                        ToolTip.visible: hovered
                    }
                }

                // 算子总数
                ColumnLayout {
                    spacing: 2
                    Text {
                        text: "算子总数"
                        color: Tok.DesignTokens.textSecondary
                        font.pixelSize: 11
                    }
                    Text {
                        text: dlg.result ? dlg.result.totalTools : 0
                        color: Tok.DesignTokens.textPrimary
                        font.pixelSize: 18
                        font.bold: true
                    }
                }

                // 成功数
                ColumnLayout {
                    spacing: 2
                    Text {
                        text: "成功"
                        color: Tok.DesignTokens.textSecondary
                        font.pixelSize: 11
                    }
                    Text {
                        text: dlg.result ? dlg.result.successCount : 0
                        color: "#27ae60"
                        font.pixelSize: 18
                        font.bold: true
                    }
                }

                // 失败数
                ColumnLayout {
                    spacing: 2
                    Text {
                        text: "失败"
                        color: Tok.DesignTokens.textSecondary
                        font.pixelSize: 11
                    }
                    Text {
                        text: dlg.result ? dlg.result.failCount : 0
                        color: dlg.result && dlg.result.failCount > 0 ? "#e74c3c" : Tok.DesignTokens.textPrimary
                        font.pixelSize: 18
                        font.bold: true
                    }
                }

                Item { Layout.fillWidth: true }
            }
        }

        // ----- 算子执行列表 -----
        Text {
            Layout.leftMargin: 16
            Layout.topMargin: 4
            text: "算子执行明细"
            color: Tok.DesignTokens.textSecondary
            font.pixelSize: 12
            font.bold: true
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.margins: 12
            Layout.preferredHeight: 180
            clip: true

            ListView {
                id: toolList
                model: dlg.result && dlg.result.toolResults ? dlg.result.toolResults : []
                spacing: 4
                implicitHeight: contentHeight

                delegate: Rectangle {
                    width: toolList.width
                    height: 36
                    color: index % 2 === 0 ? Tok.DesignTokens.bgPanel : Tok.DesignTokens.bgCanvas
                    radius: 2

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        spacing: 8

                        // 状态图标
                        Text {
                            text: modelData.ok ? "\u2705" : "\u274C"
                            font.pixelSize: 14
                            Layout.alignment: Qt.AlignVCenter
                        }

                        // 序号
                        Text {
                            text: (index + 1) + "."
                            color: Tok.DesignTokens.textSecondary
                            font.pixelSize: 12
                            Layout.alignment: Qt.AlignVCenter
                            Layout.preferredWidth: 24
                        }

                        // 算子名称
                        Text {
                            text: modelData.toolName || modelData.toolId || "未知算子"
                            color: Tok.DesignTokens.textPrimary
                            font.pixelSize: 12
                            Layout.alignment: Qt.AlignVCenter
                            Layout.preferredWidth: 180
                            elide: Text.ElideRight
                        }

                        // 耗时
                        Text {
                            text: modelData.elapsedMs + " ms"
                            color: Tok.DesignTokens.textSecondary
                            font.pixelSize: 11
                            Layout.alignment: Qt.AlignVCenter
                            Layout.preferredWidth: 80
                        }

                        // 错误信息（失败时显示）
                        Text {
                            text: modelData.ok ? "" : (modelData.errorMessage || "执行失败")
                            color: "#e74c3c"
                            font.pixelSize: 11
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            ToolTip.text: modelData.errorMessage || ""
                            ToolTip.visible: hovered && !modelData.ok
                        }
                    }
                }
            }
        }

        // ----- 输出图像预览 -----
        Text {
            Layout.leftMargin: 16
            Layout.topMargin: 4
            text: "输出图像预览（滚轮缩放 / 左键拖拽 / 右键适应）"
            color: Tok.DesignTokens.textSecondary
            font.pixelSize: 12
            font.bold: true
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.margins: 12
            Layout.fillHeight: true
            color: Tok.DesignTokens.bgCanvas
            border.width: 1
            border.color: Tok.DesignTokens.borderDefault
            radius: Tok.DesignTokens.radiusSm

            ImageViewer {
                id: outputViewer
                anchors.fill: parent
                anchors.margins: 1
                imageSource: dlg.result && dlg.result.outputImagePath
                             ? "file:///" + dlg.result.outputImagePath
                             : ""
            }
        }
    }

    // ============ 底部按钮 ============
    footer: Rectangle {
        height: 52
        color: Tok.DesignTokens.bgPanelHeader
        radius: Tok.DesignTokens.radiusMd

        RowLayout {
            anchors.fill: parent
            anchors.margins: 12
            spacing: 8

            Item { Layout.fillWidth: true }

            // 查看大图（打开内嵌预览 Popup）
            // v3.2.0 修复：ImagePreviewWindow 已改为 Popup（不再是独立 Window），
            // 必须用 open() 替代 show()，且 parent 必须是 Item（不能用 Dialog/Popup 自身）。
            Button {
                text: "查看大图"
                enabled: dlg.result && dlg.result.outputImagePath && dlg.result.outputImagePath !== ""
                Layout.alignment: Qt.AlignVCenter
                onClicked: {
                    var comp = Qt.createComponent("qrc:/qml/EditView/ImagePreviewWindow.qml")
                    if (comp.status === Component.Ready) {
                        // Popup 的 parent 必须是 Item；ApplicationWindow.overlay 是顶层 Item
                        // 这里用 dlg.parent（即 DeployDialog 自身的 parent Item）
                        var parentItem = dlg.parent
                        var popup = comp.createObject(parentItem, {
                            sourceImage: "file:///" + dlg.inputImagePath,
                            processedImage: dlg.result && dlg.result.outputImagePath
                                            ? "file:///" + dlg.result.outputImagePath
                                            : "",
                            imageTitle: "部署输出对比"
                        })
                        // Popup 不再支持 show()，改用 open()
                        popup.open()
                    }
                }
            }

            // 关闭
            Button {
                text: "关闭"
                Layout.alignment: Qt.AlignVCenter
                highlighted: true
                onClicked: dlg.close()
            }
        }
    }
}
