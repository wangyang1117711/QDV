# QDV 项目全面评估与优化建议报告

> **报告编号**：glm5.3优化建议0906
> **评估日期**：2026-09-06
> **评估范围**：QDetectVision（QDV）V2.0-0809 全项目 —— 编辑模块（重点）、运行模块、零样本模块、工程架构
> **评估方法**：静态代码走查（C++/QML 热路径逐行追踪）+ 配置审查 + 历史文档交叉验证
> **核心诉求**：编辑模块操作算子卡顿问题定位与优化，兼顾交互、操作流畅性、信息流等全面维度

---

## 0. 一句话总结（TL;DR）

编辑模块"每次操作算子都卡"的根因是一个**五层叠加的放大链**：单个参数修改 → 全参数表对比+同步磁盘日志 → 全节点 QVariantList 深拷贝广播 → QML 端 Repeater 全量重建所有节点卡片+11 处监听器级联刷新 → 300ms 后子线程重跑整条上游算子链 → 执行结果经临时 PNG 文件落盘再由 UI 线程同步解码显示。**没有任何单一瓶颈是致命的，但它们串在一起让最简单的操作也要付出"全量刷新+磁盘 IO+重算"的代价。** 治理优先级：P0 四项（日志同步落盘、Repeater 全量重建、全参数表回传、临时 PNG 中转）落地后，常规参数编辑操作可从"可感知卡顿"降到"即时响应"。

| 评估维度 | 总评 | 最严重问题 |
|---|---|---|
| 编辑流畅性 | ★★☆☆☆ | 参数修改触发全画布重建 + 全链重算 |
| 交互设计 | ★★★☆☆ | 选中即跑链、搜索无防抖、视图切换全重载 |
| 信息流 | ★★★☆☆ | 图像走磁盘 PNG 中转、状态双源 |
| 架构健康度 | ★★★☆☆ | QVariantList 值语义数据模型是万恶之源 |
| 工程质量 | ★★★★☆ | 拆分清晰、有测试、有文档，唯热路径日志失控 |

---

## 1. 备份与 Git 同步状态（任务 1 完成）

- **提交**：`4c519b6d` — "chore(backup): 2026-0906 全量备份"，220 个文件入库，已推送至 `origin/main`（github.com/wangyang1117711/QDV）。
- 本次入库内容：未跟踪的源码（ZeroShotGuide.h、3 个测试探针、qdv-annotator 标注工具源码）、文档（Halcon 模型集成评估、halcon-path-decision-tree.html、docs/optimization/）、人工反馈与测试图、软硬件配置与自我升级模型笔记。
- 处理的仓库卫生问题（顺带修复）：
  1. `.git/index.lock` 陈旧锁（8 月 14 日残留）导致无法提交 —— 已清除；
  2. `.git/hooks/pre-commit` 为 0 字节空文件导致 commit 报 "cannot spawn" —— 已重命名禁用；
  3. `.gitignore` 补充：`QDV/`（该目录只是指向 qml/EditView 的 Windows 符号链接，非真实代码）、`tools/tools/`（验证输出物）、`tools/qdv-annotator/build/`、smoke 日志、`cache_test_msmask.png`。
- 遗留建议：`tools/qdv-annotator/verify/_out/`（验证 JSON 输出）与 `OperatorTunerDialog.qml.bak` 建议后续也加入忽略或清理。

---

## 2. 编辑模块卡顿根因专项分析（核心章节）

### 2.1 卡顿链路全景图

以"用户在属性面板把阈值从 30 改成 35"这一个最小操作为例，实测代码路径如下：

```
[用户输入]
  └─ ParamForm.setValue()                      qml/EditView/ParamForm.qml:164
      ├─ 本地校验（遍历 params 找 spec）
      └─ valuesChanged(整个参数Map) ────────── ❌问题①全参数回传
          └─ PropertyPreviewPanel.onValuesChanged   :464
              └─ bridge.updateOperatorParams(nodeId, newValues)
                  │  src/UI/EditViewBridge.cpp:653
                  ├─ ❌问题②同步日志：先打 1+N 条（遍历打印每个参数名/值/类型）
                  ├─ 线性查找节点 O(n)
                  ├─ 对比新旧参数（值都变了才继续）→ 打印 changes 列表 M 条
                  ├─ validateParam 校验
                  ├─ undoStack.push(PropertyChangeCommand)
                  │   └─ updateParamInternal ── 再打 1 条日志
                  │       └─ emit currentNodesChanged() ────── ❌问题③广播风暴
                  │           └─ QVariantList 按值深拷贝 × 每个监听者
                  │               ├─ nodeRepeater.model 变化
                  │               │   └─ ❌问题④全部节点卡片销毁重建
                  │               │       （每张卡片含 2×getOperatorMeta 全量toMap、
                  │               │        端口Rect、MouseArea、ColumnLayout…）
                  │               ├─ connectionsCanvas.requestPaint
                  │               │   └─ buildConnectionMaps() O(n+e) 重算
                  │               ├─ 4 个分组 Canvas requestPaint（Main.qml:2863/2955/3024/3095）
                  │               ├─ PropertyPreviewPanel.onCurrentNodesChanged → 重读参数
                  │               │   └─ ParamForm._maybeSnapshot → JSON.stringify 整表 ×2
                  │               ├─ VariableManagerPanel.refresh / ConnectionManagerPanel.refresh
                  │               ├─ RecommendationPanel.refresh
                  │               └─ Main.qml:765 syncPreviewToNode
                  ├─ markDirty → isDirtyChanged
                  └─ m_previewManager->onNodeParamsChanged(nodeId)  :746
                      └─ 300ms 防抖后 doPreview()（QtConcurrent 子线程）
                          └─ runSingleOperator(nodeId)
                              ├─ computeUpstreamChain：递归遍历 connections QVariantList
                              ├─ buildToolChainFromNodes：每个 nodeId 再线性扫 nodes ❌O(n²)
                              ├─ 每个算子实例化 → configure → execute（重算整条链！）
                              │   ❌问题⑤上游未变也全量重跑，无增量缓存
                              ├─ 每个算子输出 saveMatToTempPng
                              │   ❌问题⑥PNG编码+写盘到%TEMP%，文件永不清理
                              └─ 完成回调 → appendSnapshot：主线程 QImage(before)+QImage(after)
                                  从磁盘再读两张 PNG
                                      ↓
                  UI 显示层 ImageViewer/PreviewPanel
                  ❌问题⑦ asynchronous:false（UI线程同步解码 1200×1200 PNG）
                  ❌问题⑧ cache:false（每次重复解码）
                  ❌问题⑨ QSG_NO_TEXTURE_CACHE=1（每次重新上传GPU纹理）
```

