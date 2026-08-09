#pragma once

#include "VisionTool.h"
#include <QFile>
#include <QFileInfo>
#include <QByteArray>
#include <opencv2/core/mat.hpp>
#include <opencv2/imgcodecs.hpp>

// 表面缺陷检测算子（P1-5a）
// 基于模板差分 + 形态约束 + 自适应阈值的表面缺陷检测。
// 底层复用 ImageArithmetic + RegionGrowing 思路：
//   1. 加载标准模板图（QFile + cv::imdecode 兼容中文路径），与 input 尺寸对齐
//   2. 两图差分 cv::absdiff → 灰度化 → 高斯模糊降噪
//   3. 自适应阈值化：threshold = mean + sensitivity * stddev
//   4. 形态学开运算去噪（cv::morphologyEx MORPH_OPEN）
//   5. findContours 找缺陷区域，按 [minDefectArea, maxDefectArea] 过滤
//   6. 统计缺陷数量、总面积、最大缺陷面积
//   7. overlayImage 在 input 上绘制缺陷区域（红色矩形框 + 轮廓填充）
class SurfaceDefectTool : public QDV::VisionTool {
public:
    SurfaceDefectTool();
    ~SurfaceDefectTool() override = default;

    QString type() const override { return "SurfaceDefect"; }

    bool configure(const QJsonObject& params) override;
    bool execute(const cv::Mat& input, ToolResult& result) override;
    QJsonObject serialize() const override;
    bool deserialize(const QJsonObject& data) override;

    // P1-3 typed ports：声明输入/输出端口
    QList<QDV::PortDescriptor> outputPorts() const override;
    QList<QDV::PortDescriptor> inputPorts() const override;

private:
    QString m_templatePath;          // 标准模板图路径
    cv::Mat m_template;              // 已加载的标准模板图（BGR）
    double  m_sensitivity = 3.0;     // 灵敏度（标准差倍数，越大越不敏感）
    double  m_minDefectArea = 50.0;  // 最小缺陷面积（像素²）
    double  m_maxDefectArea = 10000.0; // 最大缺陷面积
    int     m_morphKernelSize = 3;   // 形态学核大小
    int     m_blurSize = 3;          // 预处理高斯模糊核大小（奇数）

    // 加载标准模板图（QFile + cv::imdecode 兼容中文路径）
    bool loadTemplate();
};
