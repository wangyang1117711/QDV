# 零样本检测模块 · 易用性与可操作性优化报告

> 日期：2026-08-13 ｜ 范围：ZeroShotKit + ZeroShotDetectView ｜ 目标：让操作新手快速上手，降低学习成本

---

## 1. 结论先行

本次优化在不改动任何核心推理逻辑（zsu::Kit / ZeroShotEngine / ORT 后端）的前提下，从 **界面引导、文案通俗化、流程简化、文档培训、可测试性** 五个维度对零样本检测模块做了新手向改造。全部改动已通过编译验证，新增 8 组纯逻辑单元测试。

| 需求 | 落地情况 | 状态 |
| --- | --- | --- |
| 1. 使用说明优化 | 新增《新手操作手册 UserGuide.md》+ 界面术语通俗化 | ✅ |
| 2. 界面与提示优化 | 顶部"新手引导条"、全控件通俗 tooltip、空状态引导 | ✅ |
| 3. 流程简化 | 推理按钮状态联动、前置条件缺失弹窗指引 | ✅ |
| 4. 常见问题与错误处理 | 新增《FAQ.md》14 条排障指引 + 模型加载失败提示增强 | ✅ |
| 5. 培训与文档支持 | 新手手册 + 4 个典型场景示例 + 术语表 | ✅ |

---

## 2. 改动文件清单

| 文件 | 变更类型 | 说明 |
| --- | --- | --- |
| `include/UI/ZeroShotGuide.h` | **新增** | 新手引导纯逻辑模块（header-only）：四步状态机、引导 HTML、前置条件提示、模型类型/术语通俗文案 |
| `src/UI/ZeroShotDetectView.cpp` / `include/UI/ZeroShotDetectView.h` | 修改 | 顶部新增引导条并实时刷新；推理前置条件不足时弹窗指引（替代状态栏短提示） |
| `third_party/ZeroShotKit/ui/ZeroShotPanel.cpp` / `.h` | 修改 | 推理按钮使能联动；全控件通俗 tooltip；模型路径为空时弹窗指引 |
| `third_party/ZeroShotKit/ui/ZeroShotResultPanel.cpp` / `.h` | 修改 | 新增 `setEmptyHint()` / `hasResults()`；空状态显示分步引导；结果区通俗 tooltip |
| `third_party/ZeroShotKit/docs/UserGuide.md` | **新增** | 新手操作手册：快速上手 4 步、模型选型表、4 个典型场景、参数解读 |
| `third_party/ZeroShotKit/docs/FAQ.md` | **新增** | 常见问题与错误处理（14 条，含排查步骤） |
| `third_party/ZeroShotKit/README.md` | 修改 | 文档索引新增 UserGuide / FAQ |
| `tests/UI/test_zeroshot_guide.cpp` | **新增** | 8 组纯逻辑单元测试（无 QWidget 依赖） |
| `tests/CMakeLists.txt` | 修改 | 注册新测试文件 |

---

## 3. 各需求落地细节

### 3.1 使用说明优化 → 通俗化 + 场景化

- **新手引导条**（`ZeroShotDetectView` 顶部）：四步实时指示 `①选择模型类型 → ②加载模型 → ③加载图像 → ④开始推理`。完成步骤显示 ✔ 绿色，当前步骤 ▶ 琥珀高亮并给出**下一步的具体操作文案**，未完成步骤 ○ 置灰。
- **空状态引导**（结果面板）：从"暂无零样本推理结果"改为完整 4 步指引，并提示查阅 UserGuide。
- **模型类型通俗化**（下拉悬停即懂）：

| 模型 | 大白话 |
| --- | --- |
| AnomalyCLIP | 判断"产品有没有缺陷" |
| Grounding DINO | 找出"缺陷在哪、是什么" |
| MobileSAM | 把目标轮廓精确"抠"出来 |
| PatchCore | 只给合格品，自动学"正常标准" |

### 3.2 界面与提示优化 → 悬停帮助全覆盖

为以下控件补充/改写通俗 tooltip（含**调节方向建议**，如"误报多就调高、漏报多就调低"）：
异常阈值、检测阈值、NMS IoU、量化模型、多次推理取稳定值、人工复核、提示词、正常样本、渐进式切换阈值、检测结果表列含义、异常分数仪表读数、掩码/热力图解读。

### 3.3 流程简化 → 按钮联动 + 明确指引