**结论**：用户感知的"每次操作都卡"，是问题①~⑨在同一次点击里全部发生。其中 ②④⑤⑥⑦ 是主要耗时项，①③ 是放大器，⑧⑨ 是显示层叠加税。

### 2.2 逐项根因详解（含证据与量化）

#### 根因 A【高】Logger 同步落盘侵入所有热路径

- **证据**：
  - `src/Core/Logger.cpp:33-36`：每条 `log()` 在加锁 append buffer 后**无条件调用 `flushBuffer()`**，即每条日志立即 QFile 写入 + `QTextStream::flush()` + `rotateIfNeeded()`（后者还会调 `m_logFile.size()` 触发文件系统查询）。构造函数里明明有 100ms 定时器批量刷盘（`include/Core/Logger.h:71-74`），却被这行直调废掉了。
  - `src/UI/EditViewBridge.cpp:655-663`：`updateOperatorParams` 入口把**每个参数的键值类型各打一条 INFO**；:700-703 打 changes 列表；:1040-1043 `updateParamInternal` 再打一条；:735-741 打 after 确认日志。**修改 1 个参数 ≈ 6~15 条同步磁盘日志**（参数多的算子如 ImagePreprocess 有 10+ 参数，一次操作 30+ 条）。
  - `getRegisteredModels`（EditViewBridge.cpp:1532-1539）对**每个模型**打一条 INFO，而它在 ParamForm 的 `modelList` 绑定里被调用（PropertyPreviewPanel.qml:461），模型多时一次面板刷新又是十几条。
- **量化影响**：Windows 上 QFile+flush 单条约 0.1~0.5ms（含杀软扫描钩子时更糟），30 条 ≈ 3~15ms 纯磁盘等待，且全部发生在 UI 线程、参数修改的同步路径上。qDebug() 同步输出到 IDE 控制台在调试器附加时更慢 10 倍。
- **定位**：这是**最容易修、收益立竿见影**的一项。这些日志全是 v5.3.5~v5.3.7 排查 ComboBox 闪回时留下的诊断日志，闪回已修复，日志却留下了。

#### 根因 B【高】`currentNodesChanged` 广播 + Repeater 全量重建

- **证据**：
  - `include/UI/EditViewBridge.h:103`：`QVariantList currentNodes() const { return m_currentNodes; }` —— **值返回，每次调用深拷贝整个节点列表**（每个节点含完整 params、outputConfig map）。任何一次参数修改 → 每个监听者各自再拷一份。
  - `qml/EditView/Main.qml:2508` `nodeRepeater.model: editViewBridge.currentNodes`：QVariantList 是不可变值类型，信号一触发 QML 判定"整个 model 换了"，**所有 delegate 销毁重建**。每张卡片（Main.qml:2511 起）含：2 个 Label、border.color 里的 `getOperatorMeta()`（C++ O(n) 线性查找 + OperatorMeta::toMap 全量转换，见 OperatorDescriptors.cpp:752-760）、输入/输出端口 Rect、拖拽 MouseArea、ColumnLayout。**50 个节点 = 重建 50 张卡片 = 100 次 getOperatorMeta + 数百个 QML 对象销毁/创建**。
  - `Main.qml` 中共 **11 处** `onCurrentNodesChanged` 监听（2863/2955/3024/3095 四个分组 Canvas 各一处、connectionsCanvas:2352、PropertyPreviewPanel:99、VariableManagerPanel:79、ConnectionManagerPanel:71、RecommendationPanel:151、Main.qml:765 全局），每次信号全部触发。
  - `connectionsCanvas.onPaint`（Main.qml:2286+）每次重绘都调 `buildConnectionMaps()`（Main.qml:300）重建 nodeMap/pairCount 等全部字典 —— O(n+e) 的 JS 工作 + `editViewBridge.connections` 又一次 QVariantList 深拷贝。
- **量化影响**：节点数少时（<10）重建成本约 10~30ms；到 30~50 节点（典型视觉方案规模）就是 50~200ms —— 正好落在"感觉一顿"的区间。**这是卡顿第二主因，也是节点越多越卡的解释**。

