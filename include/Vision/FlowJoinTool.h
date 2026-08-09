#ifndef QDV_FLOW_JOIN_TOOL_H
#define QDV_FLOW_JOIN_TOOL_H

#include "Core/VisionTool.h"

namespace QDV {

/**
 * @brief 流程合并/等待算子（v2.7.0 - 海康VM对齐）
 *
 * 功能：
 * 1. 同步多个并行执行的流程分支，等待所有前置分支全部执行完成后，再触发后续节点运行
 * 2. 典型场景："图像采集预处理"和"从数据库读取产品参数"两个并行分支，
 *    全部执行完毕后才启动正式检测
 *
 * 实现方式：
 * - 算子本身不执行图像处理，仅作为同步点
 * - ToolChainExecutor 在执行到 FlowJoin 时，检查 parallelBranchStatus
 * - 若所有分支完成，透传最后一个分支的结果；否则等待
 *
 * 注意：当前实现为简化版，实际并行分支管理由 ToolChainExecutor 编排
 */
class FlowJoinTool : public VisionTool {
public:
    explicit FlowJoinTool();
    ~FlowJoinTool() override = default;

    QString type() const override { return "FlowJoin"; }
    // 注意：基类 VisionTool::name() 非虚函数，此处用 hide 而非 override
    using VisionTool::name;

    bool configure(const QJsonObject& params) override;
    bool execute(const cv::Mat& input, ToolResult& result) override;
    QJsonObject serialize() const override;
    bool deserialize(const QJsonObject& data) override;

    // P1-3 typed ports
    QList<PortDescriptor> outputPorts() const override;
    QList<PortDescriptor> inputPorts() const override;

private:
    /// 等待的分支 ID 列表（逗号分隔，如 "branch1,branch2,branch3"）
    QString m_waitBranches;
    /// 超时时间（毫秒），默认 5000ms
    int m_timeoutMs = 5000;
};

} // namespace QDV

#endif // QDV_FLOW_JOIN_TOOL_H
