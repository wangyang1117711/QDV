#ifndef ZEROSHOTDETECTTOOL_H
#define ZEROSHOTDETECTTOOL_H

// ============================================================================
// ZeroShotDetectTool — 零样本检测算子（spec v2 阶段三 Task 8）
// 将 ZeroShotKit 的 zsu::Kit 能力封装为 VisionTool，进入方案编排链。
// 可与 Loop（多 ROI 遍历）/Caliper（精确测量）等算子串联。
// LocateAnything 模型类型当前 zsu::Kit 不支持，execute() 返回明确"未实现"错误。
// ============================================================================

#include "Core/VisionTool.h"

#include <QString>
#include <QStringList>

// 前置声明 zsu::Kit，避免头文件强依赖 ZeroShotKit 实现头
namespace zsu { class Kit; }

class ZeroShotDetectTool : public QDV::VisionTool {
public:
    ZeroShotDetectTool();
    ~ZeroShotDetectTool() override;

    QString type() const override { return "ZeroShotDetect"; }

    bool configure(const QJsonObject& params) override;
    bool execute(const cv::Mat& input, ToolResult& result) override;

    // P1-3 typed ports：声明输入/输出端口元数据，供连线期类型校验
    QList<QDV::PortDescriptor> outputPorts() const override;
    QList<QDV::PortDescriptor> inputPorts() const override;

    QJsonObject serialize() const override;
    bool deserialize(const QJsonObject& data) override;

    // --- 参数访问器（供测试与序列化复用） ---
    void setModelType(const QString& type) { m_modelTypeStr = type; }
    QString modelType() const { return m_modelTypeStr; }

    void setTextPrompts(const QString& prompts) { m_textPrompts = prompts; }
    QString textPrompts() const { return m_textPrompts; }

    void setBoxThreshold(double threshold) { m_boxThreshold = threshold; }
    double boxThreshold() const { return m_boxThreshold; }

    void setDetectionMode(const QString& mode) { m_detectionMode = mode; }
    QString detectionMode() const { return m_detectionMode; }

    void setModelPath(const QString& path) { m_modelPath = path; }
    QString modelPath() const { return m_modelPath; }

private:
    // --- 配置参数 ---
    QString m_modelTypeStr   = QStringLiteral("AnomalyCLIP");  // 模型类型字符串
    QString m_textPrompts;                                      // 提示词串（如 "scratch . dent"）
    double  m_boxThreshold   = 0.25;                            // 检测框置信度阈值
    QString m_detectionMode  = QStringLiteral("detection");     // detection/rec/point
    QString m_modelPath;                                        // 模型目录路径

    // --- 运行时实例 ---
    // zsu::Kit 是 QObject，作为成员持有；模型加载后复用，避免每次 execute 重建。
    zsu::Kit* m_kit = nullptr;
    QString   m_loadedModelPath;       // 已加载模型的路径（用于检测路径变化时重新加载）
    QString   m_loadedModelTypeStr;    // 已加载模型的类型字符串

    // --- 内部辅助 ---
    // 将模型类型字符串映射到 zsu::ZeroShotModelType 枚举
    // 返回 zsu::ZeroShotModelType::Unknown 表示不支持（如 LocateAnything）
    static int modelTypeFromString(const QString& str);

    // 将提示词串拆分为 QStringList（按 " . " / "." / 空白 分隔）
    static QStringList splitPrompts(const QString& prompts);

    // 在 overlayImage 上绘制检测框（归一化坐标 → 像素坐标）
    void drawDetections(cv::Mat& overlay, const QList<QVariantMap>& detections) const;
};

#endif // ZEROSHOTDETECTTOOL_H
