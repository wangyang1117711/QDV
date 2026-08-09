#ifndef QDV_EXECUTION_CONTEXT_H
#define QDV_EXECUTION_CONTEXT_H

#include <QVariantMap>
#include <QMap>
#include <QList>
#include <QString>
#include <QVariant>
#include <opencv2/core.hpp>

// 前向声明：ToolResult 实际定义在全局命名空间（VisionTool.h）
// 注意：不能放在 QDV 命名空间内，否则会与全局 ::ToolResult 产生歧义
struct ToolResult;

namespace QDV {

/**
 * @brief 执行上下文（v2.7.0 - 海康VM对齐升级）
 *
 * 在 ToolChainExecutor::execute 期间贯穿整个执行链，为算子提供：
 * 1. 跨算子数据传递（替代仅靠 overlayImage 链式传递的局限）
 * 2. 循环迭代变量（当前索引、当前 ROI、迭代历史）
 * 3. 分支路由信息（条件评估结果、已执行分支）
 * 4. 并行分支同步点（FlowJoin 等待的分支完成状态）
 *
 * 设计要点：
 * - 线程安全：ToolChainExecutor 在子线程 execute 时，context 为栈对象，
 *   不跨线程共享；并行分支各自拷贝独立 context
 * - 轻量级：仅持有指针和 QVariant，不拷贝 cv::Mat
 * - 向后兼容：旧算子不消费 context，execute 签名不变时无感知
 */
struct ExecutionContext {
    /// 当前迭代的索引（从 0 开始，非循环场景为 0）
    int iterationIndex = 0;

    /// 当前迭代的 ROI（循环遍历场景，子链切片用）
    /// 格式：QVariantMap {x, y, w, h}
    QVariant currentRoi;

    /// 循环总迭代数（非循环场景为 1）
    int totalIterations = 1;

    /// 上游所有算子的结果（按 toolId 索引），供下游算子查询
    /// 注意：非拥有指针，指向 ToolChainExecutor::m_results 中的元素
    const QMap<QString, ToolResult>* upstreamResults = nullptr;

    /// 子链每次迭代的结果（聚合用，按迭代索引顺序）
    /// 仅在子链循环执行后填充
    QList<QVariantMap> iterationResults;

    /// 当前所属的父 LoopTool 节点 ID（非循环场景为空）
    QString parentLoopId;

    /// 并行分支同步状态：FlowJoin 等待的分支 ID → 是否已完成
    /// FlowJoin 算子检查此字段判断是否继续等待
    QMap<QString, bool> parallelBranchStatus;

    /// 递归深度（子链嵌套场景，防止无限递归）
    int depth = 0;

    /// 最大递归深度（默认 5，对齐 AGENTS.md III.2 步数限制）
    static constexpr int MAX_DEPTH = 5;

    /// 是否允许继续递归（深度检查）
    bool canRecurse() const { return depth < MAX_DEPTH; }

    /// 创建子上下文（用于子链迭代）
    ExecutionContext createChild(int iterIdx, int total, const QVariant& roi) const {
        ExecutionContext child;
        child.iterationIndex = iterIdx;
        child.totalIterations = total;
        child.currentRoi = roi;
        child.upstreamResults = upstreamResults;
        child.parentLoopId = parentLoopId;
        child.depth = depth + 1;
        return child;
    }
};

} // namespace QDV

#endif // QDV_EXECUTION_CONTEXT_H
