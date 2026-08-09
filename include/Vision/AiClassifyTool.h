#ifndef AICLASSIFYTOOL_H
#define AICLASSIFYTOOL_H

#include "Core/VisionTool.h"
#include "OperatorSDK/IInferenceEngine.h"
#include <QJsonArray>
#include <QMap>
#include <QMutex>

class AiClassifyTool : public QDV::VisionTool {
public:
    AiClassifyTool();
    ~AiClassifyTool() override;

    QString type() const override { return "AiClassify"; }

    bool configure(const QJsonObject& params) override;
    bool execute(const cv::Mat& input, ToolResult& result) override;

    QJsonObject serialize() const override;
    bool deserialize(const QJsonObject& data) override;

    void setModelPath(const QString& path) { m_modelPath = path; }
    QString modelPath() const { return m_modelPath; }

    void setCategoryLabels(const QStringList& labels) { m_categoryLabels = labels; }
    QStringList categoryLabels() const { return m_categoryLabels; }

    void setConfidenceThreshold(double threshold) { m_confidenceThreshold = threshold; }
    double confidenceThreshold() const { return m_confidenceThreshold; }

    void setTopK(int k) { m_topK = k; }
    int topK() const { return m_topK; }

    void setInputSize(int width, int height) { m_inputWidth = width; m_inputHeight = height; }
    int inputWidth() const { return m_inputWidth; }
    int inputHeight() const { return m_inputHeight; }

    // Phase 1 依赖注入：通过 IInferenceEngine 接口注入 AI 引擎
    // 连续注入使用最后一次（RT-008）
    void setInferenceEngine(QDV::IInferenceEngine* engine) { m_engine = engine; }
    QDV::IInferenceEngine* inferenceEngine() const { return m_engine; }

    // v5.4 升级：输出开关配置接口
    // key = 输出字段名（classId/className/confidence/classArray/confidenceArray）
    // value = 是否启用该输出（true=写入 result.data，false=跳过）
    // 未配置的 key 默认视为 true（向后兼容）
    void setOutputConfig(const QMap<QString, bool>& config) { m_outputConfig = config; }
    QMap<QString, bool> outputConfig() const { return m_outputConfig; }

private:
    // 一致性修复：模型加载成功后，确保类别标签与训练推理模块一致。
    // 优先级：模型目录 labels.json（权威） > 用户手动配置 categoryLabels。
    bool ensureCategoryLabels();
    QDV::IInferenceEngine* m_engine = nullptr;
    QString m_modelPath;
    QStringList m_categoryLabels;
    double m_confidenceThreshold = 0.5;
    int m_topK = 3;
    int m_inputWidth = 224;
    int m_inputHeight = 224;
    bool m_warmedUp = false;
    // v5.4 升级：输出开关配置
    // 语义：key 为输出字段名（classId/className/confidence/classArray/confidenceArray），
    //       value 为是否启用该输出。访问时统一使用 m_outputConfig.value(key, true) 形式，
    //       即未显式配置的 key 视为启用（向后兼容旧节点）。
    // 来源：由 configure() 从 params["outputConfig"] 解析，或由 setOutputConfig() 直接注入。
    QMap<QString, bool> m_outputConfig;
    // RT-010 修复：保护 execute() 并发调用的线程安全
    // 防止 m_warmedUp 竞态条件（重复 loadModel）和 m_results 并发写入（QJsonObject detach 竞争）
    mutable QMutex m_execMutex;
};

#endif // AICLASSIFYTOOL_H