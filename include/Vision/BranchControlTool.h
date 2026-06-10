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

    void setBranchNode(BranchNode* node);
    BranchNode* branchNode() const { return m_branchNode; }

private:
    BranchNode* m_branchNode = nullptr;
    cv::Mat m_inputImage;
};

#endif // BRANCHCONTROLTOOL_H