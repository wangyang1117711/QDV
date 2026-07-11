#ifndef FINDSHAPEMODELTOOL_H
#define FINDSHAPEMODELTOOL_H

#include "VisionTool.h"
#include <QFile>
#include <QFileInfo>
#include <QByteArray>
#include <opencv2/core/mat.hpp>
#include <opencv2/imgcodecs.hpp>

// 形状匹配算子
// 将模板在 [angleStart, angleStart+angleExtent] 范围内按一定步长旋转,
// 对每个旋转模板做 NCC 匹配, 取最高得分作为该位置的最优角度,
// 再用 NMS 去重得到最终匹配列表
class FindShapeModelTool : public QDV::VisionTool {
public:
    FindShapeModelTool();

    QString type() const override { return "FindShapeModel"; }

    bool configure(const QJsonObject& params) override;
    bool execute(const cv::Mat& input, ToolResult& result) override;
    QJsonObject serialize() const override;
    bool deserialize(const QJsonObject& data) override;

private:
    QString m_templatePath;        // 模板图路径
    cv::Mat m_template;            // 已加载的灰度模板
    double m_threshold = 0.7;      // 匹配阈值 [0,1]
    double m_angleStart = 0.0;     // 起始角度(度)
    double m_angleExtent = 360.0;  // 角度范围(度)
    int m_maxMatches = 10;         // 最大匹配数

    // 加载模板图(使用 QFile+cv::imdecode 兼容中文路径)
    bool loadTemplate();

    // 将模板按指定角度(度)绕中心旋转,返回旋转后的图像
    cv::Mat rotateTemplate(const cv::Mat& tpl, double angleDeg) const;
};

#endif // FINDSHAPEMODELTOOL_H
