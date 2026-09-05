// =====================================================================
// DesignTokens.qml — QDV 数据标注工具 本地设计令牌（本地副本）
//
// 来源：主工程 qml/EditView/DesignTokens.qml（深色体系，色值完全一致）
// 目的：标注工具专用本地模块，避免改动主工程共享令牌。
//      在原有令牌基础上【新增】标注专用令牌：
//        annSelectedOutline 选中虚线描边色
//        annHandleFill/Border 控制点
//        annHover 悬停态
//        candidateBorder 候选框边框（原硬编码 #FFB300）
//        edgeOverlayColor 边缘叠加（与 ImagePreprocess 默认 #00E5FF 一致）
//        logBg/logFg 训练日志区（原硬编码 #1a1a1a / #cfd8dc）
//        dirtyIndicator 未保存脏状态指示
// 约束：本地令牌色值 = 主工程令牌色值（仅新增不改原值），保证与 QDV 视觉一致。
// =====================================================================
pragma Singleton
import QtQuick

QtObject {
    // ============ 背景色阶（5 档，从深到浅）============
    readonly property color bgCanvas:   "#121212"  // 画布区（最深）
    readonly property color bgPanel:    "#1A1A1A"  // 左右面板
    readonly property color bgSurface:  "#242424"  // 卡片、控件
    readonly property color bgHover:    "#2F2F2F"  // 悬停态
    readonly property color bgHeader:   "#1F1F1F"  // 顶栏/分隔
    readonly property color bgPanelHeader: "#1F1F1F"  // 面板标题栏

    // ============ 文字色阶（5 档，全部通过 WCAG AA）============
    readonly property color textPrimary:    "#FFFFFF"  // 主标题 15.3:1
    readonly property color textSecondary:  "#C8C8C8"  // 正文 9.5:1
    readonly property color textTertiary:   "#9E9E9E"  // 辅助 5.8:1
    readonly property color textDisabled:   "#6E6E6E"  // 禁用 3.0:1（豁免）
    readonly property color textPlaceholder:"#757575"  // 占位 3.5:1（豁免）

    // ============ 语义色 ============
    readonly property color accentPrimary:      "#7C4DFF"  // 主强调（紫）
    readonly property color accentPrimaryHover: "#9D7BFF"  // 主强调悬停态
    readonly property color accentSuccess:      "#69F0AE"  // 成功/输出（绿）
    readonly property color accentWarning:      "#FFD740"  // 警告/输入端口（橙）
    readonly property color accentError:        "#FF5252"  // 错误/删除（红）
    readonly property color accentInfo:         "#448AFF"  // 信息/帮助（蓝）

    // ============ 边界/分隔 ============
    readonly property color borderDefault:  "#3D3D3D"  // 默认边框
    readonly property color borderFocus:    accentPrimary
    readonly property int   borderWidth:    1

    // ============ 标注专用令牌（本地新增，主工程无对应项）============
    readonly property color annSelectedOutline: "#9D7BFF"   // 选中标注虚线描边（亮紫，与主强调一致）
    readonly property color annHandleFill:      "#FFFFFF"   // 控制点填充
    readonly property color annHandleBorder:    accentPrimary // 控制点描边
    readonly property color annHover:           "#9D7BFF"   // 标注悬停描边
    readonly property color candidateBorder:    "#FFB300"   // 候选框边框（原硬编码）
    readonly property color edgeOverlayColor:   "#00E5FF"   // 边缘叠加青（与 ImagePreprocess 默认一致）
    readonly property color logBg:              "#1a1a1a"   // 训练日志背景（原硬编码）
    readonly property color logFg:              "#cfd8dc"   // 训练日志文字（原硬编码）
    readonly property color dirtyIndicator:     "#FFD740"   // 未保存脏状态圆点
    readonly property real  annHandleSize:      8           // 控制点尺寸

    // ============ 字体 ============
    readonly property string fontFamily:      '"Microsoft YaHei UI", "Microsoft YaHei", "PingFang SC", "Segoe UI", sans-serif'
    readonly property string fontMono:        '"Cascadia Code", "Consolas", monospace'
    readonly property string fontFamilyCJK:   '"Microsoft YaHei UI", "Microsoft YaHei", "PingFang SC", sans-serif'

    // 字号
    readonly property int fontSizeXxs:  10
    readonly property int fontSizeXs:   11
    readonly property int fontSizeSm:   12
    readonly property int fontSizeBase: 13
    readonly property int fontSizeLg:   14
    readonly property int fontSizeXl:   16
    readonly property int fontSize2xl:  20

    // 行高倍数
    readonly property real lineHeightTight:  1.2
    readonly property real lineHeightNormal: 1.35
    readonly property real lineHeightRelax:  1.5

    // ============ 间距（8px 网格）============
    readonly property int space1: 4
    readonly property int space2: 8
    readonly property int space3: 12
    readonly property int space4: 16
    readonly property int space5: 20
    readonly property int space6: 24
    readonly property int space8: 32

    // ============ 圆角 ============
    readonly property int radiusSm: 2
    readonly property int radiusMd: 4
    readonly property int radiusLg: 6
    readonly property int radiusXl: 8

    // ============ 组件尺寸 ============
    readonly property int controlHeight:     28
    readonly property int headerBarHeight:   40

    // ============ Motion Tokens ============
    readonly property int durationFast:     150
    readonly property int durationNormal:   250
    readonly property int durationSlow:     400
    readonly property string easeOutCubic:  "cubic-bezier(0.22, 1, 0.36, 1)"
    readonly property string easeOutBack:   "cubic-bezier(0.34, 1.56, 0.64, 1)"
    readonly property string easeInOutQuad: "cubic-bezier(0.45, 0, 0.55, 1)"

    // ============ Shadow Tokens ============
    readonly property string shadowSm:  "0 1px 2px rgba(0,0,0,0.3)"
    readonly property string shadowMd:  "0 4px 8px rgba(0,0,0,0.3)"
    readonly property string shadowLg:  "0 8px 24px rgba(0,0,0,0.4)"
    readonly property string shadowGlow: "0 0 12px rgba(124,77,255,0.3)"
}
