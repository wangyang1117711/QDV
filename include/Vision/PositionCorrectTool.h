#pragma once

#include "VisionTool.h"

// 位置修正算子（P0-2）
// 输入上游匹配位姿 Pose(X,Y,Angle,Scale)，计算 2D 仿射变换矩阵，
// 输出矩阵供下游算子做坐标映射。
// 设计思路：以标准参考点 (refX,refY) 为基准，构建"标准坐标 → 实际坐标"
// 的 2x3 仿射矩阵（平移 + 旋转 + X/Y 独立缩放），
// 使 (refX,refY) 经该矩阵映射到 (poseX,poseY)。
class PositionCorrectTool : public QDV::VisionTool {
public:
    PositionCorrectTool();

    QString type() const override { return "PositionCorrect"; }

    bool configure(const QJsonObject& params) override;
    bool execute(const cv::Mat& input, ToolResult& result) override;
    QJsonObject serialize() const override;
    bool deserialize(const QJsonObject& data) override;

    // P1-3 typed ports：声明输入/输出端口元数据
    QList<QDV::PortDescriptor> outputPorts() const override;
    QList<QDV::PortDescriptor> inputPorts() const override;

private:
    // 上游匹配位姿（实际值）
    double m_poseX = 0.0;       // 参考点 X 坐标（像素）
    double m_poseY = 0.0;       // 参考点 Y 坐标（像素）
    double m_poseAngle = 0.0;   // 旋转角度（度）
    double m_poseScaleX = 1.0;  // X 缩放
    double m_poseScaleY = 1.0;  // Y 缩放
    // 标准参考点（标定时的基准）
    double m_refX = 0.0;        // 标准参考点 X
    double m_refY = 0.0;        // 标准参考点 Y
};
