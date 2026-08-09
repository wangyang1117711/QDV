#ifndef BRANCHCONTROLTOOL_H
#define BRANCHCONTROLTOOL_H

#include "VisionTool.h"
#include "BranchNode.h"

class BranchControlTool : public QDV::VisionTool {
public:
    BranchControlTool() = default;
    ~BranchControlTool() override = default;

    QString type() const override { return "BranchControl"; }
    bool configure(const QJsonObject& params) override;
    bool execute(const cv::Mat& input, ToolResult& result) override;
    QJsonObject serialize() const override;
    bool deserialize(const QJsonObject& data) override;

    // P1-3 typed ports
    QList<QDV::PortDescriptor> outputPorts() const override;
    QList<QDV::PortDescriptor> inputPorts() const override;

    void setBranchNode(BranchNode* node);
    BranchNode* branchNode() const { return m_branchNode; }

private:
    // P1-4c 扩展：支持新比较算子 ge/le/ne/contains/startswith/endswith/in
    // 基于上游传入的 result 数据与 m_branchNode->conditionValue 比较
    // 原有算子（==/!=/>/>=</<=/ok/ng）保持向后兼容，交给 BranchNode::evaluate
    bool evaluateCondition(const ToolResult& result) const;

    // v2.7.0：逻辑运算辅助方法
    // 解析逻辑运算输入列表 "nodeId1:expectedBool1,nodeId2:expectedBool2"
    QStringList parseLogicInputs(const QString& inputs) const;
    // 评估单个逻辑输入 "nodeId:expectedBool"，从 result.data["logicResults"] 查找并比较
    bool evaluateLogicInput(const QString& input, const ToolResult& result) const;

    BranchNode* m_branchNode = nullptr;
    cv::Mat m_inputImage;

    /// v2.7.0：多分支选择模式参数
    /// switchValue：switch 模式的输入值（整数索引）
    /// branchCases：分支列表，格式 "case1:toolId1,toolId2;case2:toolId3"
    QString m_switchValue;
    QString m_branchCases;

    /// v2.7.0：逻辑运算参数
    /// logicInputs：逻辑运算的输入列表，格式 "cond1,cond2"（每个 cond 为 "nodeId:expectedBool"）
    /// 如 "node1:true,node2:false" 表示 node1 结果应为 true 且 node2 应为 false
    QString m_logicInputs;
};

#endif // BRANCHCONTROLTOOL_H