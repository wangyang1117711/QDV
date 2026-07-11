# Spec 1: 算法模块功能梳理与非真实算法骨架设计

**版本**: 1.0
**日期**: 2026-07-06
**作者**: brainstorming 流程
**状态**: 待用户审阅
**试点**: AI 指挥多 AGENTS 试点 2（共 3 个 Spec，本文档为 Spec 1）

---

## 1. 概述

### 1.1 定位

本 Spec 是"AI 指挥多 AGENTS 试点 2"的第 1 阶段，目标是构建**通用性算法骨架**，使算子库可被任意 Agent 协作场景复用，而非局限于具体视觉任务。

### 1.2 范围

| 需求 | 对应工作 | 交付物 |
|------|---------|--------|
| 1.1 算法模块功能梳理 | 扫描 47 个算子元数据 | 功能清单 + 技术文档 + API 文档 |
| 1.2 开发非真实算法 | 32 个元数据算子 mock 补全 + 5 个新增典型 mock | 37 个独立 dll + MockOperatorBase |
| 1.3 算法与 Agent 框架映射 | AgentOperatorRouter 组件 | C++ 静态映射路由组件 |

### 1.3 不在本 Spec 范围

- 算子智能排序与频率统计（Spec 2）
- 自定义算子参数 UI / 注册机制 / 验证体系（Spec 2）
- 算法接口效率优化（+20%）与稳定性（1000 次）（Spec 3）
- 测试覆盖率工具配置与性能基准测试（Spec 3）

---

## 2. 前置工作：Pilot 1 合并

### 2.1 合并内容

| 成果 | 源路径 | 目标路径 |
|------|--------|---------|
| OperatorSDK 接口与实现 | `.pilot-worktree/src/OperatorSDK/` | `src/OperatorSDK/` |
| Histogram 算子 | `.pilot-worktree/src/operators/extensions/Histogram/` | `src/operators/extensions/Histogram/` |
| 15 条红队测试 | `.pilot-worktree/tests/OperatorSDK/` + `tests/Operators/` + `tests/Vision/test_ai_classify_di.cpp` | `tests/` 对应子目录 |
| CMakeLists 更新 | `.pilot-worktree/tests/CMakeLists.txt` | `tests/CMakeLists.txt`（合并新增条目） |
| Agent 协作框架 | `.pilot-worktree/.agent_mesh/` | `.agent_mesh/`（已存在则跳过） |

### 2.2 合并前验证

1. 在 `.pilot-worktree` 运行 `gatekeeper.ps1` 确认编译 + 单测全绿
2. 解决已知的测试进程退出码 0xC0000005 问题（catch2 静态变量析构崩溃）
3. 合并后主仓重新编译 + 运行全部测试确认无回归

### 2.3 合并方式

`git merge agent/pilot/20260705` 或 cherry-pick 关键提交，合并后打 tag `pilot1-merged-20260706`。

---

## 3. 架构总览

### 3.1 目录结构

```
QDV 主仓 (main)
├── src/
│   ├── OperatorSDK/                       # [Pilot1合并] 接口+实现
│   │   └── include/OperatorSDK/
│   │       └── MockOperatorBase.h         # [新增] mock算子共享基类
│   ├── operators/extensions/              # 动态算子目录（命名规则见 3.3 节）
│   │   ├── Histogram/                     # [Pilot1] 真实算子参考实现
│   │   ├── MockGrabImage/                 # [新增] mock补全算子示例1
│   │   ├── MockGaussFilter/               # [新增] mock补全算子示例2
│   │   ├── ... (共32个mock补全，命名 Mock<Type>)
│   │   ├── MockClassify/                  # [新增] 典型mock算子1（Agent角色）
│   │   ├── MockValidate/                  # [新增] 典型mock算子2（Agent角色）
│   │   ├── MockPlan/                      # [新增] 典型mock算子3（Agent角色）
│   │   ├── MockCoordinate/                # [新增] 典型mock算子4（Agent角色）
│   │   └── MockAudit/                     # [新增] 典型mock算子5（Agent角色）
│   └── Core/
│       └── AgentOperatorRouter/           # [新增] 路由组件
│           ├── AgentOperatorRouter.h
│           └── AgentOperatorRouter.cpp
├── tools/
│   ├── operator_template_gen.py           # [新增] 算子模板生成脚本
│   └── gen_operator_docs.py               # [新增] 文档自动生成脚本
├── docs/
│   ├── algorithms/                        # [新增] 算法文档目录
│   │   ├── 算子功能清单.md                 # 自动生成
│   │   ├── 算子技术文档.md                 # 自动生成
│   │   ├── 算子开发指南.md                 # 手写
│   │   ├── Doxyfile                       # Doxygen 配置
│   │   └── api-html/                      # Doxygen 生成 HTML
│   └── superpowers/specs/
└── tests/
    ├── OperatorSDK/                       # [Pilot1合并] 3个测试
    ├── Operators/                         # [Pilot1合并+新增]
    │   ├── test_histogram.cpp             # [Pilot1]
    │   ├── test_mock_base.cpp             # [新增]
    │   ├── test_mock_operators.cpp        # [新增] 32 mock 批量参数化
    │   └── test_agent_mock_operators.cpp  # [新增] 5 Agent mock
    └── Core/
        └── test_agent_operator_router.cpp # [新增]
```

