#pragma once

#include "Core/VisionTool.h"
#include <opencv2/dnn.hpp>
#include <QString>
#include <vector>

// 端到端文本检测+识别算子（P0-6d）
// 流程：先用 cv::dnn 做文本检测（EAST/DB 风格模型），再对每个文本区域做识别。
// 未配置检测模型或加载失败时，优雅降级到 MSER 候选区域检测。
// 未配置识别模型时，仅输出文本框并标记"待识别"。
class DLOCRTool : public QDV::VisionTool {
public:
    DLOCRTool();
    ~DLOCRTool() override = default;

    QString type() const override { return "DLOCR"; }

    bool configure(const QJsonObject& params) override;
    bool execute(const cv::Mat& input, ToolResult& result) override;
    QJsonObject serialize() const override;
    bool deserialize(const QJsonObject& data) override;

    // P1-3 typed ports：声明输入/输出端口元数据（连线期类型校验）
    QList<QDV::PortDescriptor> outputPorts() const override;
    QList<QDV::PortDescriptor> inputPorts() const override;

private:
    // ----- 参数 -----
    QString m_detectionModelPath;       // 文本检测模型路径（.onnx/.pb）
    QString m_recognitionModelPath;     // 文本识别模型路径（.onnx，可选）
    double  m_confThreshold  = 0.5;     // 检测置信度阈值
    double  m_nmsThreshold   = 0.4;     // NMS 阈值
    int     m_inputWidth     = 320;     // 检测输入宽
    int     m_inputHeight    = 320;     // 检测输入高
    QString m_language       = "chi_sim"; // 识别语言（chi_sim/eng）
    int     m_maxTextRegions = 20;      // 最大文本区域数

    // ----- 模型状态（懒加载） -----
    cv::dnn::Net m_detNet;              // 检测网络
    cv::dnn::Net m_recNet;              // 识别网络
    bool m_detModelLoaded = false;      // 检测模型是否已加载
    bool m_recModelLoaded = false;      // 识别模型是否已加载

    // 文本区域结构（检测输出 + 识别结果）
    struct TextRegion {
        cv::Rect rect;              // 外接矩形
        float    confidence = 0.0f; // 检测置信度
        QString  text;              // 识别文本（识别后填充）
        double   recConfidence = 0.0; // 识别置信度
    };

    // 懒加载检测/识别模型（用 QFile 读字节，兼容中文路径）
    bool loadDetectionModel();
    bool loadRecognitionModel();
    // dnn 检测文本区域（EAST 风格双输出 或 DB 风格单输出）
    bool detectByDnn(const cv::Mat& input, std::vector<TextRegion>& regions);
    // MSER 降级检测文本候选区域
    bool detectByMSER(const cv::Mat& input, std::vector<TextRegion>& regions);
    // dnn 识别单个文本区域（无 charset 时返回占位 + 置信度）
    QString recognizeByDnn(const cv::Mat& region, double& conf);
};
