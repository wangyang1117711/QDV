#pragma once

#include "VisionTool.h"
#include <QFile>
#include <QFileInfo>
#include <QByteArray>
#include <opencv2/core/mat.hpp>
#include <opencv2/imgcodecs.hpp>

// 轮廓匹配算子（P0-6b）
// 基于 cv::matchShapes + Hu 矩的抗形变轮廓形状匹配。
// 流程：
//   1. 加载模板图（QFile + cv::imdecode 兼容中文路径），提取最大轮廓作为模板轮廓
//   2. 对输入图二值化（OTSU）并 findContours
//   3. 对每个面积 >= minArea 的轮廓用 cv::matchShapes 计算与模板轮廓的距离
//   4. 距离越小越相似，分数 score = 1 / (1 + distance)（范围 [0,1]）
//   5. 按分数降序，取 score >= threshold 的前 maxMatches 个作为匹配结果
//   6. 在 overlayImage 上绘制匹配轮廓的外接矩形与分数文字
class ContourMatchTool : public QDV::VisionTool {
public:
    ContourMatchTool();
    ~ContourMatchTool() override = default;

    QString type() const override { return "ContourMatch"; }

    bool configure(const QJsonObject& params) override;
    bool execute(const cv::Mat& input, ToolResult& result) override;
    QJsonObject serialize() const override;
    bool deserialize(const QJsonObject& data) override;

    // P1-3 typed ports：声明输入/输出端口，供连线期类型校验
    QList<QDV::PortDescriptor> outputPorts() const override;
    QList<QDV::PortDescriptor> inputPorts() const override;

private:
    QString m_templatePath;          // 模板轮廓图路径
    cv::Mat m_template;              // 已加载的模板图（用于提取轮廓）
    std::vector<cv::Point> m_templateContour;  // 从模板图提取的最大轮廓（matchShapes 输入）
    double m_threshold = 0.8;        // 匹配分数阈值 [0,1]，越大越严格
    int    m_maxMatches = 10;        // 最大返回匹配数
    QString m_matchMethod = "I1";    // matchShapes 方法："I1"/"I2"/"I3"
    double m_minArea = 100.0;        // 最小轮廓面积过滤

    // 加载模板图并提取最大轮廓（使用 QFile+cv::imdecode 兼容中文路径）
    bool loadTemplate();

    // 将方法字符串映射为 cv::CONTOURS_MATCH_I1/I2/I3
    int matchMethodCode() const;
};