### 3.2 核心数据流

```
任务类型字符串 ──→ AgentOperatorRouter::route(taskType)
                      │
                      ├─→ 返回 QVector<OperatorAgentMapping>
                      │     {operatorType, agentRole, priority}
                      │
                      └─→ 调用方据此:
                          1. 通过 IOperatorRegistry 获取算子创建器
                          2. 创建算子实例 → execute(context)
                          3. 按 agentRole 分配给对应 Agent 角色处理
```

### 3.3 关键设计决策

1. **mock 算子行为规范**：接收输入图像 → 返回合法 `ToolResult`（含输出图像路径 + data 字段），但不做真实计算。输出图像 = 输入图像副本或固定生成的占位图。`ToolResult.reason` 标注 `[MOCK]` 前缀。
2. **MockOperatorBase 共享基类**：封装通用 mock 行为（参数校验、占位图生成、ToolResult 构造），32 个 mock 算子继承它，各自只实现 `mockTag()` 和极简 `execute()`。
3. **Router 硬编码结构**：`QMap<QString, QVector<OperatorAgentMapping>>`，key = 任务类型（参照 Halcon 算子分类），value = 算子 + Agent 角色 + 优先级列表。
4. **manifest.json 增 `isMock` 字段**：mock 算子设为 `true`，UI 可据此标注。
5. **命名约定**：mock 算子目录名与 dll 名使用 `Mock<Type>` 前缀（如 `MockGaussFilter/` → `MockGaussFilter.dll`），但 `manifest.json` 的 `type` 字段保持原始算子类型名（如 `GaussFilter`），确保框架按原始类型识别。5 个新增 Agent mock 算子的 `type` 字段使用 `Mock` 前缀（如 `MockClassify`），因为它们是全新类型而非现有类型的 mock 补全。

---

## 4. mock 算子设计

### 4.1 MockOperatorBase 共享基类

```cpp
// src/OperatorSDK/include/OperatorSDK/MockOperatorBase.h
class MockOperatorBase : public QDV::IOperator {
public:
    // 通用 execute：参数校验 → 生成占位输出 → 构造 ToolResult
    ToolResult execute(const OperatorContext& ctx) override {
        validateParams(ctx.params);
        cv::Mat placeholder = generatePlaceholder(ctx.inputImage);
        QString outPath = savePlaceholder(placeholder, ctx.nodeId);
        return buildMockResult(outPath, ctx);
    }
    QString version() const override { return "1.0.0"; }
    IOperator* clone() const override = 0;  // 子类实现
protected:
    virtual QString mockTag() const = 0;    // 子类提供标签，如 "GaussFilter"
    virtual cv::Mat generatePlaceholder(const cv::Mat& input) const;
    virtual void validateParams(const QVariantMap& params) const;
    virtual ToolResult buildMockResult(const QString& outPath, const OperatorContext& ctx) const;
};
```

### 4.2 32 个 mock 补全算子清单