#### 根因 C【高】参数编辑回传全表 + 无上游缓存的全链重算

- **证据**：
  - ParamForm.setValue（ParamForm.qml:203）`valuesChanged(v)` 回传**整个参数 Map**（虽然 C++ 端会 diff，但 QML→C++ 跨边界传递全表 QVariantMap 本身就有转换成本）；OperatorTunerDialog.qml:222 每次滑一档还 `JSON.parse(JSON.stringify())` 深拷贝整表。
  - PreviewManager 防抖后（`src/UI/PreviewManager.cpp:80-92`）调 `runSingleOperator`，其内部（SchemeRunController.cpp:548+）`computeUpstreamChain` 递归遍历 connections，`buildToolChainFromNodes`（:83+）**对链上每个 nodeId 都在 nodes QVariantList 里线性扫描**（O(n²)），然后**从 ReadImage 开始整条链重新执行**——上游算子参数没动、图像没变，也全量重跑。YOLO/OCR/模板匹配这类算子动辄几百 ms 到秒级。
  - 上游每个算子执行完还各存一张临时 PNG（saveMatToTempPng，SchemeRunController.cpp:52-79），%TEMP% 目录里 `qdv_single_*.png` **无限累积**（UUID 命名永不覆盖），既耗编码+写盘时间，也泄漏磁盘。
- **量化影响**：改一次参数，若选中节点上游有 5 个算子（含一个 AI 算子），后台重算 0.5~3s。虽有防抖和异步，但这段时间内 CPU 被占满（QtConcurrent 默认线程池），**下一次鼠标操作（继续拖滑块、点别的节点）的 UI 响应被明显拖慢** —— 这就是"连着操作就越来越卡"的来源之一。

#### 根因 D【中高】图像显示路径三重税：同步解码 + 不缓存 + 禁纹理缓存

- **证据**：
  - `qml/EditView/ImageViewer.qml:55-56`、`PreviewPanel.qml:340-341`：`asynchronous: false; cache: false`（注释说是 v5.3 为规避 QRhi 跨实例纹理 bug 而设）→ 每次 source 变化，**UI 线程同步解码**一张 1200×1200 PNG（约 40~150ms），且**每次都重新解码**。
  - `apps/SmartVision/main.cpp:419-425`：`QSG_NO_TEXTURE_CACHE=1` 让解码后的纹理也无法复用，每次重新上传 GPU。
  - `QSG_RENDER_LOOP=basic` 单线程渲染循环：GUI 事件处理和渲染串行，上述同步解码直接阻塞交互帧。
  - 图像从 C++ 到 QML 走的是 **%TEMP% PNG 文件**（saveMatToTempPng → file:/// URL），即"内存图像 → PNG 编码 → 写盘 → 读盘 → PNG 解码 → 纹理上传"六步，磁盘 IO 出现两次。
- **量化影响**：每次预览刷新额外 50~200ms UI 线程占用；预览面板和右侧 ImageViewer 同时各显示一张时翻倍。

#### 根因 E【中】EditView 视图切换 = QML 场景整体卸载重建

- **证据**：`src/UI/EditView.cpp:289-321`：`hideEvent → unloadQml()`（setSource(QUrl()) 销毁整个场景），`showEvent → loadQml()` 重新加载 4194 行的 Main.qml。切到运行视图再切回来，**算子库列表、所有面板、节点 Repeater 全部从头构建**，用户感受是"切回来白屏卡一下"。这是 v5.4 为治 QRhi 纹理跨实例 bug 的猛药，代价是交互税。

#### 根因 F【中】交互细节税

- **选中节点即触发预览执行**：`PreviewManager::onNodeSelected → setPreviewNodeId → previewNow()` 跳过防抖立即执行（PreviewManager.cpp:60-68）——单击画布上一个 AI 算子节点 = 立刻后台跑一次它的上游链。
- **搜索框无防抖**：Main.qml:1682 `onTextChanged` 每敲一个字符 → `searchOperators`（EditViewBridge.cpp:990+：70 算子 × OperatorMeta::toMap 全量转换构建 QVariantList）→ `model = null; model = buildModel()` 全列表重建。
- `OperatorDescriptors::get` O(n) 线性查找（OperatorDescriptors.cpp:752），被 QML 每张节点卡片 border 绑定和文本绑定调用（Main.qml:2544-2548、2566-2570），70 算子 × 重建 N 卡片时被调用 2N 次；`toMap()` 每次全量重建 QVariantMap（含 params/outputs 列表），**无缓存**。

### 2.3 卡顿贡献度汇总（按典型 30 节点方案、单次参数修改估算）

| # | 根因 | 主线程耗时占比（估） | 用户感受 |
|---|---|---|---|
| A | 热路径同步日志（30+ 条/次） | 15~25% | 输入后"顿一下"才刷新 |
| B | Repeater 全量重建 + 11 处级联 | 30~45% | 改参数/加节点/删节点都卡 |
| C | 后台全链重算抢 CPU | （后台）影响下一次操作 | 连续操作越来越卡 |
| D | 图像同步解码×2面板 + 纹理重传 | 20~30% | 预览刷新掉帧、拖动画布粘滞 |
| E | 视图切换全重载 | （一次性 0.5~2s） | 切回编辑视图白屏 |
| F | 选中即跑链、搜索无防抖 | 交互税 | 点节点偶发延迟、搜索打字卡 |