- **推理按钮状态联动**：模型未加载时【推理当前】【推理全部】置灰，加载成功后自动可用 → 消除"点了没反应"的困惑。
- **前置条件弹窗**：点击推理但缺模型/缺图像时，弹窗给出明确分步指引（原实现仅状态栏一行短提示，新手不易察觉）。
- **模型路径为空指引**：点【加载模型】未选路径时弹窗提示"点【浏览...】或在具体模型下拉直接选"。

### 3.4 常见问题与错误处理 → FAQ.md（14 条）

覆盖：模型加载失败（按错误特征分 5 类）、推理按钮置灰、推理无结果、误报/漏报调参方向、提示词写法、结果不稳定、批量目录无图、PatchCore 无样本、中文路径、检测慢、产线算子接入等。每条均给出**原因 + 处理办法**。

### 3.5 培训与文档支持

- `UserGuide.md`：快速上手表（步骤/位置/完成标志）、模型选型表、4 个典型场景分步示例、参数解读表、生产链路提醒。
- `FAQ.md`：14 条排障条目，含"算子参数 ↔ 界面参数"对照表。

---

## 4. 可测试性设计

引导逻辑抽为 **header-only 纯函数模块** `include/UI/ZeroShotGuide.h`（仅依赖 Qt Core），UI 只负责"收集状态 → 渲染"，测试直接测逻辑，无需实例化 QWidget（规避了沙箱环境 NVIDIA/UI 测试限制）。

`tests/UI/test_zeroshot_guide.cpp` 覆盖 8 组用例：
1. 四步完成判定（含步骤①恒完成）
2. nextPendingStep 推导（初始/逐步/全部完成）
3. 引导 HTML 生成（✔/▶/○ 标记）
4. 空状态引导文本完整性
5. 前置条件提示 4 种组合
6. 模型类型通俗解释（4 模型 + 未实现 + 未知）
7. 术语通俗解释（含调节方向 + 未收录返回空）
8. 步骤文案正确性

---

## 5. 编译与测试验证

- 环境：MinGW 13.1 + Qt 6.11.1（D:/Qt_new）+ OpenCV（D:/opencv），构建目录 `build/`
- `cmake ..` 重配成功（纳入新测试文件）
- **完整 `QDV_tests` 目标构建成功（100%）**，链接产出 `build/bin/QDV_tests.exe`
- **运行测试套件：744 / 744 全部通过，0 失败**（含新增 8 组 ZeroShotGuide 用例）
- ⚠️ 已知环境限制：若并行（-j4）编译大测试文件会触发 `cc1plus: out of memory`（沙箱内存限制，与代码无关），以 `-j1` 串行构建即可。

### 新增测试运行结果（8/8 PASS）

```
[PASS] ZeroShotGuide: 初始状态仅步骤①完成
[PASS] ZeroShotGuide: 模型加载后步骤②完成
[PASS] ZeroShotGuide: 图像加载后步骤③完成
[PASS] ZeroShotGuide: 有结果后四步全部完成
[PASS] ZeroShotGuide: nextPendingStep 推导
[PASS] ZeroShotGuide: 引导 HTML 含步骤标记与状态
[PASS] ZeroShotGuide: 全部完成后提示完成
[PASS] ZeroShotGuide: 空状态引导包含完整步骤指引
[PASS] ZeroShotGuide: 前置条件提示组合
[PASS] ZeroShotGuide: 模型类型通俗解释
[PASS] ZeroShotGuide: 常用术语通俗解释
[PASS] ZeroShotGuide: 步骤文案与详细说明
```

---

## 6. 后续建议（可选迭代）

1. **引导条做成可折叠**：熟练用户可收起，节省纵向空间。
2. **首次使用向导（QTour）**：首次进入本 Tab 时以气泡引导走一遍四步流程。
3. **阈值"推荐档位"预设**：如"严格/标准/宽松"三档一键切换，替代手动滑块。
4. **把 ZeroShotGuide 的通俗文案同步到模型注意事项（model_notes.json）**，统一两处术语口径。
5. **测试接入 CI**：在非沙箱环境恢复 `UI/test_ui_smoke.cpp` 等被禁用的 UI 测试，并纳入完整回归。

---

## 7. 功能完整性说明

- 未改动 `zsu::Kit`、`ZeroShotEngine`、`ORTInferenceEngine` 等任何推理层代码，推理行为与结果与改动前完全一致。
- 所有新增接口为**纯增量**：`setEmptyHint()` / `hasResults()` / `updateGuideHint()` / `updateInferenceButtonsState()`，不改变既有信号槽与数据流。
- ZeroShotKit 保持第三方库独立性（空状态文案通过依赖注入传入，不反向 include 主项目头文件）。