| 类别 | 需 mock 的算子 type | 数量 |
|------|---------------------|------|
| 图像采集 | GrabImage, OpenFramegrabber | 2 |
| 预处理 | FftGeneric, GaussFilter, MeanImage, Emphasize, ScaleImage, MedianImage | 6 |
| 几何变换 | AffineTransImage, PolarTransImage | 2 |
| 形态学 | Closing, BottomHat, Erosion, Opening, TopHat, Dilation | 6 |
| 图像分割 | DynThreshold, Watershed, RegionGrowing | 3 |
| Blob 分析 | Connection, SelectShape | 2 |
| 特征提取 | PointsHarris, EdgesSubPix | 2 |
| 匹配定位 | FindNccModel, FindShapeModel | 2 |
| 几何测量 | DistancePp, AngleLl | 2 |
| 3D 视觉 | Reconstruct3D, BinocularDisparity, SurfaceMatching | 3 |
| 深度学习 | SegmentDl, DetectObjectsDl | 2 |
| **合计** | | **32** |

### 4.3 5 个新增典型 mock 算子（对应 Agent 角色）

| 算子 type | cnName | 对应 Agent 角色 | 用途 |
|-----------|--------|----------------|------|
| MockClassify | 模拟分类 | Reviewer | 模拟评审分类判定 |
| MockValidate | 模拟验证 | Gatekeeper | 模拟终门验证 |
| MockPlan | 模拟规划 | Planner | 模拟生成执行计划 |
| MockCoordinate | 模拟协调 | Coordinator | 模拟任务调度 |
| MockAudit | 模拟审计 | SpecGuardian | 模拟审计检查 |

### 4.4 单个 mock 算子目录结构（独立 dll 模式）

```
src/operators/extensions/MockGaussFilter/      # 以 GaussFilter 为例
├── CMakeLists.txt          # 编译为 SHARED 库
├── manifest.json           # 从 operators.json 对应条目生成 + isMock:true
├── MockGaussFilterOperator.h   # 继承 MockOperatorBase
└── MockGaussFilterOperator.cpp # 极简实现：mockTag() + C API 导出
```

单个 mock 算子 cpp 约 30 行：

```cpp
// MockGaussFilterOperator.cpp
#include "MockGaussFilterOperator.h"

QString MockGaussFilterOperator::mockTag() const { return "GaussFilter"; }

IOperator* MockGaussFilterOperator::clone() const {
    return new MockGaussFilterOperator(*this);
}

extern "C" {
    QDV_EXPORT const char* operator_type() { return "GaussFilter"; }
    QDV_EXPORT const char* operator_version() { return "1.0.0"; }
    QDV_EXPORT QDV::IOperator* create_operator() { return new MockGaussFilterOperator(); }
}
```

### 4.5 mock 算子 ToolResult 规范

```json
{
    "success": true,
    "reason": "[MOCK] GaussFilter 占位输出，未做真实高斯滤波",
    "outputImagePath": "<临时文件路径>",
    "data": {
        "isMock": true,
        "mockTag": "GaussFilter",
        "inputSize": "640x480",
        "params": { "kernelSize": 5, "sigma": 1.0 }
    }
}
```

---

## 5. 算子模板生成脚本

### 5.1 路径

`tools/operator_template_gen.py`

### 5.2 功能

- 输入：算子 type 名 + cnName + 类别 + 参数列表 + 模式（mock / real）
- 输出：在 `src/operators/extensions/<type>/` 下生成完整目录骨架
  - `CMakeLists.txt`（基于模板，自动填入算子名）
  - `manifest.json`（基于输入参数生成）
  - `<Type>Operator.h` / `.cpp`（mock 模式继承 MockOperatorBase；real 模式继承 IOperator）

### 5.3 用法示例

```bash
# 生成 mock 算子骨架
python tools/operator_template_gen.py --type MockNewOp --cnName "新mock算子" --category "预处理" --mock

# 生成真实算子骨架
python tools/operator_template_gen.py --type NewFilter --cnName "新滤波器" --category "预处理" --real \
    --params '[{"name":"ksize","type":0,"defaultValue":5,"minValue":1,"maxValue":31}]'
```

---

## 6. AgentOperatorRouter 设计

### 6.1 路径

`src/Core/AgentOperatorRouter/`

### 6.2 接口

```cpp
struct OperatorAgentMapping {
    QString operatorType;   // 如 "GaussFilter"
    QString agentRole;      // 如 "Developer"
    int priority;           // 1=首选, 2=备选
};

class AgentOperatorRouter {
public:
    AgentOperatorRouter();
    
    // 静态映射：任务类型 → 算子+Agent角色列表
    QVector<OperatorAgentMapping> route(const QString& taskType) const;
    
    // 获取所有支持的任务类型
    QStringList supportedTaskTypes() const;
    
private:
    QMap<QString, QVector<OperatorAgentMapping>> m_routingTable;
    void initRoutingTable();
};
```

