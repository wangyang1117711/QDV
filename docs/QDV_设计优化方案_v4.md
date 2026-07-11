# QDV 设计优化方案 v4.0

> **版本**: v4.0  
> **日期**: 2026-07-04  
> **范围**: 全项目 UI/UX 视觉与交互优化  
> **原则**: 保留现有 Design Token 体系，渐进式增强，不引入破坏性变更  

---

## 一、项目设计现状评估

### 1.1 已有成果（基础扎实）

| 领域 | 现状 | 评价 |
|------|------|------|
| Design Token 体系 | `DesignTokens.qml` v3.0.0，完整的色阶/字号/间距/圆角体系，WCAG AA 对比度验证通过 | 优秀 |
| QML EditView | 三栏布局（算子库/画布/详情），支持缩放/拖拽/连线/撤销重做/小地图 | 良好 |
| 暗色主题 QSS | 全局 QWidget 暗色覆盖，按钮/表格/输入框/滚动条/菜单均有定义 | 良好 |
| 参数表单 | `ParamForm.qml` 支持 7 种类型（Int/Float/Enum/Bool/String/ROI/Vector），含文件路径浏览器 | 良好 |
| 响应式断点 | ≥1200 三栏 / 900-1199 两栏 / <900 单栏 | 良好 |
| 浮动面板 | `FloatingPanel.qml` 支持拖拽/靠边自动隐藏/固定 | 良好 |
| 图像预览 | `ImagePreviewWindow.qml` 独立窗口，支持最小化/最大化/透明度调节 | 良好 |

### 1.2 核心问题识别

经过对全部 QML、QSS、C++ UI 源码和交互评估报告的分析，识别出以下 **5 大问题域**：

---

## 二、问题域 P1 — QWidget 视图与 QML 视图风格割裂

### 2.1 问题描述

项目中存在 **两套并行的视觉体系**：

- **QML EditView**: 使用 `DesignTokens.qml`（紫色调 `#7C4DFF`，背景 `#121212`）
- **QWidget 视图**（LoginView、CentralWindow、CameraView、IOView 等）: 使用 `dark_theme.qss`（偏红紫色 `#660874`，背景 `#1E1E1E`）

两套体系的**背景色阶不同**（`#121212` vs `#1E1E1E`）、**强调色不同**（`#7C4DFF` vs `#660874`），导致用户在 EditView 与其他视图之间切换时产生**明显的视觉跳跃**。

### 2.2 影响范围

- `LoginView.cpp` — 登录页背景 `#252525`，强调色 `#660874`
- `CentralWindow.cpp` — 导航栏背景 `#252525`，品牌色 `#660874`
- `dark_theme.qss` — 全局 QWidget 背景 `#1E1E1E`
- `DesignTokens.qml` — QML 背景 `#121212`，强调色 `#7C4DFF`

### 2.3 优化方案

**方案 A（推荐）**: 统一到 QML Design Token 色值

修改 `dark_theme.qss` 使其与 `DesignTokens.qml` 对齐：

| Token | DesignTokens 值 | 当前 QSS 值 | 修改后 QSS 值 |
|-------|-----------------|-------------|---------------|
| `bgCanvas` | `#121212` | — | —（仅 QML） |
| `bgPanel` | `#1A1A1A` | — | —（仅 QML） |
| QWidget 背景 | — | `#1E1E1E` | `#1E1E1E`（保持，作为全局基色） |
| QSS GroupBox 边框 | — | `#444` | `#3D3D3D`（`borderDefault`） |
| QSS 按钮 hover | — | `#555` | `#2F2F2F`（`bgHover`） |
| QSS 按钮 pressed | — | `#660874` | `#7C4DFF`（`accentPrimary`） |
| QSS 选中态 | — | `#660874` | `#7C4DFF`（`accentPrimary`） |
| QSS 进度条 | — | `#660874` | `#7C4DFF`（`accentPrimary`） |
| QSS 菜单选中 | — | `#660874` | `#7C4DFF`（`accentPrimary`） |

同步修改 C++ 视图的硬编码色值：
- `LoginView.cpp`: `#660874` → `#7C4DFF`
- `CentralWindow.cpp`: `#660874` → `#7C4DFF`，`#252525` → `#1A1A1A`

**预期效果**: 全项目视觉一致性从 ~60% 提升到 95%。

### 2.4 优先级: 高  
### 2.5 工作量: 0.5 天

---

## 三、问题域 P2 — CentralWindow 导航栏设计粗糙

### 3.1 问题描述

CentralWindow 是用户进入主界面后的**第一个接触点**，但其导航栏设计存在以下问题：

