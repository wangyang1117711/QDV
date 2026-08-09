# O1a · 算子库 MVP 预打包导入 — 实施计划

| 字段 | 值 |
|---|---|
| 配套 spec | [2026-07-15-O1a-operator-import-design.md](./2026-07-15-O1a-operator-import-design.md) |
| 创建日期 | 2026-07-15 |
| 状态 | 待执行 |

---

## 阶段 0：CMake 接入（前置必须）

### Task 0.1 · 新建 src/OperatorLibrary/CMakeLists.txt
**目标**：让 OperatorLibrary 模块进入构建系统
**输入契约**：根 CMakeLists.txt 提供项目变量 `${CMAKE_CURRENT_SOURCE_DIR}`、Qt6::Core、Qt6::Gui
**输出契约**：`QDV::OperatorLibrary` 静态库目标，含 `OperatorDefinition.cpp`
**步骤**：
1. 创建 [src/OperatorLibrary/CMakeLists.txt](file:///e:/anchor/Trae/QDV/src/OperatorLibrary/CMakeLists.txt)
2. 定义 `add_library(QDV_OperatorLibrary STATIC OperatorDefinition.cpp)`
3. `target_include_directories(... PUBLIC ${CMAKE_SOURCE_DIR}/include)`
4. `target_link_libraries(... PUBLIC Qt6::Core Qt6::Gui)`
5. `add_library(QDV::OperatorLibrary ALIAS QDV_OperatorLibrary)`
**验证**：单独构建此目标无错误
**评审者检查点**：CMake 文件语法、链接库完整性

### Task 0.2 · 根 CMakeLists.txt 添加子目录
**目标**：将 OperatorLibrary 纳入主构建
**输入契约**：Task 0.1 完成
**输出契约**：根构建系统包含 OperatorLibrary
**步骤**：
1. 编辑 [根 CMakeLists.txt](file:///e:/anchor/Trae/QDV/CMakeLists.txt) L86 后插入 `add_subdirectory(src/OperatorLibrary)`
**验证**：`cmake --build . --target QDV_OperatorLibrary` 成功
**评审者检查点**：插入位置正确、不破坏其他 target

### Task 0.3 · 全量编译验证
**目标**：确认 OperatorDefinition.cpp 编译通过
**输入契约**：Task 0.1 + 0.2 完成
**输出契约**：全量构建无新增错误
**步骤**：
1. 运行 `cmake --build build_D盘` 或等效构建命令
2. 确认无 OperatorDefinition 相关编译错误
**验证**：构建产物中含 OperatorLibrary 静态库
**评审者检查点**：编译警告（如有）是否合理

---

## 阶段 1：持久化层 OperatorRegistryStore

### Task 1.1 · 实现 OperatorRegistryStore
**目标**：完成 .qdvop 文件 I/O 层
**输入契约**：[include/OperatorLibrary/OperatorRegistryStore.h](file:///e:/anchor/Trae/QDV/include/OperatorLibrary/OperatorRegistryStore.h) 已存在；OperatorDef 可用
**输出契约**：实现 scanAll/save/remove/contains 四个核心方法
**步骤**：
1. 读取 [include/OperatorLibrary/OperatorRegistryStore.h](file:///e:/anchor/Trae/QDV/include/OperatorLibrary/OperatorRegistryStore.h) 确认接口
2. 创建 [src/OperatorLibrary/OperatorRegistryStore.cpp](file:///e:/anchor/Trae/QDV/src/OperatorLibrary/OperatorRegistryStore.cpp)
3. 实现 `scanAll()`：QDir 入口过滤 `*.qdvop`，跳过 `*.tmp`/`*.bak`，按文件名字典序，逐文件 `OperatorDef::fromJson()`
4. 实现 `save(def)`：原子写入 tmp→rename，已存在则先备份 .bak
5. 实现 `remove(type)`：QFile::remove，失败返回错误
6. 实现 `contains(type)`：查 m_loaded 缓存
7. 实现目录自动创建 + 降级到 QStandardPaths
**验证**：单元测试 8 项（见 spec §6.2）
**评审者检查点**：原子写入正确性、错误传播完整

### Task 1.2 · 编写 OperatorRegistryStore 单元测试
**目标**：覆盖 8 个核心用例
**输入契约**：Task 1.1 完成
**输出契约**：测试可执行并通过
**步骤**：
1. 创建 [tests/operator_library_test.cpp](file:///e:/anchor/Trae/QDV/tests/operator_library_test.cpp)（沿用项目现有测试框架）
2. 用例：扫描空目录、扫描含损坏文件、保存覆盖、删除不存在、目录创建失败降级、原子写入验证、tmp 跳过、备份生成
3. 修改 [tests/CMakeLists.txt](file:///e:/anchor/Trae/QDV/tests/CMakeLists.txt) 加入新测试目标
**验证**：`ctest -R operator_library` 全过
**评审者检查点**：测试独立性（无副作用）、断言有效性

---

## 阶段 2：校验层 OperatorValidator

### Task 2.1 · 实现 OperatorValidator
**目标**：完成字段校验
**输入契约**：[include/OperatorLibrary/OperatorValidator.h](file:///e:/anchor/Trae/QDV/include/OperatorLibrary/OperatorValidator.h) 已存在；OperatorDef 可用
**输出契约**：实现 `validate(def)` 返回 ValidationResult
**步骤**：
1. 读取头文件确认接口
2. 创建 [src/OperatorLibrary/OperatorValidator.cpp](file:///e:/anchor/Trae/QDV/src/OperatorLibrary/OperatorValidator.cpp)
3. 实现校验规则（spec §3.2）：
   - type 正则 `^[A-Za-z][A-Za-z0-9_]{2,63}$`
   - cnName 非空 + ≤32 字符
   - version 语义化版本
   - inputs/outputs 至少 1 个 + name 唯一
   - params name 唯一 + Enum 必有 options
   - implementation recipe/library 至少一项非空
   - recipe 引用未知算子为 Warning（需调 OperatorDescriptors::has，但为避免循环依赖，此检查放 Importer 层）
4. 更新 CMakeLists 加入新 .cpp
**验证**：单元测试 12 项
**评审者检查点**：正则正确性、Error/Warning 分级正确

### Task 2.2 · 编写 OperatorValidator 单元测试
**目标**：覆盖 12 个用例
**输入契约**：Task 2.1 完成
**输出契约**：测试通过
**步骤**：
1. 在 operator_library_test.cpp 追加 Validator 测试段
2. 用例：全字段合法、type 缺失、type 含非法字符、Enum 无 options、recipe 未知算子（mock）、version 非语义化、cnName 超长、inputs 名重复等
**验证**：`ctest -R operator_library` 全过
**评审者检查点**：边界用例完整性

---

## 阶段 3：导入层 OperatorImporter

### Task 3.1 · 实现 OperatorImporter
**目标**：完成解析+校验+查重 → ImportPreview
**输入契约**：Task 1.1 + 2.1 完成；OperatorDef::fromJson 可用
**输出契约**：实现 `importFile(path)` 与 `importFromJson(obj)`
**步骤**：
1. 创建 [src/OperatorLibrary/OperatorImporter.cpp](file:///e:/anchor/Trae/QDV/src/OperatorLibrary/OperatorImporter.cpp)
2. `importFile(path)`：
   - QFile 读取（错误：文件不存在/读权限）
   - QJsonDocument::fromJson（错误：JSON 解析失败，含行号）
   - OperatorDef::fromJson(err)（错误：字段错误）
   - Validator::validate(def)（issues 转 conflicts/warnings）
   - recipe 引用算子检查（调 OperatorDescriptors::has）→ Warning
   - 返回 ImportPreview{def, conflicts, warnings}
3. `importFromJson(obj)`：跳过文件读取，其余同上
4. `detectConflicts(def, store)`：与注册表比对（本切片简化为 type 查重）
5. 更新 CMakeLists
**验证**：单元测试 10 项
**评审者检查点**：错误信息可读性、ImportPreview 字段完整

### Task 3.2 · 编写 OperatorImporter 单元测试
**目标**：覆盖 10 个用例
**输入契约**：Task 3.1 完成
**输出契约**：测试通过
**步骤**：
1. 在测试文件追加 Importer 段
2. 用例：合法文件、空文件、JSON 损坏、type 重复（mock store）、字段类型错误、嵌套对象错误、Implementation 全空等
3. 注意：Importer 测试需 mock OperatorDescriptors（或隔离该调用）
**验证**：测试通过
**评审者检查点**：mock 隔离性

---

## 阶段 4：控制器 OperatorLibraryController

### Task 4.1 · 实现 OperatorLibraryController
**目标**：完成门面层，桥接 OperatorDescriptors
**输入契约**：Task 1.1 + 2.1 + 3.1 完成；OperatorDescriptors::registerExternalOperator 可用
**输出契约**：实现 init/importFile/commitImport/removeImported/query
**步骤**：
1. 创建 [src/OperatorLibrary/OperatorLibraryController.cpp](file:///e:/anchor/Trae/QDV/src/OperatorLibrary/OperatorLibraryController.cpp)
2. `init(operatorsPath)`：调 RegistryStore::scanAll() → 逐 def 调 OperatorDescriptors::registerExternalOperator(def.toOperatorMeta())
3. 需实现 `OperatorDef::toOperatorMeta()` 适配器（如未在 OperatorDefinition.cpp 中实现，需补充）
4. `importFile(path)`：委托 OperatorImporter
5. `commitImport(preview)`：
   - 二次校验 conflicts 为空
   - RegistryStore::save(def)（含原子写入）
   - OperatorDescriptors::registerExternalOperator(meta)
   - 失败回滚：删除已写文件
   - 成功：m_loaded[type] = def + 发信号 importedChanged(type, "added")
6. `removeImported(type)`：RegistryStore::remove + m_loaded.remove + 发信号
7. `query(category, keyword)`：遍历 m_loaded 过滤
8. 单例实现：s_instance + instance()
9. 更新 CMakeLists
**验证**：集成测试 5 项（spec §6.3）
**评审者检查点**：事务回滚正确性、信号发射时机

### Task 4.2 · 编写 OperatorLibraryController 集成测试
**目标**：覆盖 6 个用例 + 5 个集成测试
**输入契约**：Task 4.1 完成
**输出契约**：测试通过
**步骤**：
1. 在测试文件追加 Controller 段
2. 用例：init 流程、commitImport 事务回滚（mock registerExternalOperator 失败）、removeImported、二次校验防 TOCTOU 等
3. 集成测试：启动加载 3 文件、searchOperators 包含、删除重启、内置冲突、端到端拖拽（手动）
**验证**：测试通过
**评审者检查点**：集成测试真实性（无过度 mock）

### Task 4.3 · 验证 OperatorDef::toOperatorMeta 适配器
**目标**：确认 OperatorDef → OperatorMeta 转换正确
**输入契约**：Task 4.1 完成
**输出契约**：转换后字段对齐
**步骤**：
1. 检查 [include/UI/OperatorDescriptors.h](file:///e:/anchor/Trae/QDV/include/UI/OperatorDescriptors.h) 中 OperatorMeta 字段
2. 检查 [include/OperatorLibrary/OperatorDefinition.h](file:///e:/anchor/Trae/QDV/include/OperatorLibrary/OperatorDefinition.h) 中 OperatorDef::toMap()
3. 实现/补充 toOperatorMeta() 适配器（若已存在则验证）
4. 关键字段映射：type/cnName/category/subGroup/iconPath/description/params/outputs
5. ParamDef → ParamSpec 类型映射（spec §1.4 决策）
**验证**：转换后 OperatorMeta 可被 EditViewBridge 正常消费
**评审者检查点**：字段映射完整性、类型映射正确

---

## 阶段 5：QML 列表模型 OperatorListModel

### Task 5.1 · 实现 OperatorListModel
**目标**：QAbstractListModel 包装 Controller 查询结果
**输入契约**：Task 4.1 完成
**输出契约**：QML 可用的列表模型
**步骤**：
1. 创建 [include/OperatorLibrary/OperatorListModel.h](file:///e:/anchor/Trae/QDV/include/OperatorLibrary/OperatorListModel.h) 与 src/OperatorLibrary/OperatorListModel.cpp
2. 继承 QAbstractListModel
3. 角色：type/cnName/category/version/isDeleted
4. `setSource(defs)`：beginResetModel/endResetModel
5. 监听 Controller::importedChanged 自动刷新
6. 更新 CMakeLists
**验证**：QML 能实例化并读取数据
**评审者检查点**：角色完整性、信号正确性

---

## 阶段 6：QML 桥接 OperatorLibraryBridge

### Task 6.1 · 实现 OperatorLibraryBridge
**目标**：QML ↔ Controller 桥接
**输入契约**：Task 4.1 + 5.1 完成
**输出契约**：Q_INVOKABLE 方法 + 信号
**步骤**：
1. 创建 [include/UI/OperatorLibraryBridge.h](file:///e:/anchor/Trae/QDV/include/UI/OperatorLibraryBridge.h) 与 [src/UI/OperatorLibraryBridge.cpp](file:///e:/anchor/Trae/QDV/src/UI/OperatorLibraryBridge.cpp)
2. Q_INVOKABLE 方法：
   - `requestPreview(QString path) → QVariantMap`（含 def + conflicts + warnings）
   - `commitImport(QVariantMap preview) → bool`
   - `removeImported(QString type) → QVariantMap{ok, error}`（含画布预检）
   - `listImported() → QVariantList`
   - `importedCount() → int`
3. 信号：`importedChanged(QString type, QString action)`、`importFailed(QString msg)`、`previewReady(QVariantMap preview)`
4. 画布预检：遍历 EditViewBridge::currentNodes 检查 type 使用情况
5. 更新 [src/UI/CMakeLists.txt](file:///e:/anchor/Trae/QDV/src/UI/CMakeLists.txt) 加入新 .cpp + 链接 QDV::OperatorLibrary
**验证**：QML 能调用方法并接收信号
**评审者检查点**：方法签名 QML 友好、画布预检逻辑正确

### Task 6.2 · EditViewBridge 集成
**目标**：将 OperatorLibraryBridge 暴露给 QML
**输入契约**：Task 6.1 完成
**输出契约**：QML 可通过 editViewBridge.operatorLibraryBridge 访问
**步骤**：
1. 编辑 [include/UI/EditViewBridge.h](file:///e:/anchor/Trae/QDV/include/UI/EditViewBridge.h) 添加成员 `OperatorLibraryBridge* m_operatorLibraryBridge`
2. 添加 Q_PROPERTY 或 getter
3. 在 EditViewBridge 构造函数中创建实例
4. 在 main.cpp 启动时调 `OperatorLibraryController::instance().init("config/operators_imported")`
**验证**：QML 端 `editViewBridge.operatorLibraryBridge` 不为 undefined
**评审者检查点**：生命周期管理、初始化时机

---

## 阶段 7：导入对话框 OperatorImportDialog.qml

### Task 7.1 · 实现 OperatorImportDialog.qml
**目标**：完整的导入对话框 UI
**输入契约**：Task 6.2 完成；HTML mockup 可参考
**输出契约**：可打开/关闭/交互的 Popup
**步骤**：
1. 创建 [qml/EditView/OperatorImportDialog.qml](file:///e:/anchor/Trae/QDV/qml/EditView/OperatorImportDialog.qml)
2. 复用 DesignTokens（参考 [qml/EditView/OperatorEditorDialog.qml](file:///e:/anchor/Trae/QDV/qml/EditView/OperatorEditorDialog.qml) 的 Popup 模式）
3. 4 分区布局（选文件/预览/校验结果/按钮）
4. 文件选择：FileDialog 过滤 `*.qdvop`
5. 选文件后自动调 `operatorLibraryBridge.requestPreview(path)`
6. 监听 `previewReady` 信号填充预览面板
7. 冲突/警告/成功三色分区显示
8. 提交按钮在 conflicts 非空时禁用
9. 提交成功 → showToast + close
10. 注册到 [qml/EditView.qrc](file:///e:/anchor/Trae/QDV/qml/EditView.qrc)
**验证**：手动操作验证（对照 HTML mockup 场景 2/3）
**评审者检查点**：与 DesignTokens 一致、交互流畅

---

## 阶段 8：已导入算子面板 ImportedOperatorsPanel.qml

### Task 8.1 · 实现 ImportedOperatorsPanel.qml
**目标**：已导入算子列表 + 删除功能
**输入契约**：Task 6.2 + 7.1 完成
**输出契约**：可列表/删除的 Popup
**步骤**：
1. 创建 [qml/EditView/ImportedOperatorsPanel.qml](file:///e:/anchor/Trae/QDV/qml/EditView/ImportedOperatorsPanel.qml)
2. 顶部搜索框
3. ListView 使用 OperatorListModel
4. 列表项：name + version + cnName + category + 删除按钮
5. 已删除项置灰 + "已删除·重启生效"标签
6. 删除按钮 → 二次确认对话框（MessageDialog）
7. 确认 → `operatorLibraryBridge.removeImported(type)` → 根据返回显示 Toast 或阻断信息
8. 底部说明文字（HTML mockup 场景 4）
9. 注册到 qrc
**验证**：手动操作验证（对照 HTML mockup 场景 4/5）
**评审者检查点**：删除预检提示清晰、置灰逻辑

---

## 阶段 9：Main.qml 入口按钮接入

### Task 9.1 · 添加算子库面板底部入口
**目标**：用户可见的入口
**输入契约**：Task 7.1 + 8.1 完成
**输出契约**：算子库面板底部出现两个按钮
**步骤**：
1. 编辑 [qml/EditView/Main.qml](file:///e:/anchor/Trae/QDV/qml/EditView/Main.qml) 左侧算子库面板
2. 在 panel-body 之后、面板结束前插入 RowLayout
3. 两个 Button：导入算子（primary）/ 已导入 (N)
4. Loader 实例化 OperatorImportDialog 与 ImportedOperatorsPanel
5. 已导入按钮文字绑定 `operatorLibraryBridge.importedCount`
6. 监听 `importedChanged` 信号触发 `operatorList.model = null; buildModel()`（复用 L1383-1384 模式）
**验证**：编译运行后，按钮可见且可点击打开对话框
**评审者检查点**：布局不破坏现有面板、刷新机制正确

---

## 阶段 10：端到端验收

### Task 10.1 · 端到端验收
**目标**：验证 spec §0.3 三层成功标准
**输入契约**：所有前置 Task 完成
**输出契约**：VERDICT PASS
**步骤**：
1. **技术成功**：
   - 运行 `ctest` 全部通过
   - 应用启动无崩溃
   - 导入对话框可打开/关闭
2. **业务成功**：
   - 准备合法 .qdvop 样例文件
   - 点"导入算子"→ 选文件 → 预览 → 提交 → Toast 成功
   - EditView 左侧面板出现新算子
   - 拖入画布 → 双击 → OperatorEditorDialog 可配置参数
   - 从"已导入"面板删除 → Toast 成功 → 重启后算子消失
3. **用户满意度**：
   - 点击次数 ≤ 4
   - 错误提示可读
   - UI 风格无割裂（对照 HTML mockup）
4. **对抗性用例**：
   - TOCTOU（手动模拟）
   - 删除时画布依赖（拖入节点后尝试删除）
   - 内置 type 冲突（导入 type=Threshold）
5. 生成验收报告（VERDICT 结构，AGENTS.md 规则 III.1）
**验证**：VERDICT: PASS
**评审者检查点**：所有业务用例通过、对抗用例符合预期

### Task 10.2 · 编写用户操作手册
**目标**：spec 末尾要求"提供详细用户操作手册"
**输入契约**：Task 10.1 通过
**输出契约**：操作手册文档
**步骤**：
1. 创建 [docs/QDV_v2.7.0_算子导入功能使用指南.md](file:///e:/anchor/Trae/QDV/docs/QDV_v2.7.0_算子导入功能使用指南.md)
2. 章节：功能概述/前置条件/导入流程/删除流程/常见问题/示例 .qdvop 模板
3. 含截图（来自实际运行）
**验证**：文档审阅通过
**评审者检查点**：步骤清晰、示例可复现

---

## 验收清单（Verdict Gate）

执行完毕后，评审者依据以下清单输出 VERDICT：

| 检查项 | 验证方法 | 通过标准 |
|---|---|---|
| 契约合规性 | 检查 ImportPreview/OperatorDef/OperatorMeta 字段流转 | 无字段丢失/类型错误 |
| 业务逻辑对齐 | 端到端用例 | 导入/删除/搜索全部通过 |
| CMake 集成 | 全量构建 | 无新增编译错误 |
| 测试覆盖 | ctest | ≥36 单元 + 5 集成全过 |
| 对抗用例 | 5 项红队 | 全部符合预期行为 |
| UI 一致性 | 对照 HTML mockup | 配色/布局/交互一致 |
| 性能 | 启动时间对比 | 导入算子库加载 < 200ms |
| 无回归 | 现有 EditView 功能 | 拖拽/搜索/参数编辑正常 |

**VERDICT: PASS** 当且仅当全部检查项通过。
**VERDICT: FAIL** 任何一项不通过，列明原因 + 修复路径。
**VERDICT: NEEDS_HUMAN_REVIEW** 当出现 spec 未覆盖的边界情况。

---

## 风险与缓解

| 风险 | 概率 | 影响 | 缓解 |
|---|---|---|---|
| OperatorDef::toOperatorMeta 适配器字段缺失 | 中 | 高 | Task 4.3 专门验证 |
| OperatorDescriptors::has 在 Importer 中调用导致循环依赖 | 中 | 中 | 通过前向声明或将此检查移至 Controller |
| QML 端 OperatorListModel 角色名与 QML 期望不一致 | 低 | 低 | Task 5.1 验证 |
| Qt6::Dialogs FileDialog 在某些平台行为差异 | 低 | 低 | 测试平台覆盖 |
| 启动加载顺序：Controller init 必须在 EditViewBridge 创建后 | 中 | 高 | main.cpp 显式控制顺序 |