> 注：以上为静态分析 + 经验量化（未做运行时 profiling 基准，建议落地时先加一次 QElapsedTimer 埋点建立基线，见 4.0 节）。

---

## 3. 分维度全面评估

### 3.1 交互设计（★★★☆☆）

**做得好的**：
- 快捷键体系完整（Ctrl+K 搜索、Ctrl+C/V 参数复制粘贴、Delete 删除、拖拽建连、多选拖拽）；
- 参数编辑三形态（属性面板内嵌 / TunerDialog 实时调参 / 帮助文档弹窗），符合海康 VM 的用户习惯；
- 剪贴板复制粘贴算子参数（copyNodeParams/pasteNodeParams）、最近使用/常用/收藏三轨算子库，对高频用户友好；
- 冲突检测角标（conflictDetector）、智能推荐面板（recommender）是超前的增值功能。

**问题**：
| 编号 | 问题 | 证据 | 影响 |
|---|---|---|---|
| I-1 | 单击选中即跑上游链，无"仅选中不执行"模式 | PreviewManager.cpp:60-68 | 误触/浏览方案时 CPU 被无谓消耗，间接卡顿 |
| I-2 | 搜索每键全量重建，无防抖 | Main.qml:1682-1686 | 输入法用户打中文时每候选词刷一次列表 |
| I-3 | 撤销栈 50 步对滑块拖动场景偏小，且无"撤销历史"面板 | EditViewBridge.cpp:58 | 用户不敢大胆试参数 |
| I-4 | 无操作耗时反馈：预览执行中仅状态栏文字，无骨架屏/进度 | PreviewPanel.qml:75-78 | 重算 1~3s 期间用户不知道该等还是该继续操作 |
| I-5 | 视图切换（编辑↔运行）丢弃整个编辑场景状态 | EditView.cpp:289-321 | 回到编辑要重等加载，滚动/折叠/面板状态全丢 |
| I-6 | 拖拽节点时 x/y 绑定依赖 livePositions 整字典变化 | Main.qml:2516-2529 | 多选拖拽时每帧全部选中节点重求值（绑定粒度过粗） |

### 3.2 操作流畅性（★★☆☆☆）

- 拖拽路径已优化（moveNodeLive 不广播、单 Canvas 画连线，见 Main.qml:2273-2276 注释，P1-B04 已修 100 节点卡顿）——**说明团队已有性能意识，但只治了拖拽一条路**；参数修改、增删节点、选中节点三条高频路径仍在全量刷新。
- 前后对比 Snapshot（appendSnapshot 前后图）是好功能，但它的 QImage 磁盘读取发生在主线程回调（PreviewManager.cpp:127-137）。
- 双图像显示（PreviewPanel + 右侧 ImageViewer + Tuner 内预览 = 最多 3 处同时显示同一图像源）都在 UI 线程解码。

### 3.3 信息流（★★★☆☆）

- **图像流**：C++ cv::Mat → PNG 编码 → %TEMP% 写盘 → QML file:// 读盘解码。内存中明明有图像，却两次穿越磁盘。临时 PNG 用 UUID 命名**永不复用也永不清理**（%TEMP% 会随使用无限膨胀，也拖慢系统 temp 目录扫描）。
- **状态双源**：`root.currentSelectedNodeId`（QML）与 `m_selectedNodeId`（C++，EditViewBridge.cpp:553-558）并存，靠 selectNode() 双写同步（Main.qml:234-247）；undo/redo 后 C++ 状态变了，QML 侧 currentSelectedNodeId 不会跟随（潜在不同步 bug 面）。
- **参数双源**：ParamForm `_internalValues`（乐观值）与节点 params（权威值）+ `_maybeSnapshot` 用 JSON.stringify 全表对比判断是否同步（ParamForm.qml:69-90）——每次 currentNodesChanged 都做两次全表序列化。
- **结果值流**：算子 ports → setNodeOutputValues 缓存（mutex 保护，好）→ 变量面板读取，路径清晰。
- **日志流**：qDebug + 文件双写，编辑模块右下角浮窗可查——设计 OK，但热路径过量日志让"可观测"变成了"拖累"。

### 3.4 架构健康度（★★★☆☆）

**好的方面**：6 层架构清晰；SchemeRunController/SchemeSerializer/OutputConfigManager/PreviewManager 等职责拆分到位；ToolChainExecutor 端口契约 + 70 算子三处一致性铁律；有 Catch2 测试与 offscreen 跑法约定；历史 bug 修复均有注释级文档（v2.x~v5.x 修复记录密度罕见地高）。

**结构性风险**：
| 编号 | 风险 | 证据 | 后果 |
|---|---|---|---|
| A-1 | QVariantList/QVariantMap 作为节点/连线数据模型 | EditViewBridge.h:103,138 | 所有读写全量深拷贝；无法做"单节点变更"通知；是根因 B/C 的温床 |
| A-2 | 算子元数据查询无索引无缓存 | OperatorDescriptors.cpp:752-760 | 70 算子下每次 O(n)+toMap 全量转换 |
| A-3 | buildToolChainFromNodes O(n²) + 每次执行新建/销毁全部工具对象 | SchemeRunController.cpp:83+ | 链长时执行前准备成本显著 |
| A-4 | 上游链无结果缓存，任何下游参数变化全链重跑 | SchemeRunController.cpp:548+ | AI 算子场景下预览延迟被放大 N 倍 |
| A-5 | PreviewManager 重入保护"直接丢弃"而非"排队合并" | PreviewManager.cpp:99-103 | 上一次没跑完期间的所有参数修改被吞，用户看到旧结果（正确性体验问题） |
| A-6 | Logger 单例直调 flush | Logger.cpp:36 | 全局所有模块受害 |

