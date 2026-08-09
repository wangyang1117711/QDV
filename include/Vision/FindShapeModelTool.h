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
// P0-6a 扩展：支持各向异性 ScaleX/ScaleY 尺度搜索，对齐 VM D.2
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

    // P0-6a 各向异性尺度搜索参数
    // 当 scaleMin==scaleMax==1.0 且 !m_anisotropyEnabled 时，退化为原行为（不搜索尺度）
    double m_scaleMin = 1.0;           // 最小统一尺度
    double m_scaleMax = 1.0;           // 最大统一尺度
    double m_scaleStep = 0.05;         // 尺度步长
    bool   m_anisotropyEnabled = false;// 是否启用各向异性（ScaleX ≠ ScaleY）
    double m_scaleXRatio = 1.0;        // 各向异性时 ScaleX = scale * ratio
    double m_scaleYRatio = 1.0;        // 各向异性时 ScaleY = scale * ratio
    // 各向异性 ratio 搜索范围（当 anisotropyEnabled 且 ratioMin<ratioMax 时枚举）
    double m_ratioMin = 1.0;           // ScaleX/ScaleY 比率搜索下限
    double m_ratioMax = 1.0;           // ScaleX/ScaleY 比率搜索上限
    double m_ratioStep = 0.1;          // 比率搜索步长

    // 加载模板图(使用 QFile+cv::imdecode 兼容中文路径)
    bool loadTemplate();

    // 将模板按指定角度(度)绕中心旋转,返回旋转后的图像
    cv::Mat rotateTemplate(const cv::Mat& tpl, double angleDeg) const;

    // P0-6a：各向异性缩放模板，scaleX/scaleY 独立
    // 返回缩放后的灰度模板；尺度为 1.0 时返回克隆
    cv::Mat scaleTemplate(const cv::Mat& tpl, double scaleX, double scaleY) const;
};

#endif // FINDSHAPEMODELTOOL_H
