// ============================================================================
// ZeroShotKit 门面类实现
// 封装零样本功能的完整调用流程，简化集成
// ============================================================================

#include "ZeroShotKit/ZeroShotKit.h"
#include "ZeroShotKit/Logger.h"
#include "PipelineEngine.h"
#include "EvaluationEngine.h"
#include "BadCaseRecorder.h"
#include "ModelNotesManager.h"

#include <QtConcurrent>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>

namespace zsu {

// ============================================================================
// 构造与析构
// ============================================================================

Kit::Kit(QObject* parent)
    : QObject(parent)
{
    // 创建各子组件，parent=this 确保自动释放
    m_engine          = new ZeroShotEngine(this);
    m_pipeline        = new PipelineEngine(this);
    m_evaluator       = new EvaluationEngine(this);
    m_badCaseRecorder = new BadCaseRecorder(this);
    m_notesManager    = new ModelNotesManager(this);

    // 绑定流水线引擎到零样本引擎（三个阶段共用同一引擎实例）
    m_pipeline->setAnomalyEngine(m_engine);
    m_pipeline->setDetectionEngine(m_engine);
    m_pipeline->setSegmentEngine(m_engine);

    // 异步单张推理完成 → 在主线程发射信号
    connect(&m_singleWatcher, &QFutureWatcher<ZeroShotResult>::finished, this, [this]() {
        ZeroShotResult result = m_singleWatcher.result();
        emit inferenceCompleted(result);
    });

    // 异步批量推理完成 → 在主线程发射信号
    connect(&m_batchWatcher, &QFutureWatcher<QList<ZeroShotResult>>::finished, this, [this]() {
        QList<ZeroShotResult> results = m_batchWatcher.result();
        emit batchCompleted(results);
    });
}

Kit::~Kit()
{
    // 子组件由 parent 机制自动释放，无需手动 delete
}

// ============================================================================
// 模型管理
// ============================================================================

bool Kit::loadModel(ZeroShotModelType type, const QString& modelPath)
{
    return m_engine->loadModel(type, modelPath);
}

bool Kit::isModelLoaded() const
{
    return m_engine->isModelLoaded();
}

ZeroShotModelType Kit::modelType() const
{
    return m_engine->modelType();
}

QString Kit::lastError() const
{
    return m_engine->lastError();
}

// ============================================================================
// 配置
// ============================================================================

void Kit::setTextPrompts(const QStringList& prompts)
{
    m_engine->setTextPrompts(prompts);
}

QStringList Kit::textPrompts() const
{
    return m_engine->textPrompts();
}

void Kit::setAnomalyThreshold(float threshold)
{
    m_engine->setAnomalyThreshold(threshold);
}

float Kit::anomalyThreshold() const
{
    return m_engine->anomalyThreshold();
}

void Kit::setDetectionThreshold(float threshold)
{
    m_engine->setDetectionThreshold(threshold);
}

float Kit::detectionThreshold() const
{
    return m_engine->detectionThreshold();
}

void Kit::setStabilityConfig(const StabilityConfig& config)
{
    m_engine->setStabilityConfig(config);
}

StabilityConfig Kit::stabilityConfig() const
{
    return m_engine->stabilityConfig();
}

// ============================================================================
// 同步推理
// ============================================================================

ZeroShotResult Kit::infer(const cv::Mat& image)
{
    // 根据稳定性配置选择推理方式
    if (m_engine->stabilityConfig().enableMultiRunStability) {
        return m_engine->inferStable(image);
    }
    return m_engine->infer(image);
}

QList<ZeroShotResult> Kit::inferBatch(const QList<cv::Mat>& images)
{
    QList<ZeroShotResult> results;
    results.reserve(images.size());
    for (const auto& img : images) {
        if (m_cancelFlag.load(std::memory_order_acquire)) {
            break;
        }
        results.append(infer(img));
    }
    return results;
}

// ============================================================================
// 异步推理
// ============================================================================

void Kit::inferAsync(const QString& imagePath)
{
    m_cancelFlag.store(false, std::memory_order_release);

    // 使用 QtConcurrent 在工作线程执行推理
    m_singleWatcher.setFuture(QtConcurrent::run([this, imagePath]() -> ZeroShotResult {
        cv::Mat img = readImage(imagePath);
        if (img.empty()) {
            ZeroShotResult result;
            result.success = false;
            result.errorMessage = QString::fromUtf8("无法读取图像: %1").arg(imagePath);

            // 在主线程发射错误信号
            QMetaObject::invokeMethod(this, [this, msg = result.errorMessage]() {
                emit errorOccurred(msg);
            }, Qt::QueuedConnection);
            return result;
        }
        return infer(img);
    }));
}

void Kit::inferBatchAsync(const QStringList& imagePaths)
{
    m_cancelFlag.store(false, std::memory_order_release);

    m_batchWatcher.setFuture(QtConcurrent::run([this, imagePaths]() -> QList<ZeroShotResult> {
        QList<ZeroShotResult> results;
        results.reserve(imagePaths.size());
        int total = imagePaths.size();

        for (int i = 0; i < total; ++i) {
            if (m_cancelFlag.load(std::memory_order_acquire)) {
                break;
            }

            cv::Mat img = readImage(imagePaths[i]);
            if (img.empty()) {
                ZeroShotResult result;
                result.success = false;
                result.errorMessage = QString::fromUtf8("无法读取图像: %1").arg(imagePaths[i]);
                results.append(result);
            } else {
                results.append(infer(img));
            }

            // 在主线程发射进度信号
            int current = i + 1;
            QMetaObject::invokeMethod(this, [this, current, total]() {
                emit progressUpdated(current, total);
            }, Qt::QueuedConnection);
        }
        return results;
    }));
}

void Kit::cancel()
{
    m_cancelFlag.store(true, std::memory_order_release);
}

bool Kit::isRunning() const
{
    return m_singleWatcher.isRunning() || m_batchWatcher.isRunning();
}

// ============================================================================
// PatchCore 正常样本管理
// ============================================================================

bool Kit::addNormalSample(const cv::Mat& image)
{
    return m_engine->addNormalSample(image);
}

bool Kit::removeLastNormalSample()
{
    return m_engine->removeLastNormalSample();
}

void Kit::clearNormalSamples()
{
    m_engine->clearNormalSamples();
}

int Kit::normalSampleCount() const
{
    return m_engine->normalSampleCount();
}

// ============================================================================
// 辅助组件访问
// ============================================================================

ZeroShotEngine* Kit::engine() const
{
    return m_engine;
}

PipelineEngine* Kit::pipeline() const
{
    return m_pipeline;
}

EvaluationEngine* Kit::evaluator() const
{
    return m_evaluator;
}

BadCaseRecorder* Kit::badCaseRecorder() const
{
    return m_badCaseRecorder;
}

ModelNotesManager* Kit::notesManager() const
{
    return m_notesManager;
}

// ============================================================================
// 读取图片（支持中文路径）
// 使用 QFile 读取字节流 + cv::imdecode 解码，规避 cv::imread 不支持中文路径的问题
// ============================================================================

cv::Mat Kit::readImage(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        ZSU_LOG_ERROR(QString("Kit: 无法打开图片文件: %1").arg(path));
        return cv::Mat();
    }
    QByteArray data = file.readAll();
    file.close();

    cv::Mat img = cv::imdecode(std::vector<uchar>(data.begin(), data.end()), cv::IMREAD_COLOR);
    if (img.empty()) {
        ZSU_LOG_WARN(QString("Kit: 图片解码失败: %1").arg(path));
    }
    return img;
}

} // namespace zsu