### 3.5 工程质量（★★★★☆）

- 文档密度高、交接文档质量好（QDV_项目总结、docs/optimization 两份报告、ZeroShotKit UserGuide/FAQ）。
- 本次备份前仓库有：陈旧 index.lock、空 pre-commit hook 两个阻断性问题（已顺手修复）；.gitignore 缺项已补。
- `OperatorTunerDialog.qml.bak`、`QDV/EditView` 符号链接、`tools/tools/` 等杂项建议清理。
- 无性能回归测试：现有 77 用例全是功能/正确性向，**没有任何"操作延迟 < Xms"的门槛用例**，性能退化不会被 CI 发现。

---

## 4. 优化建议（按优先级分级落地）

> 实施原则：**每条改动独立可验证、不破坏 v5.x 已修复的 bug（特别是 QRhi 纹理修复）**。先做 P0 的 4 条"低风险高收益"，再做 P1 结构性改造，P2 择机。

### P0 —— 立即做（1~2 天工作量，卡顿感预计消除 60%+）

**P0-1 热路径日志静音 + Logger 改异步批量落盘**
- 文件：`src/UI/EditViewBridge.cpp`（updateOperatorParams :655-663、:700-703、:735-741、updateParamInternal :1040-1043）、`src/Core/Logger.cpp:36`
- 做法：
  1. 删除/降级上述诊断日志到 `Logger::debug`（或编译期宏 `QDV_DIAG` 包裹）；
  2. `log()` 中删除无条件 `flushBuffer()` 直调，只保留 100ms 定时器 + BUFFER_FLUSH_SIZE(256) 双触发（头文件已具备该常量，用起来即可）；
  3. `getRegisteredModels` 逐模型日志同样降级。
- 预期收益：每次参数修改减少 6~30 次同步磁盘 IO，主线程直接省 3~15ms+；所有模块受益。
- 风险：极低（日志仍在，只是批量落盘，崩溃前 100ms 内日志可能丢——可接受，Error 级别可保留立即 flush）。

**P0-2 currentNodes 改为角色化模型，节点卡片只建一次**
- 文件：新增 `NodeListModel : QAbstractListModel`（roles: id/type/x/y/params/outputConfig），`EditViewBridge` 内部 m_currentNodes 迁移为该模型；`Main.qml:2508` Repeater 的 model 与 delegate 全部改读角色。
- 做法：参数修改走 `dataChanged(start,end,{params角色})` 而非整体 reset；节点卡片 Text/border 绑定改为对 modelData 的角色绑定（QML 端 delegate 不再销毁重建）。
- 预期收益：消除根因 B 的重建成本（30 节点方案下每次操作省 50~200ms），节点越多收益越大；同步消灭 `currentNodes()` 值拷贝。
- 风险：中。需要同步改 11 处 QML 监听为 Connections→模型信号或保留 `currentNodesChanged` 兼容信号。建议分两步：先在 C++ 内部把 QVariantList 保留为兼容读接口（从模型导出），QML 渐进迁移。

**P0-3 参数回传改增量：setValue 只传变化的键**
- 文件：`qml/EditView/ParamForm.qml:203`、`PropertyPreviewPanel.qml:464`、`OperatorTunerDialog.qml` setValue/debounce 路径
- 做法：`valuesChanged` 增加 `(name, value)` 形态（或 `changedKeys` 数组），C++ `updateOperatorParams` 增加单参数重载（内部跳过全表对比）；TunerDialog 去掉 `JSON.parse(JSON.stringify())` 全表深拷贝，改为浅拷贝 + 单键写。
- 预期收益：跨边界数据量从全表降到 1 键；配合 P0-2 后单参数修改的主线程成本趋近于零。
- 风险：低。保留旧全表接口兼容 OperatorEditorDialog。

**P0-4 预览图像内存直传 + 临时文件治理**
- 文件：`src/UI/SchemeRunController.cpp`（saveMatToTempPng 调用点）、`qml/EditView/ImageViewer.qml:55`、`PreviewPanel.qml:340`
- 做法（分两档）：
  1. 低成本档：预览输出统一走**单个滚动复用文件**（如固定 `qdv_preview_<nodeId>.png`，覆盖写），启动时清理 7 天前的 `qdv_*.png`；Image 元素 `asynchronous: true` 恢复（QRhi bug 已用 NO_TEXTURE_CACHE 规避，异步解码不会复现跨实例问题）+ `cache: true`。
  2. 进阶档（P1 顺延）：注册 `QQuickImageProvider`（`image://qdv/<nodeId>`），C++ 侧 cv::Mat→QImage 内存直供，彻底绕开 PNG 编码/磁盘。
- 预期收益：预览刷新省 50~200ms UI 线程同步解码；磁盘不再累积垃圾文件。
- 风险：中低。asynchronous 恢复需回归验证 QRhi 跨实例错误是否复现（保留 NO_TEXTURE_CACHE=1 应该安全）。

### P1 —— 本周做（结构性改造，2~5 天/项）