1. **纯文字按钮**：导航项（首页/相机/方案/编辑/IO/通信/监控/训练）全部为纯文本，无图标
2. **72px 高度过大**：占用了宝贵的垂直空间
3. **选中态不明显**：纯色背景切换在深色主题下对比度不足
4. **无状态指示**：用户不清楚当前处于哪个功能模块
5. **"QD" 品牌标识过于简陋**：两个字母 + 紫色，缺乏专业感

### 3.2 优化方案

**导航栏重设计**：

| 要素 | 当前 | 优化后 |
|------|------|--------|
| 高度 | 72px | 48px |
| 按钮样式 | 纯文字 | 图标 + 文字（横向排列） |
| 选中态 | 不明显 | `accentPrimary` 底部 2px 指示条 + 文字高亮 |
| 品牌区 | "QD" 文字 | SVG Logo + "奇测视觉" 品牌文字 |
| hover 态 | 无 | `bgHover` 背景 + 文字色变化 |
| 间距 | 4px | 8px，按钮增加左右 padding 至 12px |

**图标方案**: 使用 Unicode 符号或内嵌 SVG（无需外部图标库依赖）：

| 导航项 | 图标符号 |
|--------|---------|
| 首页 | ⌂ (U+2302) |
| 相机 | ◉ (U+25C9) |
| 方案 | ⊞ (U+229E) |
| 编辑 | ✎ (U+270E) |
| IO | ⇄ (U+21C4) |
| 通信 | ⟡ (U+27E1) |
| 监控 | ⚡ (U+26A1) |
| 训练 | ▶ (U+25B6) |

### 3.3 优先级: 高  
### 3.4 工作量: 1 天

---

## 四、问题域 P3 — LoginView 缺少专业感

### 4.1 问题描述

登录页是**品牌第一印象**，当前设计问题：

1. **品牌标识**：仅有 "QD" 紫色文字 + "奇测科技" 纯文字，缺少 Logo
2. **错误反馈**：登录失败时无内联错误提示（交互评估报告 UI-001，高优先级）
3. **加载状态**：登录请求期间无 loading 指示器
4. **视觉层次**：整体扁平，缺少微妙的深度感（阴影、渐变）
5. **"记住我" 样式粗糙**：原生 QCheckBox 在暗色主题下对比度不足

### 4.2 优化方案

| 改进项 | 方案 |
|--------|------|
| 品牌区 | 增加 SVG Logo（眼睛/镜头图形），与紫色渐变背景 |
| 错误提示 | 密码框下方增加红色错误消息 Label，`accentError` 色值 |
| Loading 态 | 登录按钮文字替换为 "登录中..." + 禁用按钮 + 进度指示 |
| 卡片深度 | `loginCard` 增加 `box-shadow` 效果（QSS 用多重 border 模拟） |
| 输入焦点 | 统一使用 `borderFocus`（`accentPrimary`）2px 边框 |
| Enter 键 | 密码框 `returnPressed` → 触发登录 |
| 记住我 | 自定义样式 CheckBox：选中态 `accentPrimary` 填充 |

### 4.3 优先级: 高  
### 4.4 工作量: 1 天

---

## 五、问题域 P4 — EditView 画布交互体验不足

### 5.1 问题描述

画布是核心工作区，但以下交互细节需要优化：

1. **节点卡片视觉单调**：仅有中文名 + 英文类型名，无图标/缩略图
2. **连线缺少动画**：创建连线时无动画过渡，显得生硬
3. **拖拽无弹性动画**：节点从工具库添加到画布时无落入动画（设计文档 M5 提到但未实现）
4. **无框选功能**：设计文档提到 "框选多选" 但 `selectedNodeIds` 未用于实际框选逻辑
5. **网格背景过于简单**：纯 Canvas 线条网格，无点阵模式切换
6. **小地图功能有限**：不显示连线，不显示视口框，无法点击导航
7. **无对齐辅助线**：节点拖动时无智能对齐/吸附

### 5.2 优化方案

**5.2.1 节点卡片增强**

```
当前:  [中文名]         [类型名]
优化:  [分类色块] [中文名]  [端口图标]
       [类型名 · 参数数]
```

- 分类色块从 16x16 调整为圆角矩形 + 分类首字母
- 节点高度从 72px 增加到 80px（更舒展）
- 选中态增加微妙的 `accentPrimary` 发光效果（`opacity: 0.1` 外阴影）

**5.2.2 连线动画**

