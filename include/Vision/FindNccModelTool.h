#ifndef FINDNCCMODELTOOL_H
#define FINDNCCMODELTOOL_H

#include "VisionTool.h"
#include <QFile>
#include <QFileInfo>
#include <QByteArray>
#include <opencv2/core/mat.hpp>
#include <opencv2/imgcodecs.hpp>

// 归一化互相关(NCC)模板匹配算子
// 使用 cv::matchTemplate(TM_CCOEFF_NORMED) 在原图中查找模板，
// 配合 NMS(非极大值抑制)去除重叠匹配,支持多目标定位
class FindNccModelTool : public QDV::VisionTool {
public:
    FindNccModelTool();

    QString type() const override { return "FindNccModel"; }

    bool configure(const QJsonObject& params) override;
    bool execute(const cv::Mat& input, ToolResult& result) override;
    QJsonObject serialize() const override;
    bool deserialize(const QJsonObject& data) override;

private:
    QString m_templatePath;        // 模板图路径
    cv::Mat m_template;            // 已加载的灰度模板
    double m_threshold = 0.8;      // 匹配阈值 [0,1]
    int m_maxMatches = 10;         // 最大匹配数 [1,1000]

    // 加载模板图(使用 QFile+cv::imdecode 兼容中文路径)
    bool loadTemplate();

    // 非极大值抑制: 从得分图中筛选满足阈值且不重叠的候选位置
    std::vector<cv::Rect> nonMaxSuppression(
        const cv::Mat& scoreMap,
        double threshold,
        int maxMatches,
        int templateW,
        int templateH,
        std::vector<double>& outScores) const;
};

#endif // FINDNCCMODELTOOL_H
