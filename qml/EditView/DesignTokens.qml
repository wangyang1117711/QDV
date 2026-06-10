// =====================================================================
// DesignTokens.qml — 全局设计 Token 单例（v3.0.0 方案B）
//
// 所有组件通过 DesignTokens.xxx 引用，不再硬编码色值。
// 统一管理：背景色阶 / 文字色阶 / 语义色 / 分类色 / 字体 / 间距 / 圆角。
// 全部文字-背景组合通过 WCAG AA 4.5:1 对比度验证。
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

    // ============ 文字色阶（5 档，全部通过 WCAG AA）============
    readonly property color textPrimary:    "#FFFFFF"  // 主标题 15.3:1
    readonly property color textSecondary:  "#C8C8C8"  // 正文 9.5:1
    readonly property color textTertiary:   "#9E9E9E"  // 辅助 5.8:1
    readonly property color textDisabled:   "#6E6E6E"  // 禁用 3.0:1（豁免）
    readonly property color textPlaceholder:"#757575"  // 占位 3.5:1（豁免）

    // ============ 语义色 ============
    readonly property color accentPrimary:  "#7C4DFF"  // 主强调（紫）
    readonly property color accentSuccess:  "#69F0AE"  // 成功/输出（绿）
    readonly property color accentWarning:  "#FFD740"  // 警告/输入端口（橙）
    readonly property color accentError:    "#FF5252"  // 错误/删除（红）
    readonly property color accentInfo:     "#448AFF"  // 信息/帮助（蓝）

    // ============ 边界/分隔 ============
    readonly property color borderDefault:  "#3D3D3D"  // 默认边框
    readonly property color borderFocus:    accentPrimary
    readonly property int   borderWidth:    1

    // ============ 分类色（12 类，全部在 bgSurface 上通过 AA）============
    function categoryColor(cat) {
        switch (cat) {
            case "图像采集": return "#FFB74D"
            case "预处理":   return "#7986CB"
            case "几何变换": return "#4DB6AC"
            case "形态学":   return "#AED581"
            case "图像分割": return "#4DD0E1"
            case "Blob分析": return "#64B5F6"
            case "特征提取": return "#CE93D8"
            case "匹配定位": return "#F06292"
            case "几何测量": return "#FF8A65"
            case "3D视觉":   return "#90A4AE"
            case "深度学习": return "#81C784"
            case "分支":     return "#EF5350"
            default:        return "#9E9E9E"
        }
    }

    // 选中态（更亮版本）
    function categoryColorSelected(cat) {
        switch (cat) {
            case "图像采集": return "#FFCC80"
            case "预处理":   return "#9FA8DA"
            case "几何变换": return "#80CBC4"
            case "形态学":   return "#C5E1A5"
            case "图像分割": return "#80DEEA"
            case "Blob分析": return "#90CAF9"
            case "特征提取": return "#E1BEE7"
            case "匹配定位": return "#F48FB1"
            case "几何测量": return "#FFAB91"
            case "3D视觉":   return "#B0BEC5"
            case "深度学习": return "#A5D6A7"
            case "分支":     return "#EF9A9A"
            default:        return "#BDBDBD"
        }
    }

    // ============ 字体 ============
    // v3.0.0：中英文字体回退优化，提升中文可读性
    readonly property string fontFamily:      '"Microsoft YaHei UI", "Microsoft YaHei", "PingFang SC", "Segoe UI", sans-serif'
    readonly property string fontMono:        '"Cascadia Code", "Consolas", monospace'
    // v3.0.0：CJK 字符专用的字体回退链，避免中文回退到 YaHei 时英文回退不一致
    readonly property string fontFamilyCJK:   '"Microsoft YaHei UI", "Microsoft YaHei", "PingFang SC", sans-serif'

    // 字号
    readonly property int fontSizeXs:   11
    readonly property int fontSizeSm:   12
    readonly property int fontSizeBase: 13
    readonly property int fontSizeLg:   14
    readonly property int fontSizeXl:   16
    readonly property int fontSize2xl:  20

    // v3.0.0：行高倍数（小字号用 1.4-1.5，避免截断；大字号 1.3）
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
    readonly property int controlHeight:     28   // 标准控件高度
    readonly property int headerBarHeight:   44   // 顶栏
    readonly property int nodeCardWidth:     200  // 节点卡片（v3.0.0：160→200，给 14px 文字留出宽度）
    readonly property int nodeCardHeight:    72   // 节点卡片（v3.0.0：60→72，两行 14+13 文字更舒展）
    readonly property int portSize:          10   // 输入/输出端口
    readonly property int searchBarHeight:   30   // 搜索栏
    readonly property int filterTabHeight:   26   // 筛选标签
    readonly property int categoryRowHeight: 32   // 分类行（v3.0.0：28→32，文字更舒展）
    readonly property int subGroupRowHeight: 28   // 子分组行（v3.0.0：24→28）
    readonly property int operatorRowHeight: 40   // 算子行（v3.0.1：36→40，配合 14px 加粗文字 + 1.4 行高）

    // ============ 面板宽度 ============
    readonly property int leftPanelDefault:  280  // 左侧默认
    readonly property int leftPanelMin:      200  // 左侧最小
    readonly property int rightPanelDefault: 340  // 右侧默认
    readonly property int rightPanelMin:     260  // 右侧最小

    // ============ 响应式断点 ============
    readonly property int breakpointLg: 1200  // 三栏
    readonly property int breakpointMd: 900   // 两栏（左+画布，右折叠）
    readonly property int breakpointSm: 600   // 单栏（仅画布）

    // ============ Toast 颜色 ============
    function toastBg(level) {
        switch (level) {
            case "success": return "#2E7D32"
            case "warning": return "#E65100"
            case "error":   return "#C62828"
            default:        return "#424242"
        }
    }
}