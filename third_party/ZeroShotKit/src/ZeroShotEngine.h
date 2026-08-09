#ifndef ZEROSHOTENGINE_H
#define ZEROSHOTENGINE_H

// ============================================================================
// 零样本推理引擎
//
// M1 阶段：占位定义，仅声明接口
// M2 阶段：实现 AnomalyCLIP 零样本异常检测（基于 CLIP ViT-B/32）
// M3 阶段：集成 Grounding DINO + MobileSAM
// M4 阶段：集成 PatchCore 渐进式切换
// ============================================================================

#include <QObject>
#include <QString>
#include <QStringList>
#include <QJsonObject>
#include <QSize>
#include <opencv2/opencv.hpp>
#include "ZeroShotTypes.h"
#include "ORTInferenceEngine.h"
#include <vector>

namespace zsu {

// 预计算的文本嵌入条目
struct TextEmbeddingEntry {
    QString prompt;         // 提示词文本
    bool isAnomaly;         // 是否为异常提示词
    std::vector<float> features;  // 特征向量（512维）
};

class ZeroShotEngine : public QObject {
    Q_OBJECT

public:
    explicit ZeroShotEngine(QObject* parent = nullptr);
    ~ZeroShotEngine();

    // --- 零样本模型加载 ---
    // modelType: AnomalyCLIP / GroundingDINO / MobileSAM / OpenCLIP / PatchCore
    // modelPath: 对于 AnomalyCLIP，指向 clip_vision_vit_b32.onnx 所在目录
    bool loadModel(ZeroShotModelType modelType, const QString& modelPath,
                   const QSize& inputSize = QSize(224, 224));

    // --- 文本提示配置 ---
    // AnomalyCLIP: "a photo of a normal {product}" / "a photo of a damaged {product}"
    // Grounding DINO: "scratch . dent . stain"
    void setTextPrompts(const QStringList& prompts) { m_textPrompts = prompts; }
    QStringList textPrompts() const { return m_textPrompts; }

    // --- 异常阈值 ---
    void setAnomalyThreshold(float t) { m_anomalyThreshold = t; }
    float anomalyThreshold() const { return m_anomalyThreshold; }

    // --- 检测阈值（Grounding DINO） ---
    void setDetectionThreshold(float t) { m_detectionThreshold = t; }
    float detectionThreshold() const { return m_detectionThreshold; }

    // --- 量化模型支持（M5-1） ---
    // 启用后，loadModel 会优先加载 _int8.onnx 量化版本
    void setUseQuantizedModel(bool enable) { m_useQuantized = enable; }
    bool useQuantizedModel() const { return m_useQuantized; }
    // 获取上一次模型加载是否使用了量化版本
    bool lastLoadUsedQuantized() const { return m_lastLoadQuantized; }

    // --- PatchCore 正常样本建模（M4） ---
    // 添加正常样本到 memory bank
    bool addNormalSample(const cv::Mat& image);
    // 从 memory bank 移除最后添加的样本
    bool removeLastNormalSample();
    // 清空 memory bank
    void clearNormalSamples();
    // 获取 memory bank 中的样本数
    int normalSampleCount() const { return static_cast<int>(m_patchCoreBank.size()); }
    // 设置渐进式切换阈值（当 memory bank 样本数超过此值时切换到 PatchCore）
    void setProgressiveSwitchThreshold(int n) { m_progressiveThreshold = n; }
    int progressiveSwitchThreshold() const { return m_progressiveThreshold; }

    // --- 零样本推理 ---
    // 输入图像，返回 ZeroShotResult（含异常分数/检测框/掩码/热力图）
    ZeroShotResult infer(const cv::Mat& input);

    // --- 稳定性推理（多次推理取稳定值） ---
    // 当 StabilityConfig.enableMultiRunStability 为 true 时，
    // 对同一张图推理多次，取检测框的交集/众数，保证结果稳定
    ZeroShotResult inferStable(const cv::Mat& input);

    // --- 稳定性配置 ---
    void setStabilityConfig(const StabilityConfig& config) { m_stabilityConfig = config; }
    StabilityConfig stabilityConfig() const { return m_stabilityConfig; }

    // --- NMS 去重（静态方法，供外部调用） ---
    // 对检测结果做非极大值抑制，去除 IoU > threshold 的重叠框
    static std::vector<ZeroShotResult::Detection> applyNMS(
        const std::vector<ZeroShotResult::Detection>& detections,
        float iouThreshold);

    // --- 模型状态 ---
    bool isModelLoaded() const { return m_modelLoaded; }
    ZeroShotModelType modelType() const { return m_modelType; }

    // --- 获取文本嵌入数量（调试用） ---
    int textEmbeddingCount() const { return static_cast<int>(m_textEmbeddings.size()); }