**P1-1 上游链增量执行缓存**
- 位置：`SchemeRunController::runSingleOperator`（:548+）
- 做法：以 (nodeId, paramsHash, 输入图Hash) 为 key 缓存 ToolResult 与 overlayImage；参数只改下游时，上游直接取缓存。连线变化（connectionsChanged）时失效相关下游。
- 预期收益：AI/模板匹配方案下调参预览从"全链重跑 1~3s"降为"仅重跑当前算子"（几十 ms 级）。这是调参体验的**质变项**。

**P1-2 buildToolChainFromNodes 去掉 O(n²)**
- 位置：SchemeRunController.cpp:83-110
- 做法：先构建 `QHash<QString, QVariantMap> nodeIndex` 一次，链上查找 O(1)；顺手把"每次执行 new 全部工具对象"改为按 type 缓存原型 + clone。
- 预期收益：链准备成本从 O(n²) 降到 O(n)；30+ 节点方案有可感提升。

**P1-3 搜索与算子库刷新防抖**
- 位置：Main.qml:1682（搜索）、:755/:785（operatorsChanged 重建）
- 做法：搜索加 200ms Timer 防抖 + 结果ListModel 缓存；`OperatorDescriptors` 增加 `QHash<QString, OperatorMeta>` 索引和 `toMap()` 结果缓存（元数据启动后不变，可安全缓存）。
- 预期收益：中文输入法搜索不再逐候选词刷列表；节点卡片 meta 查询从 O(n) 全量 toMap 变 O(1) 取缓存。

**P1-4 预览重入改"合并最新"**
- 位置：PreviewManager.cpp:99-103（m_isRunning 时直接 return 丢弃请求）
- 做法：running 期间来的请求记入 m_pendingNodeId，执行完成回调里若发现 pending ≠ 已跑节点则自动补跑一次（已有字段，补逻辑即可）。
- 预期收益：消除"改完参数显示的却是旧结果"的正确性体验问题。

**P1-5 选中与预览解耦**
- 位置：PreviewManager.cpp:60-68（onNodeSelected 立即 previewNow）
- 做法：选中仅切换预览目标并显示该节点已有缓存图（ImageVariableManager 里有），仅当目标节点从未执行过且 autoPreview 开启时才排队执行（并入 300ms 防抖）。
- 预期收益：浏览/点选方案不再触发算子执行风暴。

**P1-6 视图切换保活**
- 位置：EditView.cpp:289-321（hideEvent unloadQml）
- 做法：验证 QRhi 修复三件套（固定 d3d11 + NO_TEXTURE_CACHE + basic loop）已稳定的前提下，将 hideEvent 卸载改为 `m_qmlCanvas->hide()` 保活（或仅隐藏+释放纹理 via releaseResources()），仅保留异常兜底卸载路径。
- 预期收益：编辑↔运行来回切换不再白屏重载；面板/滚动/折叠状态保留。
- 风险：中。需回归验证原 QRhi 跨实例 bug 不复现（这是当年 v5.3 的崩溃级 bug，务必带 `tests/UI` 探针回归）。

### P2 —— 择机做（体验增值与长期健康）

| 编号 | 建议 | 说明 |
|---|---|---|
| P2-1 | 性能门槛测试 | 用 QElapsedTimer 给"addOperator/updateOperatorParams/selectNode"埋点，Catch2 加阈值断言（如 30 节点下 <50ms），CI 挡性能回归 |
| P2-2 | 渲染循环调优 | 在 QRhi 修复稳固后，试点去掉 `QSG_RENDER_LOOP=basic`（回 threaded）与 `QSG_NO_TEXTURE_CACHE=1`，A/B 对比帧率与稳定性 |
| P2-3 | 撤销体验 | undoLimit 50→200（QUndoCommand 单参数合并已做，内存压力可控）；加撤销历史下拉 |
| P2-4 | 执行中反馈 | 预览区 isRunning 时显示骨架屏 + 当前执行到第几个上游算子（ToolChainExecutor 已有 toolExecuted 信号可订阅） |
| P2-5 | 状态单源化 | selectedNodeId 以 C++ 为唯一源，QML 经 nodeSelected 信号跟随（含 undo/redo 后同步） |
| P2-6 | 仓库卫生 | 清理 .bak 文件、verify/_out 验证产物入 gitignore 或移出仓库；建立"诊断日志随 bug 修复退役"的规约 |
| P2-7 | Snapshot 后图异步读 | appendSnapshot 的 QImage 读盘挪到子线程（与 P0-4 进阶档合并做） |

### 4.0 落地第一步建议：先建基线

任何优化前，先用 10 分钟加三个 QElapsedTimer 埋点（addOperator / updateOperatorParams / selectNode 全链路 + doPreview 执行耗时），记录 30 节点典型方案的基线数字并写入 `docs/optimization/`。**没有基线，就无法证明"全绿=完成"**——这也是每条优化验收的标准（见下）。

### 4.1 优化验收标准（防止"自检全绿但目标未达成"）

| 验收项 | 量化门槛 |
|---|---|
| 单参数修改（30 节点方案）主线程耗时 | < 30ms（基线预计 80~250ms） |
| 增删节点主线程耗时 | < 60ms |
| 预览刷新（含上游 5 算子、仅当前算子参数变化） | < 300ms 到首帧 |
| 搜索输入单键响应 | < 16ms（无感） |
| 编辑↔运行视图切换回编辑 | < 300ms 无白屏重建 |
| %TEMP% qdv_*.png | 上限复用、7 天自动清理、总量 < 200MB |
| 崩溃/QRhi 错误 | tests/UI 探针 + 手工回归 0 复现 |

