# 调试会话：editview-blank-screen-v2

- **报告日期**: 2026-06-09
- **状态**: [FIXED]

## 症状
编辑模块（EditView）界面空白，组件渲染失败。

## 预分析

### 已知修复历史
1. v3.0.0：`qmldir` 类型注册 → 静态库链接问题，通过 C++ `qmlRegisterSingletonType` / `qmlRegisterType` 解决
2. v3.0.1：字体名 `fontFamilyCJKCJK` 错误（12处）、算子行渲染优化
3. v3.0.1b：ListView 白色高亮残留导致白底白字 → 禁用 highlight

### 5 个可证伪假设

| # | 假设 | 观察位置 | 验证方法 |
|---|------|----------|----------|
| H1 | QML 类型注册失效（DesignTokens 未加载或因字体名错误崩溃） | QML 引擎加载 EditView 时的报错 | 插桩日志 `console.error` + QQuickWidget::Status |
| H2 | QQuickWidget 的 source 路径错误或 QRC 资源未打包 | QQuickWidget::source() 加载状态 | 插桩 `component.onCompleted` + status 日志 |
| H3 | QML 中某组件报错（如 DesignTokens.fontFamilyCJKCJK 未定义）导致级联失败 | QML engine warnings/errors | HTTP 上报 QML 错误 |
| H4 | 上一轮修复（禁用 highlight、改 transparent→bgPanel）引入了 QML 语法错误 | 具体行号 | 编译日志 + QML 解析错误 |
| H5 | Windows 主题/DPI 变化导致 Basic 样式异常 | QuickStyle 加载 | 日志 QuickStyle 实际值 |

## 插桩记录

| 时间 | 文件 | 行 | 内容 |
|------|------|-----|------|
| 2026-06-09 15:27 | EditView.cpp | 16-35 | 添加 `reportDebugEvent` 辅助函数 + QNetwork includes |
| 2026-06-09 15:27 | EditView.cpp | 123-179 | 3 处 instrumentation blocks（QML 加载状态/engine warnings/QuickStyle） |
| 2026-06-09 15:28 | Main.qml | 27-60 | Component.onCompleted XMLHttpRequest 上报 |

## 证据分析

| 假设 | 状态 | 证据 |
|------|------|------|
| H1 - QML 类型注册失效 | ❌ 排除 | 日志显示 status=Ready，DesignTokens 可用 |
| H2 - QQuickWidget source 路径错误 | ❌ 排除 | `qrc:/qml/EditView/Main.qml` 正常加载 |
| H3 - QML 组件报错导致级联失败 | ❌ 排除 | 修复后无 QML warnings |
| **H4 - 上一轮修复引入 QML 语法错误** | **✅ 根因** | **`background: Rectangle {...}` 对 ListView 无效，QML 解析崩** |
| H5 - Windows 主题/Basic 样式异常 | ❌ 排除 | `QT_QUICK_CONTROLS_STYLE=Basic` 正确 |

### 根因细节

v3.0.1b 修复"白底白字"时在 `Main.qml:448` 添加：
```qml
background: Rectangle { color: "transparent" }
```
但 `ListView`（来自 QtQuick 模块）**没有 `background` 属性**（该属性是 QtQuick.Controls 的 Pane/ScrollView 等控件专属）。
QML 引擎解析到该行时报 `Cannot assign to non-existent property "background"`，
整个 EditView 组件加载中断 → 界面空白。

## 修复方案

- **删除** `background: Rectangle { color: "transparent" }`（无效属性）
- **保留** `highlightFollowsCurrentItem: false` + `currentIndex: -1`（有效，可防止白底高亮残留）

## 最终验证

| 项目 | 结果 |
|------|------|
| Pre-fix QML 加载 | ❌ **Error** — `Cannot assign to non-existent property "background"` |
| Post-fix QML 加载 | ✅ **Ready** — 无错误 |
| 单元测试 232/232 | ✅ 全部通过 |
| 像素验证（白色占比） | 0.78%（仅文字/控件，无白块背景）|