    // --- 最近一次加载失败的详细错误信息（含原因 + 改进建议） ---
    // 供 UI 层展示给用户，帮助定位问题。
    QString lastError() const { return m_lastError; }

signals:
    void inferenceCompleted(const ZeroShotResult& result);
    void stageProgress(const QString& stage, qint64 elapsedMs);

private:
    ZeroShotModelType m_modelType = ZeroShotModelType::Unknown;
    bool m_modelLoaded = false;
    QString m_lastError;              // 最近一次加载失败的详细错误信息（原因 + 建议）
    QStringList m_textPrompts;
    float m_anomalyThreshold = 0.5f;
    float m_detectionThreshold = 0.3f;  // Grounding DINO 检测阈值

    // M5-1: 量化模型支持
    bool m_useQuantized = false;          // 是否启用量化模型
    bool m_lastLoadQuantized = false;     // 上次加载是否使用了量化版本

    // CLIP 视觉编码器推理引擎（复用 ORTInferenceEngine）
    ORTInferenceEngine m_visionEngine;

    // Grounding DINO 检测引擎（M3）
    ORTInferenceEngine m_detectionEngine;
    bool m_detectionLoaded = false;

    // MobileSAM 分割引擎（M3）
    ORTInferenceEngine m_segmentEngine;
    bool m_segmentLoaded = false;

    // MobileSAM 编码器/解码器拆分引擎（M6: 真实 MobileSAM 模型）
    // 编码器: [1,3,1024,1024] -> [1,256,64,64] 图像特征
    // 解码器: image_embeddings + 点提示 -> 掩码/iou
    ORTInferenceEngine m_samEncoderEngine;
    ORTInferenceEngine m_samDecoderEngine;
    bool m_samEncoderLoaded = false;
    bool m_samDecoderLoaded = false;

    // 预计算的文本嵌入
    std::vector<TextEmbeddingEntry> m_textEmbeddings;
    int m_featureDim = 512;  // CLIP ViT-B/32 特征维度

    // CLIP 输入尺寸
    QSize m_clipInputSize = QSize(224, 224);

    // --- PatchCore 相关（M4） ---
    // Memory bank: 存储正常样本的特征向量
    struct PatchCoreEntry {
        std::vector<float> features;  // 特征向量（512维）
    };
    std::vector<PatchCoreEntry> m_patchCoreBank;
    int m_progressiveThreshold = 10;  // 渐进式切换阈值（默认10个样本）
    bool m_patchCoreReady = false;    // PatchCore 是否就绪

    // M2+ 阶段实现的子方法
    ZeroShotResult inferAnomalyCLIP(const cv::Mat& input);
    ZeroShotResult inferGroundingDINO(const cv::Mat& input);
    ZeroShotResult inferMobileSAM(const cv::Mat& input);
    ZeroShotResult inferOpenCLIP(const cv::Mat& input);
    ZeroShotResult inferPatchCore(const cv::Mat& input);

    // --- CLIP 辅助方法 ---
    bool loadTextEmbeddings(const QString& npzPath);
    cv::Mat preprocessForCLIP(const cv::Mat& input, const QSize& targetSize);
    static float cosineSimilarity(const std::vector<float>& a, const std::vector<float>& b);
    bool readNpzEmbeddings(const QString& path, std::vector<std::vector<float>>& embeddings,
                            std::vector<QString>& prompts, std::vector<bool>& isAnomaly);

    // --- Grounding DINO 辅助方法（M3） ---
    // 预处理：resize + 归一化 + NCHW
    cv::Mat preprocessForDetection(const cv::Mat& input, const QSize& targetSize);
    // 解析文本提示词为类别标签列表（"scratch . dent . stain" → ["scratch", "dent", "stain"]）
    QStringList parseTextPrompts(const QString& text) const;

    // --- 量化模型辅助方法（M5-1） ---
    // 根据原始模型路径推导量化版本路径（xxx.onnx → xxx_int8.onnx）
    QString resolveQuantizedPath(const QString& originalPath) const;
    // 尝试加载量化模型，失败则回退到原始模型
    QString resolveModelPath(const QString& originalPath);

    // --- MobileSAM 辅助方法（M3） ---
    // 预处理：resize 到 256x256 + 归一化 + NCHW
    cv::Mat preprocessForSegmentation(const cv::Mat& input, const QSize& targetSize);
    // 后处理：掩码二值化 + resize 回原图尺寸
    cv::Mat postprocessMask(const cv::Mat& mask, const cv::Size& originalSize, float threshold = 0.5f);

    // --- 稳定性配置 ---
    StabilityConfig m_stabilityConfig;

    // --- 计算两个检测框的 IoU ---
    static float computeIoU(const ZeroShotResult::Detection& a, const ZeroShotResult::Detection& b);
};

} // namespace zsu

#endif // ZEROSHOTENGINE_H