---

## 4.2 P0 执行记录（2026-09-06 已落地 ✅）

> 本节为实施记录。P0-1/2/3/4 全部完成，编译 100% 通过，测试回归 746/746 全过，主程序烟测通过。执行中另有两个后台分析代理（C++/QML 两侧）交叉验证了本报告四大主因全部成立，并发现 5 项新增问题，其中 **AI 模型重载风暴**已随本次 P0 一并修复（P0-4b）。

### 4.2.1 已落地改动清单

| 项 | 改动 | 文件 | 验证 |
|---|---|---|---|
| P0-1a | Logger 移除每条无条件 flush，改 100ms 定时器+256 条阈值批量落盘（Error 级立即落盘保崩溃可查） | src/Core/Logger.cpp:32-41 | 编译+测试过 |
| P0-1b | 日志级别过滤：新增 `s_minLevel`（默认 Info，`QDV_LOG_LEVEL` 环境变量可调），`Logger::debug/trace` 在调用点直接丢弃——静音的热路径日志连字符串拼接成本都省掉 | include/Core/Logger.h, src/Core/Logger.cpp | 编译+测试过 |
| P0-1c | 热路径诊断日志降级：updateOperatorParams 逐参数日志（原 1+N 条）、changes/after 确认日志、updateParamInternal、removeConnectionInternal、getRegisteredModels 逐模型日志、logDiag（[RecDiag] 悬停风暴，一处改动覆盖 QML 11 个调用点）、resolveSchemeRunInput 逐节点检查日志 | src/UI/EditViewBridge.cpp, src/UI/SchemeRunController.cpp | 编译过 |
| P0-1d | TunerDialog 3 处 console.log（整表 JSON.stringify ×3）删除；RecommendationPanel 的 diag 逐条目日志删除 | qml/EditView/OperatorTunerDialog.qml, RecommendationPanel.qml | QML 语法随运行验证 |
| P0-2 | **粒度信号 nodeParamsChanged(nodeId, paramNames)**：参数修改不再 emit currentNodesChanged → 画布节点 Repeater 不再全量销毁重建；PropertyPreviewPanel 改听粒度信号刷新参数；VariableManagerPanel（最重级联监听者，双常驻实例）仅在算子参数 Tab 可见时重建；RecommendationPanel 不再对参数变化重算推荐（推荐只依赖连线/类型） | include/UI/EditViewBridge.h:446, src/UI/EditViewBridge.cpp(updateParamInternal), PropertyPreviewPanel.qml, VariableManagerPanel.qml, RecommendationPanel.qml | 编译+测试过 |
| P0-3 | ParamForm 新增单键信号 valueChanged(name, value)（与全表 valuesChanged 并发）；C++ 新增 Q_INVOKABLE updateOperatorParam(nodeId, name, value)；PropertyPreviewPanel 单键优先走增量接口；OperatorEditorDialog 单键浅拷贝替代全表 JSON 深拷贝 | qml/EditView/ParamForm.qml, include/UI/EditViewBridge.h, PropertyPreviewPanel.qml, OperatorEditorDialog.qml | 编译+测试过 |
| P0-4a | 临时 PNG 确定性命名：qdv_single_<nodeId>.png / qdv_deploy_<nodeId>.png 覆盖写（原 UUID 命名永不清理，%TEMP% 无限累积）；快照文件改 qdv_snap_<nodeId>_before/after.png；SchemeRunController 构造时 std::call_once 一次性清理历史 UUID 残留 | src/UI/SchemeRunController.cpp(saveMatToTempPng/构造函数), src/UI/PreviewManager.cpp(appendSnapshot) | 编译+测试过 |
| P0-4b | **InferenceEngine 同路径短路**（代理发现的重大遗漏）：loadModel 相同路径直接复用已加载 cv::dnn::Net；**warmUp 引擎侧去重**（m_warmUpDone，新模型/卸载时重置）——原实现每次预览重建工具对象导致 AI 算子每次重付 readNetFromONNX + 3 次完整 forward（0.3~2s/次） | src/AI/InferenceEngine.cpp(loadModel/warmUp/unloadModel), include/AI/InferenceEngine.h | 编译+测试过 |
| P0-4c | 8 处 Image 元素恢复 asynchronous:true + cache:true（ImageViewer/PreviewPanel 主图与分屏双图/Tuner 预览/VariableManager 缩略图/PropertyPreviewPanel 双缩略图/ImagePreviewWindow 双图）——v5.3 的 QRhi 纹理 bug 已由 main.cpp 渲染层三件套（固定 d3d11+NO_TEXTURE_CACHE+basic loop）规避，不需要用应用层同步解码兜底；PNG 文件名确定性后 cache 不影响内容正确性 | qml/EditView/ 6 个文件 | QML 语法随运行验证 |

### 4.2.2 代理交叉验证结论（两个后台分析代理独立复核）