### 6.3 映射规则

任务类型参照 **Halcon 算子参考手册**的标准分类。映射基于算子 `manifest.json` 的 `category` 字段与 `.agent_mesh/roles/*.md` 的角色职责：

| 任务类型 | 优先算子 | Agent 角色 |
|---------|---------|-----------|
| 图像采集 | ReadImage, GrabImage, OpenFramegrabber | Developer |
| 滤波预处理 | GaussFilter, MeanImage, MedianImage, FftGeneric | Developer |
| 形态学 | Erosion, Dilation, Opening, Closing, TopHat, BottomHat | Developer |
| 图像分割 | Threshold, DynThreshold, Watershed, RegionGrowing | Developer |
| Blob 分析 | BlobDetect, Connection, ContourAnalyze, SelectShape | Reviewer |
| 特征提取 | PointsHarris, EdgesSubPix, LineCircleDetect | Developer |
| 匹配定位 | TemplateMatch, FindNccModel, FindShapeModel | Developer |
| 几何测量 | GeometryMeasure, DistancePp, AngleLl | Reviewer |
| 3D 视觉 | Reconstruct3D, BinocularDisparity, SurfaceMatching | Developer |
| 深度学习 | AiClassify, SegmentDl, DetectObjectsDl | Reviewer |
| 评审验证 | MockClassify, MockValidate | Gatekeeper |
| 审计检查 | MockAudit, MockPlan, MockCoordinate | SpecGuardian |

### 6.4 QML 暴露

通过 `qmlRegisterType` 注册为 QML 组件，供编辑模块调用 `router.route(taskType)` 获取推荐算子列表。

---

## 7. 文档体系

### 7.1 文档清单

| 文档 | 路径 | 生成方式 | 内容 |
|------|------|---------|------|
| 算子功能清单 | `docs/algorithms/算子功能清单.md` | 脚本自动生成 | 表格：type/cnName/category/参数/版本/是否mock |
| 算子技术文档 | `docs/algorithms/算子技术文档.md` | 脚本自动生成 | 每算子详细参数说明 + 输入输出格式 + 适用场景 |
| 算子开发指南 | `docs/algorithms/算子开发指南.md` | 手写 | SDK 架构 + manifest 规范 + 开发步骤 + 模板脚本 + 参考实现 |
| API 文档 | `docs/algorithms/api-html/` | Doxygen 生成 | OperatorSDK + Router 头文件 API 参考 |

### 7.2 文档生成脚本

**路径**：`tools/gen_operator_docs.py`

**功能**：
- 扫描 `src/operators/extensions/*/manifest.json` + `config/operators.json`
- 合并元数据（mock 算子标注 `[MOCK]`）
- 按类别分组输出 Markdown 表格与详情
- 支持 `--format md|html`

### 7.3 算子开发指南大纲

1. OperatorSDK 架构总览（IOperator / IInferenceEngine / IOperatorRegistry）
2. manifest.json 字段规范（type / version / cnName / category / iconPath / description / library / params / isMock）
3. 独立 dll 开发步骤（创建目录 → 编写 cpp → CMakeLists → 编译 → 部署）
4. 模板脚本使用（`operator_template_gen.py` 用法）
5. Histogram 参考实现解读（真实算子范例）
6. MockOperatorBase 使用（mock 算子范例）
7. 测试编写指南（参考 `tests/Operators/test_histogram.cpp`）
8. 常见问题与调试技巧

### 7.4 Doxygen 配置

- `Doxyfile` 扫描 `src/OperatorSDK/include/` 与 `src/Core/AgentOperatorRouter/`
- 头文件注释符合 Doxygen 规范（`/** */` + `@param` + `@return`）
- 输出 HTML 到 `docs/algorithms/api-html/`

---

## 8. 测试策略

### 8.1 测试清单