- 连线创建时：`NumberAnimation { duration: 300; easing.type: Easing.OutCubic }` 从 0 到完整路径
- 连线删除时：反向淡出 200ms
- 连线 hover 态：线宽 2→3 + 发光

**5.2.3 弹性落入动画**

节点添加到画布时：
```qml
ScaleAnimator { from: 0.5; to: 1.0; duration: 300; easing.type: Easing.OutBack }
OpacityAnimator { from: 0; to: 1.0; duration: 200 }
```

**5.2.4 框选多选**

在画布空白区域按下左键拖拽 → 绘制半透明矩形选区 → 释放时选中区域内所有节点 → 更新 `selectedNodeIds`

**5.2.5 小地图增强**

- 绘制连线（半透明紫色线条）
- 显示当前视口矩形框
- 点击小地图区域 → 画布平移到对应位置

### 5.3 优先级: 中  
### 5.4 工作量: 2-3 天

---

## 六、问题域 P5 — 整体 UX 反馈与引导缺失

### 6.1 问题描述

交互评估报告指出的多个问题仍未完全解决：

1. **空状态引导不够醒目**：虽然 EditView 画布有空状态提示，但其他视图（CameraView、IOView、CommView、MonitorView）无引导
2. **操作反馈延迟**：参数修改后无即时视觉确认（如参数值闪烁高亮）
3. **Toast 位置和样式**：当前 Toast 在顶部居中，可能遮挡工具栏
4. **无 Onboarding/新手引导**：新用户首次使用无步骤式引导
5. **键盘快捷键提示不足**：虽然支持 Ctrl+K/Ctrl+Z/Ctrl+Y 等，但入口不明显

### 6.2 优化方案

**6.2.1 各视图空状态模板**

为所有视图统一设计空状态组件：

```
┌─────────────────────────────┐
│                             │
│        [功能图标 64px]       │
│      "XX 功能"              │
│   "简要描述 + 首次操作引导"  │
│      [CTA 按钮]             │
│                             │
└─────────────────────────────┘
```

**6.2.2 参数修改高亮**

参数值变化时，对应的 Label 执行 500ms 颜色闪烁：
```
#FFFFFF → accentSuccess (#69F0AE) → #FFFFFF
```

**6.2.3 Toast 位置调整**

从顶部居中 → 右上角（与通知区域常规位置一致），增加进入/退出滑动动画。

**6.2.4 快捷键面板**

按 `?` 键（或 Ctrl+/）弹出快捷键速查浮层：
- 列出所有可用快捷键
- 分类显示（全局/画布/编辑器）
- 搜索过滤

### 6.3 优先级: 中  
### 6.4 工作量: 2 天

---

## 七、问题域 P6 — Design Token 体系可扩展性

### 7.1 问题描述

当前 Design Token 体系虽然完善，但存在以下扩展性问题：

1. **无亮色主题 Token**：全部为暗色硬编码值，无法切换到亮色模式
2. **缺少 Motion Token**：动画时长/缓动曲线散落在各 QML 文件中
3. **缺少 Shadow Token**：阴影效果未统一管理
4. **缺少 Spacing Scale**：虽然有空格 Token（space1-8），但组件内边距未统一使用

### 7.2 优化方案

在 `DesignTokens.qml` 中新增：

```qml
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

// ============ Component Spacing ============
readonly property int cardPadding: 16
readonly property int listPadding: 8
readonly property int formLabelWidth: 120
readonly property int inputHeight: 32
```

### 7.3 优先级: 低  
### 7.4 工作量: 0.5 天

---

## 八、问题域 P7 — 可访问性（Accessibility）补充

### 8.1 问题描述

虽然已添加了部分 `Accessible` 属性，但以下区域缺失：

1. **ParamForm 输入控件**：部分缺少 `Accessible.description`（帮助文本）
2. **画布节点**：缺少 `Accessible.role` 和操作说明
3. **颜色编码依赖**：某些状态仅通过颜色传达（如置信度条），色盲用户可能无法区分
4. **焦点顺序**：Tab 键焦点顺序未经过优化

### 8.2 优化方案

1. 为所有 ParamForm 参数增加 `Accessible.description` = `spec.help`
2. 为画布节点增加 `Accessible.role: Accessible.Button` + `Accessible.description: "算子节点: {cnName}"`
3. 置信度条增加文字百分比标签（已有，确保始终可见）
4. 为 QML 组件定义 `KeyNavigation.tab` 属性，确保逻辑焦点顺序

### 8.3 优先级: 低  
### 8.4 工作量: 1 天

---

## 九、执行路线图

### Phase 1: 视觉一致性（1.5 天）— 立即执行

