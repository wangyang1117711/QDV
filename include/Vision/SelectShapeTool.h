#ifndef SELECTSHAPETOOL_H
#define SELECTSHAPETOOL_H

#include "VisionTool.h"

// 形状选择算子（HALCON select_shape 标准实现 v2 2026-07-09）
// 二值化后提取连通域轮廓，按 7 种形状特征之一筛选：
//   area/width/height/ratio/rectangularity/circularity/compactness
// 在 overlay 上绘制符合条件轮廓（绿色）及外接矩形（蓝色）。
//
// 与 operators.json 元数据严格匹配：
//   threshold  : 二值化阈值 [0,255]
//   featureType: 特征枚举
//   minValue   : 特征下限
//   maxValue   : 特征上限
class SelectShapeTool : public QDV::VisionTool {
public:
    SelectShapeTool();

    QString type() const override { return "SelectShape"; }

    bool configure(const QJsonObject& params) override;
    bool execute(const cv::Mat& input, ToolResult& result) override;
    QJsonObject serialize() const override;
    bool deserialize(const QJsonObject& data) override;

private:
    // 计算指定轮廓的特征值
    // @param contour 输入轮廓
    // @param area 轮廓面积（已计算，避免重复）
    // @param featureType 特征类型字符串
    // @return 特征值（无效类型返回 -1）
    static double computeFeature(const std::vector<cv::Point>& contour,
                                 double area,
                                 const QString& featureType);

    double m_threshold = 128.0;       // 二值化阈值
    QString m_featureType = "area";   // 特征类型
    double m_minValue = 100.0;        // 特征下限
    double m_maxValue = 1000000.0;    // 特征上限
};

#endif // SELECTSHAPETOOL_H
