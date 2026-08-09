#pragma once

#include "VisionTool.h"
#include <QFile>
#include <QFileInfo>
#include <QByteArray>
#include <opencv2/core/mat.hpp>
#include <opencv2/imgcodecs.hpp>

// 颜色比对算子（P0-6c）
// 标准样片与输入图的颜色直方图比对，支持 HSV/RGB/Lab 多色彩空间。
// 流程：
//   1. 加载标准样片图（QFile + cv::imdecode 兼容中文路径）
//   2. 按 colorSpace 转换色彩空间（HSV: cv::COLOR_BGR2HSV, Lab: cv::COLOR_BGR2Lab）
//   3. 计算 input 与 template 的多通道直方图（cv::calcHist），归一化到 [0,1]
//   4. 用 cv::compareHist 比对，方法映射到 Correl/ChiSqr/Intersect/Bhattacharyya
//   5. Correl/Intersect 越大越好；ChiSqr/Bhattacharyya 越小越好
//   6. 统一转换为分数 score∈[0,1]，分数 >= threshold 则匹配
//   7. overlayImage 绘制 ROI 框与分数文字
class ColorMatchTool : public QDV::VisionTool {
public:
    ColorMatchTool();
    ~ColorMatchTool() override = default;

    QString type() const override { return "ColorMatch"; }

    bool configure(const QJsonObject& params) override;
    bool execute(const cv::Mat& input, ToolResult& result) override;
    QJsonObject serialize() const override;
    bool deserialize(const QJsonObject& data) override;

    // P1-3 typed ports：声明输入/输出端口
    QList<QDV::PortDescriptor> outputPorts() const override;
    QList<QDV::PortDescriptor> inputPorts() const override;

private:
    QString m_templatePath;          // 标准样片图路径
    cv::Mat m_template;              // 已加载的标准样片图（BGR）
    QString m_colorSpace = "HSV";    // 比对色彩空间："HSV"/"RGB"/"Lab"
    QString m_histMethod = "Correl"; // 直方图比对方法："Correl"/"ChiSqr"/"Intersect"/"Bhattacharyya"
    double  m_threshold = 0.8;       // 匹配阈值 [0,1]
    int     m_bins = 32;             // 直方图每通道 bin 数
    int     m_roiX = 0;              // 比对 ROI 左上 X（0 表示全图）
    int     m_roiY = 0;              // 比对 ROI 左上 Y
    int     m_roiW = 0;              // 比对 ROI 宽（0 表示全图宽）
    int     m_roiH = 0;              // 比对 ROI 高（0 表示全图高）

    // 加载标准样片图（QFile + cv::imdecode 兼容中文路径）
    bool loadTemplate();

    // 将方法字符串映射为 cv::HISTCMP_* 枚举
    int histMethodCode() const;

    // 按色彩空间转换图像。RGB 不转换（直接用 BGR 三通道）；HSV/Lab 做 cvtColor
    cv::Mat convertColorSpace(const cv::Mat& bgr) const;

    // 计算多通道直方图并归一化到 [0,1]
    cv::Mat computeHist(const cv::Mat& img) const;

    // 比对方法是否为"越大越好"
    bool isHigherBetter() const;

    // 将原始距离转换为 [0,1] 分数
    double toScore(double distance) const;
};