| 任务 | 对应问题域 | 交付物 |
|------|-----------|--------|
| 统一 QSS 与 Design Token 色值 | P1 | 修改 `dark_theme.qss` |
| 统一 C++ 硬编码色值 | P1 | 修改 `LoginView.cpp`、`CentralWindow.cpp` |
| 新增 Motion/Shadow Token | P6 | 扩展 `DesignTokens.qml` |

### Phase 2: 核心交互增强（3 天）— 1 周内

| 任务 | 对应问题域 | 交付物 |
|------|-----------|--------|
| CentralWindow 导航栏重设计 | P2 | 修改 `CentralWindow.cpp` |
| LoginView 体验增强 | P3 | 修改 `LoginView.cpp` |
| 节点卡片视觉增强 | P4 | 修改 `Main.qml` 节点 delegate |
| 弹性落入动画 | P4 | 修改 `Main.qml` 节点添加逻辑 |
| 连线动画 | P4 | 修改 `Main.qml` 连线 Canvas |

### Phase 3: UX 反馈完善（2 天）— 2 周内

| 任务 | 对应问题域 | 交付物 |
|------|-----------|--------|
| Toast 位置/动画优化 | P5 | 修改 `Main.qml` Toast Popup |
| 参数修改高亮 | P5 | 修改 `ParamForm.qml` |
| 各视图空状态模板 | P5 | 新增 QML 空状态组件 |
| 快捷键面板 | P5 | 新增 `ShortcutsPanel.qml` |

### Phase 4: 画布高级交互（2-3 天）— 3 周内

| 任务 | 对应问题域 | 交付物 |
|------|-----------|--------|
| 框选多选 | P4 | 修改 `Main.qml` 画布 MouseArea |
| 小地图增强 | P4 | 修改 `Main.qml` minimap Canvas |
| 网格模式切换 | P4 | 修改 `Main.qml` 画布网格 |
| 对齐辅助线 | P4 | 新增 `AlignmentGuide.qml` |

### Phase 5: 可访问性与打磨（1 天）— 持续

| 任务 | 对应问题域 | 交付物 |
|------|-----------|--------|
| 可访问性补充 | P7 | 所有 QML 文件增加 Accessible 属性 |
| 焦点顺序优化 | P7 | 增加 KeyNavigation |

---

## 十、成功标准

### 10.1 视觉一致性
- [ ] QSS 强调色与 DesignTokens `accentPrimary` 完全一致
- [ ] 所有 C++ 视图的硬编码色值替换为 Token 等价值
- [ ] 切换视图时无视觉跳跃感

### 10.2 交互完整性
- [ ] 登录失败时显示内联错误消息
- [ ] CentralWindow 导航栏带图标和选中指示条
- [ ] 节点添加到画布有弹性动画
- [ ] 连线创建有过渡动画
- [ ] 框选多选功能可用
- [ ] Toast 通知在右上角显示并有进出动画

### 10.3 零破坏性
- [ ] AI 模块代码零修改（`git diff` 验证）
- [ ] 现有功能全部正常（回归测试通过）
- [ ] Design Token 体系向后兼容（新增属性，不修改已有属性值）

---

## 十一、风险评估

| 风险 | 影响 | 概率 | 缓解措施 |
|------|------|------|---------|
| QSS 色值修改导致其他 QWidget 视图样式异常 | 中 | 低 | 修改后逐一检查所有 QWidget 视图截图 |
| C++ 硬编码色值遗漏 | 低 | 中 | 用 `grep -r "#660874"` 全面搜索 |
| 节点动画性能下降（>50 节点时） | 中 | 中 | 节点数 >50 时禁用 ScaleAnimator |
| 框选功能与节点拖拽冲突 | 中 | 中 | 空白区域按下 = 框选；节点区域按下 = 拖拽 |
| 浮动面板动画与 Qt 窗口管理器冲突 | 低 | 低 | 仅对 QML 内部 Rectangle 做动画，不动 Window |

---

## 十二、总结

QDV 项目的设计基础设施（Design Token、QML 组件体系、响应式断点）已经相当成熟。主要优化方向集中在 **3 个层面**：

1. **视觉一致性**：消除 QWidget/QML 两套体系的色值割裂 — **投入最小，收益最大**
2. **关键触点体验**：导航栏、登录页、画布节点 — **用户每天高频接触的区域**
3. **交互细节**：动画、反馈、引导 — **从"可用"到"好用"的质变**

建议按 Phase 1 → Phase 2 顺序优先执行，Phase 3-5 根据实际资源排期。
