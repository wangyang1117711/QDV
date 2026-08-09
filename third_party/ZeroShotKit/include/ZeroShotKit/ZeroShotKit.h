#ifndef ZEROSHOTKIT_ZEROSHOTKIT_H
#define ZEROSHOTKIT_ZEROSHOTKIT_H

// ============================================================================
// ZeroShotKit 门面类
// 封装零样本功能的完整调用流程，简化集成
// ============================================================================

#include <QObject>
#include <QString>
#include <QStringList>
#include <QList>
#include <QFuture>
#include <QFutureWatcher>
#include <opencv2/opencv.hpp>
#include "ZeroShotKit/ZeroShotTypes.h"
#include "ZeroShotKit/ZeroShotEngine.h"

namespace zsu {

class PipelineEngine;
class EvaluationEngine;
class BadCaseRecorder;
class ModelNotesManager;

// ZeroShotKit 门面类
// 封装零样本功能的完整调用流程，简化集成
class Kit : public QObject {
    Q_OBJECT
public:
    explicit Kit(QObject* parent = nullptr);
    ~Kit();

    // --- 模型管理 ---
    bool loadModel(ZeroShotModelType type, const QString& modelPath);
    bool isModelLoaded() const;
    ZeroShotModelType modelType() const;
    // 最近一次加载失败的详细错误信息（含原因 + 建议），供 UI 展示
    QString lastError() const;

    // --- 配置 ---
    void setTextPrompts(const QStringList& prompts);
    QStringList textPrompts() const;
    void setAnomalyThreshold(float threshold);
    float anomalyThreshold() const;
    void setDetectionThreshold(float threshold);
    float detectionThreshold() const;
    void setStabilityConfig(const StabilityConfig& config);
    StabilityConfig stabilityConfig() const;

    // --- 同步推理 ---
    ZeroShotResult infer(const cv::Mat& image);
    QList<ZeroShotResult> inferBatch(const QList<cv::Mat>& images);

    // --- 异步推理 ---
    void inferAsync(const QString& imagePath);
    void inferBatchAsync(const QStringList& imagePaths);
    void cancel();
    bool isRunning() const;

    // --- PatchCore 正常样本管理 ---
    bool addNormalSample(const cv::Mat& image);
    bool removeLastNormalSample();
    void clearNormalSamples();
    int normalSampleCount() const;

    // --- 辅助组件访问 ---
    ZeroShotEngine* engine() const;
    PipelineEngine* pipeline() const;
    EvaluationEngine* evaluator() const;
    BadCaseRecorder* badCaseRecorder() const;
    ModelNotesManager* notesManager() const;

signals:
    void inferenceCompleted(const zsu::ZeroShotResult& result);
    void batchCompleted(const QList<zsu::ZeroShotResult>& results);
    void progressUpdated(int current, int total);
    void errorOccurred(const QString& message);

private:
    ZeroShotEngine* m_engine;
    PipelineEngine* m_pipeline;
    EvaluationEngine* m_evaluator;
    BadCaseRecorder* m_badCaseRecorder;
    ModelNotesManager* m_notesManager;

    QFutureWatcher<ZeroShotResult> m_singleWatcher;
    QFutureWatcher<QList<ZeroShotResult>> m_batchWatcher;
    std::atomic<bool> m_cancelFlag{false};

    // 读取图片（支持中文路径）
    static cv::Mat readImage(const QString& path);
};

} // namespace zsu

#endif // ZEROSHOTKIT_ZEROSHOTKIT_H