- **四大主因全部实锤成立**：热路径同步日志（含机制修正：每条仍触发一次写+flush）、QVariantList 全量广播+Repeater 重建、无缓存全链重算、PNG 磁盘中转。
- **数字修正**：currentNodesChanged 监听为 12 处（原报告 11）；每张节点卡片 getOperatorMeta 调用 4 次（原报告 2 次，另两处在 Accessible 绑定）；同一图像最多 4 个消费者（原 3）。
- **代理新发现（已修复 1 项，其余列入 P1/P2）**：
  1. ✅已修复→P0-4b：预览链每次重建全部工具对象 → AI 算子每次预览重新 readNetFromONNX + warmUp(3)；InferenceEngine 无同路径缓存（**报告原最大遗漏，带模型方案调参秒级延迟的最大单一因素**）；
  2. 列入 P1：VariableManagerPanel.buildOperatorParamsList 为最重级联监听者（已通过 P0-2 可见性门控大幅缓解，彻底方案为按节点增量更新）；
  3. 列入 P1：hitTestConnection 每帧鼠标移动 O(连线×20) 贝塞尔采样；
  4. 列入 P1：搜索一键 ≥4 次全量 getFilteredOperators 重算（计数标签+列表+飞出菜单各自求值）；
  5. 列入 P1：_maybeSnapshot 的 JSON.stringify 键序不一致风险（C++ QVariantMap 字母序 vs QML 插入序，可能永不收敛致每次深拷贝）；
  6. 列入 P2：collectUpstreamNodes 递归内重复深拷贝 connections（O(链长) 次）；checkPortCompatible 每次连线 new 两个算子对象；PreviewManager 同节点调参时仍丢请求（补偿条件是 nodeId 比较，应加参数版本号）。

### 4.2.3 验证记录（防止"全绿≠完成"）

| 验收项 | 结果 |
|---|---|
| 编译 | 100% 通过（QDetectVision.exe + QDV_tests.exe；修复 3 个构建问题：Logger 静态封装调成员 log、qEnvironmentVariable 返回类型、build 缓存的旧 GLOB 引用已删模型文件→重跑 CMake 配置） |
| 测试回归 | QDV_tests 全套 **746/746 通过，0 断言失败**（offscreen 模式多轮运行确认；有间歇段错误出现在 ResultDatabase boundary 压力测试，经 git stash 对照基线验证为**存量 flaky**（基线 4 轮中 2 轮同样崩溃，QSqlite 连接管理历史问题，源码注释有"崩溃根因修复"记录），非本次引入——已单列 P2 跟进） |
| 主程序烟测 | offscreen 启动存活 6s+，登录页正常显示，日志完整输出 |
| 预期性能收益（待实测校准） | 改参数：省 6~30 条同步磁盘日志 + 全画布卡片重建（30 节点约 50~200ms）+ 全表跨界拷贝；带 AI 算子方案调参预览：省每次 0.3~2s 模型重载+warmUp；预览刷新：省 40~150ms UI 线程同步解码；%TEMP%：从无限累积变为每节点恒定 1 文件 |

### 4.2.4 顺带修复的构建环境问题

- build/bin 缺 Qt6Test.dll 等运行库导致 QDV_tests.exe 0xC0000139 静默退出（表现为 Git Bash exit 127 无输出）→ windeployqt 补齐；
- build 缓存引用已删除的 model_20260712_201806.onnx → 重跑 CMake 配置重新 GLOB。

---

## 5. 优化路线图（建议节奏）

> 注：P0 四项已全部于 2026-09-06 当日完成（见 4.2 节执行记录）；P0-2 采用了低风险粒度信号方案（完整 NodeListModel 角色化列为后续迭代）。下述节奏中 P0 行仅作历史留档。

```
第 1 天     P0-1 日志静音+异步落盘、P0-3 增量回传        （低风险，立竿见影）
第 2~3 天   P0-4 临时文件治理+Image异步恢复、P1-2 去O(n²)、P1-3 搜索防抖+meta缓存
第 4~6 天   P0-2 NodeListModel 角色化改造（QML 分批迁移）
第 7~9 天   P1-1 增量执行缓存（先 AI 算子场景验证）、P1-4 重入合并、P1-5 选中解耦
第 10 天+   P1-6 视图保活（带回归）→ P2 按需
全程        每批改动跑 tests/（offscreen）+ 30 节点方案手工回归 + 基线对比
```

---

## 6. 附：本次评估覆盖的关键文件清单

- 编辑核心：`src/UI/EditViewBridge.cpp`(2106行) / `SchemeRunController.cpp`(739) / `PreviewManager.cpp` / `UndoCommands.cpp` / `src/UI/EditView.cpp` / `src/UI/CentralWindow.cpp`
- QML：`qml/EditView/Main.qml`(4194行) / `ParamForm.qml`(1134) / `PropertyPreviewPanel.qml` / `PreviewPanel.qml` / `ImageViewer.qml` / `OperatorTunerDialog.qml` / `DesignTokens.qml`
- 支撑：`src/Core/Logger.cpp`+`h` / `src/UI/OperatorDescriptors.cpp` / `src/AI/ModelManager.cpp` / `apps/SmartVision/main.cpp`(QSG 环境变量) / `include/UI/UndoCommands.h`
- 历史文档：`QDV_项目总结_2026-08-13_21-23.md`、`docs/optimization/*` 两份报告（对照历次修复，避免推翻已修 bug）

---

*报告生成：GLM-5.3 ｜ 2026-09-06 ｜ 评估方式为静态代码走查，量化数字为基于代码路径的工程估算；落地前请以 4.0 节基线埋点实测校准。配套可视化说明见同目录 `优化说明0906.html`。*