| 测试类型 | 文件 | 覆盖内容 | 用例数 |
|---------|------|---------|--------|
| Pilot 1 继承 | `tests/OperatorSDK/*` + `tests/Operators/test_histogram.cpp` + `tests/Vision/test_ai_classify_di.cpp` | 原有 15 条红队用例 | 15 |
| MockOperatorBase | `tests/Operators/test_mock_base.cpp` | 占位图生成 / 参数校验 / ToolResult 格式 | 5 |
| 32 mock 批量 | `tests/Operators/test_mock_operators.cpp` | 每个 mock 可加载 / 可执行 / 输出合法 | 3（参数化批量） |
| 5 Agent mock | `tests/Operators/test_agent_mock_operators.cpp` | Agent 角色 mock 行为 | 5 |
| AgentOperatorRouter | `tests/Core/test_agent_operator_router.cpp` | 12 类任务路由正确 / 空任务处理 / 优先级 | 8 |
| 模板脚本 | `tests/tools/test_template_gen.py` | 生成目录骨架完整性 | 3 |
| 文档脚本 | `tests/tools/test_doc_gen.py` | 文档生成完整性 | 2 |
| **合计新增** | | | **~26** |

### 8.2 测试工具

- C++ 测试：Catch2（沿用现有 `tests/catch2/catch2_minimal.hpp`）
- Python 脚本测试：pytest（新增 `tests/tools/` 目录）

---

## 9. 验收标准

### 9.1 技术层（确定性终门）

1. ✅ 主仓编译通过（含 OperatorSDK + 37 个 mock dll + Router）
2. ✅ 全部单元测试通过（原有 300 + 新增 ~26 = ~326）
3. ✅ `gatekeeper.ps1` 编译验证 + 单测验证全绿
4. ✅ 37 个 mock dll 均可被 `OperatorPluginLoader` 扫描加载并注册成功
5. ✅ AgentOperatorRouter 对 12 类任务返回非空映射

### 9.2 业务层

1. ✅ 启动 `QDetectVision.exe`，编辑模块算子库显示全部 47 + 5 = 52 个算子
2. ✅ 任意 mock 算子可拖拽到画布并执行，返回 `[MOCK]` 标注的 ToolResult
3. ✅ AgentOperatorRouter 可在 QML 中调用并返回推荐算子列表
4. ✅ 算子功能清单文档覆盖全部 52 个算子
5. ✅ 模板脚本可一键生成新算子骨架并编译通过

### 9.3 用户层（通用性骨架）

1. ✅ 骨架可被 Agent 协作框架的 6 角色调用
2. ✅ 新增算子仅需：模板脚本生成骨架 → 填写 manifest → 实现 execute → 编译部署，无需修改框架代码
3. ✅ 开发者可参照开发指南独立完成新算子开发

---

## 10. 风险与缓解

| 风险 | 缓解措施 |
|------|---------|
| 37 个 dll 编译耗时 | 并行编译 + mock cpp 极简（~30 行） |
| Pilot 1 合并冲突 | 先验证 worktree 全绿，合并后重跑全测 |
| Router 硬编码不可扩展 | 设计为单一 `initRoutingTable()` 方法，修改集中 |
| mock 算子与真实算子混淆 | manifest.json 增 `isMock: true` 字段 + ToolResult.reason 标 `[MOCK]` 前缀 |
| catch2 析构崩溃（Pilot 1 遗留） | 合并前修复，改用动态分配或 QScopedPointer |

---

## 11. 实施顺序建议

1. **Pilot 1 合并**（前置）
2. **MockOperatorBase 基类**（其他 mock 依赖）
3. **5 个新增典型 mock 算子**（验证骨架）
4. **32 个 mock 补全算子**（批量生成）
5. **AgentOperatorRouter**（依赖算子清单）
6. **模板生成脚本 + 文档生成脚本**（并行）
7. **测试编写**（伴随各模块）
8. **集成验证 + 验收**

---

## 12. 后续 Spec 依赖

本 Spec 完成后，为后续提供基础：

- **Spec 2**（算子排序 + 自定义算子体系）：
  - 依赖本 Spec 的 52 个算子作为排序对象
  - 依赖模板脚本作为自定义算子开发工具
  - 依赖 MockOperatorBase 作为自定义算子的简化路径
- **Spec 3**（接口优化 + 测试文档）：
  - 依赖本 Spec 的算子清单进行性能基准测试
  - 依赖本 Spec 的测试体系扩展覆盖率工具
  - 依赖本 Spec 的文档体系扩展性能参数文档
