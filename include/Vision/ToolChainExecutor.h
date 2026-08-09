#ifndef TOOLCHAINEXECUTOR_H
#define TOOLCHAINEXECUTOR_H

#include <QObject>
#include <QList>
#include <QMap>
#include <QThread>
#include <QMutex>
#include <QFuture>
#include <atomic>
#include "VisionTool.h"
#include "BranchNode.h"
#include "ExecutionContext.h"

// v2.7.0 架构升级（海康VM对齐）：
// ToolChainExecutor 从纯线性执行器升级为支持以下能力的编排引擎：
// 1. 子链循环执行：LoopTool 输出 ROI 列表后，对每个 ROI 切片执行子链
// 2. trueBranch/falseBranch 路由：BranchControlTool 评估条件后选择分支
// 3. 多分支选择：基于整数输入匹配分支序号（switch/case 语义）
// 4. 并行分支 + FlowJoin 同步：多分支并行执行，FlowJoin 等待全部完成
// 5. ExecutionContext：贯穿执行链的上下文对象，支持跨算子数据传递
//
// P1-B12 架构约束（继承）：
// 各 VisionTool 实例的内部状态非线程安全。
// 并行分支执行时，必须为每条分支创建独立的 Tool 实例（通过 ToolFactory::createTool）。
class ToolChainExecutor : public QObject {
    Q_OBJECT

public:
    explicit ToolChainExecutor(QObject* parent = nullptr);

    void setTools(const QList<QDV::VisionTool*>& tools);
    void setBranches(const QMap<QString, BranchNode*>& branches);

    /// v2.7.0：设置子链映射（key=LoopTool 的 toolId, value=子链算子列表）
    /// 子链算子为 NON-OWNING 指针，由 Scheme 持有
    void setSubChains(const QMap<QString, QList<QDV::VisionTool*>>& subChains);

    /// v2.7.0：设置并行分支映射（key=分支起始 toolId, value=分支算子列表）
    /// 用于 FlowJoin 算子的并行分支同步
    void setParallelBranches(const QMap<QString, QList<QDV::VisionTool*>>& branches);

    bool execute(const cv::Mat& input);
    QFuture<bool> executeAsync(const cv::Mat& input);
    void stop();

    // === 端口级多输入数据流执行（spec：editor-output-connection-optimization） ===
    /// 一条端口绑定（上游输出端口 → 下游输入端口）
    struct FlowBinding {
        QString upstreamToolId;    ///< 上游算子 ID
        QString upstreamPort;      ///< 上游输出端口名
        QString downstreamToolId;  ///< 下游算子 ID
        QString downstreamPort;    ///< 下游输入端口名
    };
    /// 设置端口绑定（执行前调用；executeWithFlow 据此构建依赖图）
    void setFlowBindings(const QList<FlowBinding>& bindings);
    /// 按端口绑定执行多输入数据流（拓扑序：每个算子仅在其全部上游执行后运行）
    /// @param primaryInput 初始输入图像（无上游绑定算子的输入源）
    bool executeWithFlow(const cv::Mat& primaryInput);
    /// 查询某算子在 flow 模式下合并到的上游输入数据（key=下游端口名）
    /// 图像仍走 overlayImage（execute() 的 currentInput 语义）；此处为非图像 typed 数据
    QVariantMap flowInputsFor(const QString& toolId) const;

    QMap<QString, ToolResult> getResults() const { return m_results; }
    ToolResult getResult(const QString& toolId) const;

    /// v2.7.0：获取子链迭代结果（按迭代索引顺序）
    QList<ToolResult> getIterationResults(const QString& loopToolId) const;

signals:
    void toolExecuted(const QString& toolId, const ToolResult& result);
    void chainCompleted(bool success);
    void executionProgress(int current, int total);
    /// P1-A2 修复：单个工具执行失败信号（供上层桥接到 errorRaised）
    void toolFailed(const QString& toolId, const QString& errorMessage);
    /// v2.7.0：子链迭代开始/完成信号
    void subChainIterationStarted(const QString& loopToolId, int index, int total);
    void subChainIterationCompleted(const QString& loopToolId, int index, const ToolResult& result);

private:
    // NON-OWNING: tools are owned by Scheme. Do not delete.
    QList<QDV::VisionTool*> m_tools;
    QMap<QString, BranchNode*> m_branches;
    QMap<QString, ToolResult> m_results;
    std::atomic<bool> m_running{false};
    mutable QMutex m_mutex;

    /// v2.7.0：子链映射（LoopTool id → 子链算子列表）
    QMap<QString, QList<QDV::VisionTool*>> m_subChains;

    /// v2.7.0：并行分支映射（分支起始 toolId → 分支算子列表）
    QMap<QString, QList<QDV::VisionTool*>> m_parallelBranches;

    /// v2.7.0：子链迭代结果聚合（LoopTool id → 每次迭代结果列表）
    QMap<QString, QList<ToolResult>> m_iterationResults;

    /// 端口绑定（flow 模式）：下游 toolId → 该算子所有输入绑定
    QMap<QString, QList<FlowBinding>> m_flowBindings;
    /// flow 模式合并的上游输入数据：下游 toolId → {下游端口名: 上游typed数据}
    mutable QMap<QString, QVariantMap> m_flowInputs;

    bool executeTool(QDV::VisionTool* tool, const cv::Mat& input, ToolResult& result);
    bool evaluateBranch(const BranchNode* branch);
    QList<QString> getNextToolIds(QDV::VisionTool* currentTool);

    /// flow 模式：对工具集做依赖拓扑排序（依赖 = 端口绑定上游）
    /// @return 拓扑序 toolId 列表（存在环时返回空）
    QList<QString> topoSortFlow() const;
    /// flow 模式：执行单个节点（组装主图像 + 合并上游 typed 数据）
    bool executeFlowNode(QDV::VisionTool* tool, const cv::Mat& primaryInput,
                         ToolResult& result);

    /// v2.7.0：执行子链循环（对 ROI 列表逐个切片执行子链）
    /// @param loopTool LoopTool 算子实例
    /// @param input 原始输入图像
    /// @param loopResult LoopTool 的执行结果（含 roiList）
    /// @param ctx 执行上下文
    /// @return true=全部迭代成功；false=至少一次失败
    bool executeSubChain(QDV::VisionTool* loopTool, const cv::Mat& input,
                         ToolResult& loopResult, const QDV::ExecutionContext& ctx);

    /// v2.7.0：执行单条分支（线性顺序执行分支内的算子）
    /// @param branchTools 分支算子列表
    /// @param input 分支输入图像
    /// @param ctx 执行上下文
    /// @return 分支最后一个算子的结果
    ToolResult executeBranchSequence(const QList<QDV::VisionTool*>& branchTools,
                                      const cv::Mat& input,
                                      const QDV::ExecutionContext& ctx);

    /// v2.7.0：并行执行多分支并等待全部完成（FlowJoin 语义）
    /// @param branchIds 需要并行执行的分支起始 toolId 列表
    /// @param input 各分支的输入图像
    /// @param ctx 执行上下文
    /// @return true=所有分支完成；false=至少一条分支失败
    bool executeParallelBranches(const QList<QString>& branchIds,
                                  const cv::Mat& input,
                                  const QDV::ExecutionContext& ctx);
};

#endif // TOOLCHAINEXECUTOR_H