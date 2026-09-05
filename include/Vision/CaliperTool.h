#pragma once

#include "VisionTool.h"

// 卡尺测量算子（P0-4）
// 双卡尺边缘对查找，输出亚像素间距。
// 复用 EdgesSubPix 的边缘提取思路：在 ROI 内沿测量方向投影 →
// 求梯度 → 按极性筛选边缘点 → 抛物线亚像素插值 → 取前两个边缘点间距。
// 测量方向自动判定：ROI 宽≥高时沿水平方向，否则沿垂直方向。
class CaliperTool : public QDV::VisionTool {
public:
    CaliperTool();

    QString type() const override { return "Caliper"; }

    bool configure(const QJsonObject& params) override;
    bool execute(const cv::Mat& input, ToolResult& result) override;
    QJsonObject serialize() const override;
    bool deserialize(const QJsonObject& data) override;

    // P1-3 typed ports：声明输入/输出端口元数据
    QList<QDV::PortDescriptor> outputPorts() const override;
    QList<QDV::PortDescriptor> inputPorts() const override;

private:
    int     m_roiX = 0;            // ROI 左上 X
    int     m_roiY = 0;            // ROI 左上 Y
    int     m_roiW = 100;          // ROI 宽
    int     m_roiH = 100;          // ROI 高
    int     m_searchWidth = 20;    // 卡尺搜索宽度（垂直于测量方向的搜索带）
    int     m_searchLength = 100;  // 卡尺搜索长度（沿测量方向）
    double  m_edgeThreshold = 30.0;// 边缘梯度阈值
    QString m_polarity = "any";    // 边缘极性 "any"/"dark_to_bright"/"bright_to_dark"
    double  m_smoothSigma = 1.0;   // 高斯平滑 sigma
    // P 优化：useSobel=true 用 Sobel 算子（更适合工业软边），false 用中心差分（保留兼容）
    bool    m_useSobel = true;
    // P 优化：自适应阈值降级（用户阈值找不到边缘时自动降阈值）
    bool    m_autoThreshold = true;
};
