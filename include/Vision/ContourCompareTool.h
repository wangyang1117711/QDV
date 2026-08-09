#pragma once

#include "VisionTool.h"
#include <QFile>
#include <QFileInfo>
#include <QByteArray>
#include <opencv2/core/mat.hpp>
#include <opencv2/imgcodecs.hpp>

// 轮廓比对算子（P1-5b）
// 实测轮廓 vs 标准轮廓偏差判定。
// 底层复用 EdgesSubPix 思路（Canny 边缘提取）+ cv::matchShapes（Hu 矩距离）。
// 判定逻辑：deviation <= maxDeviation 且 score >= minScore 时视为匹配。
//   - deviation：cv::matchShapes 原始返回值（距离，越小越相似）
//   - score：1/(1+deviation) 映射到 [0,1]（越大越相似）
class ContourCompareTool : public QDV::VisionTool {
public:
    ContourCompareTool();

    QString type() const override { return "ContourCompare"; }

    bool configure(const QJsonObject& params) override;
    bool execute(const cv::Mat& input, ToolResult& result) override;
    QJsonObject serialize() const override;
    bool deserialize(const QJsonObject& data) override;

    // P1-3 typed ports：声明输入/输出端口元数据
    QList<QDV::PortDescriptor> outputPorts() const override;
    QList<QDV::PortDescriptor> inputPorts() const override;

private:
    QString m_templatePath;        // 标准轮廓模板图路径
    cv::Mat m_template;            // 已加载的灰度模板（缓存，避免重复 IO）
    double  m_maxDeviation = 5.0;  // 最大允许偏差（像素）
    QString m_matchMethod = "I1"; // 匹配方法 "I1"/"I2"/"I3"
    double  m_minScore = 0.8;     // 最小匹配分数 [0,1]

    // 加载模板图（使用 QFile+cv::imdecode 兼容中文路径）
    bool loadTemplate();

    // 从灰度图提取最大轮廓（Canny + findContours）
    static std::vector<cv::Point> extractLargestContour(const cv::Mat& gray);

    // 将 "I1"/"I2"/"I3" 映射为 cv::matchShapes 方法枚举
    static int matchMethodFromString(const QString& m);
};
