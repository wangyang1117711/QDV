// =====================================================================
// PreprocessPanel.qml — 自动预处理 / 弱边缘增强辅助标注 控制面板
//
// 对应后端 AnnotationSession 的预处理属性与方法：
//   - preprocessEnabled / preprocessMode / edgeOverlayEnabled
//   - analyzeCurrentImage() 量化诊断
//   - suggestRegionsForCurrent() / acceptAllCandidates(id) / clearCandidates()
// 视觉算法专家 SOP：先量化（诊断），再增强，再辅助标注，最后人工确认。
// =====================================================================
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QDVAnnotator 1.0 as Tok

ColumnLayout {
    id: pp
    spacing: 8

    // 外部注入：当前激活标签（全部接受候选框时使用）
    property int activeLabelId: -1
    // 本地状态：诊断结果显示
    property var stat: ({})

    function refreshStat() {
        pp.stat = session.analyzeCurrentImage()
    }
    Connections {
        target: session
        function onCurrentImageChanged() { refreshStat() }
        function onCurrentStatsChanged() { refreshStat() }
    }
    Component.onCompleted: refreshStat()

    // ---- 标题 ----
    Label {
        text: "自动预处理 / 弱边缘增强"
        font.bold: true
        font.family: Tok.DesignTokens.fontFamilyCJK
        font.pixelSize: Tok.DesignTokens.fontSizeBase
        color: Tok.DesignTokens.textPrimary
    }

    // ---- 开关 + 模式 ----
    RowLayout {
        spacing: 6
        Switch {
            id: preSw
            checked: session.preprocessEnabled
            onCheckedChanged: session.setPreprocessEnabled(checked)
        }
        Label { text: "启用预处理"; font.family: Tok.DesignTokens.fontFamilyCJK; color: Tok.DesignTokens.textPrimary }
        Item { Layout.fillWidth: true }
        Tok.WideComboBox {
            id: modeCombo
            enabled: session.preprocessEnabled
            textRole: "text"
            valueRole: "val"
            model: [
                { text: "自动", val: "auto" },
                { text: "原图(无)", val: "none" },
                { text: "CLAHE 对比度", val: "clahe" },
                { text: "非锐化掩膜", val: "unsharp" },
                { text: "仅边缘叠加", val: "edge" }
            ]
            Component.onCompleted: { currentIndex = 0; for (var i=0;i<model.length;i++) if (model[i].val===session.preprocessMode) currentIndex=i }
            onActivated: session.setPreprocessMode(model[currentIndex].val)
        }
    }

    RowLayout {
        spacing: 6
        Switch {
            id: edgeSw
            checked: session.edgeOverlayEnabled
            onCheckedChanged: session.setEdgeOverlayEnabled(checked)
        }
        Label { text: "边缘叠加(青色)"; font.family: Tok.DesignTokens.fontFamilyCJK; color: Tok.DesignTokens.textPrimary }
        Item { Layout.fillWidth: true }
        Label {
            text: (session.preprocessMode === "auto" ? "自动→" + ((pp.stat.weakEdge || pp.stat.lowContrast) ? "CLAHE" : "非锐化") : session.preprocessMode)
            font.family: Tok.DesignTokens.fontFamilyCJK
            color: Tok.DesignTokens.textTertiary
            font.pixelSize: Tok.DesignTokens.fontSizeXs
        }
    }

    // ---- 量化诊断卡片 ----
    Frame {
        Layout.fillWidth: true
        background: Rectangle { color: Tok.DesignTokens.bgSurface; radius: 4; border.color: Tok.DesignTokens.borderDefault; border.width: 1 }
        ColumnLayout {
            spacing: 3
            Label { text: "量化诊断（视觉算法专家依据）"; font.bold: true; font.family: Tok.DesignTokens.fontFamilyCJK; color: Tok.DesignTokens.textSecondary; font.pixelSize: Tok.DesignTokens.fontSizeXs }
            GridLayout {
                columns: 2; columnSpacing: 10; rowSpacing: 2
                Label { text: "均值"; font.family: Tok.DesignTokens.fontFamilyCJK; color: Tok.DesignTokens.textTertiary; font.pixelSize: Tok.DesignTokens.fontSizeXs }
                Label { text: (pp.stat.mean !== undefined ? pp.stat.mean.toFixed(1) : "—"); font.family: Tok.DesignTokens.fontFamilyCJK; color: Tok.DesignTokens.textPrimary; font.pixelSize: Tok.DesignTokens.fontSizeXs }
                Label { text: "标准差"; font.family: Tok.DesignTokens.fontFamilyCJK; color: Tok.DesignTokens.textTertiary; font.pixelSize: Tok.DesignTokens.fontSizeXs }
                Label { text: (pp.stat.stddev !== undefined ? pp.stat.stddev.toFixed(1) : "—"); font.family: Tok.DesignTokens.fontFamilyCJK; color: Tok.DesignTokens.textPrimary; font.pixelSize: Tok.DesignTokens.fontSizeXs }
                Label { text: "边缘密度"; font.family: Tok.DesignTokens.fontFamilyCJK; color: Tok.DesignTokens.textTertiary; font.pixelSize: Tok.DesignTokens.fontSizeXs }
                Label { text: (pp.stat.edgeDensity !== undefined ? pp.stat.edgeDensity.toFixed(4) : "—"); font.family: Tok.DesignTokens.fontFamilyCJK; color: Tok.DesignTokens.textPrimary; font.pixelSize: Tok.DesignTokens.fontSizeXs }
                Label { text: "判定"; font.family: Tok.DesignTokens.fontFamilyCJK; color: Tok.DesignTokens.textTertiary; font.pixelSize: Tok.DesignTokens.fontSizeXs }
                Label {
                    text: (pp.stat.lowContrast ? "低对比 " : "") + (pp.stat.weakEdge ? "弱边缘" : "")
                    font.family: Tok.DesignTokens.fontFamilyCJK
                    color: (pp.stat.lowContrast || pp.stat.weakEdge) ? Tok.DesignTokens.accentError : Tok.DesignTokens.accentSuccess
                    font.pixelSize: Tok.DesignTokens.fontSizeXs
                }
            }
        }
    }

    // ---- 辅助标注动作 ----
    RowLayout {
        spacing: 6
        Button {
            text: "建议候选框"
            Layout.fillWidth: true
            onClicked: session.suggestRegionsForCurrent()
            ToolTip.text: "在(增强后)图上求 Sobel→二值化→闭运算→连通域，列出候选"
        }
    }
    RowLayout {
        spacing: 6
        Button {
            text: "全部接受"
            highlighted: true
            enabled: session.candidateRegions.length > 0
            onClicked: session.acceptAllCandidates(activeLabelId)
            ToolTip.text: "将全部候选框作为矩形标注接受（使用激活标签）"
        }
        Button {
            text: "清除候选"
            enabled: session.candidateRegions.length > 0
            onClicked: session.clearCandidates()
        }
    }
    Label {
        text: "候选框: " + session.candidateRegions.length + " 个（橙色虚框，score 越高越可能是目标）"
        font.family: Tok.DesignTokens.fontFamilyCJK
        color: Tok.DesignTokens.textTertiary
        font.pixelSize: Tok.DesignTokens.fontSizeXs
        wrapMode: Text.Wrap
    }
}
