#ifndef ORTINFERENCEENGINE_H
#define ORTINFERENCEENGINE_H

// ============================================================================
// M1 核心交付：ONNX Runtime 推理引擎
//
// 完整实现 ONNX Runtime C++ API 的模型加载、推理、后处理。
// 与主项目 InferenceEngine 接口兼容，合并时将本类的 runONNXRuntime()
// 逻辑移植回主项目 InferenceEngine::runONNXRuntime()。
//
// 支持的模型类型：
//   - ResNet 分类（2D 输出 [1, N]）
//   - YOLOv5 检测（3D 输出 [1, anchors, 5+nc]）
//   - YOLOv8 检测（3D 输出 [1, 4+nc, anchors]）
//   - 通用 4D 输出（仅输出 shape）
// ============================================================================

#include <QObject>
#include <QString>
#include <QStringList>
#include <QSize>
#include <QElapsedTimer>
#include <atomic>
#include <memory>
#include <opencv2/opencv.hpp>
#include <opencv2/dnn.hpp>

#include "ZeroShotTypes.h"

#ifdef ZSU_HAS_ORT
    #include "ORTCompat.h"   // MinGW SAL 注解兼容性
    #include <onnxruntime_cxx_api.h>
#endif

namespace zsu {

class ORTInferenceEngine : public QObject {
    Q_OBJECT

public:
    explicit ORTInferenceEngine(QObject* parent = nullptr);
    ~ORTInferenceEngine();

    // --- 后端配置 ---
    void setBackend(Backend backend) { m_backend = backend; }
    Backend backend() const { return m_backend; }
    static QStringList availableBackends();

    // --- 类别标签 ---
    void setClassLabels(const QStringList& labels) { m_classLabels = labels; }
    QStringList classLabels() const { return m_classLabels; }

    // --- YOLO 后处理阈值 ---
    void setConfThreshold(float t) { m_confThreshold = t; }
    float confThreshold() const { return m_confThreshold; }
    void setIoUThreshold(float t) { m_iouThreshold = t; }
    float ioUThreshold() const { return m_iouThreshold; }

    // --- ORT 会话配置 ---
    void setORTConfig(const ORTSessionConfig& config) { m_ortConfig = config; }
    ORTSessionConfig ortConfig() const { return m_ortConfig; }

    // --- 模型加载 ---
    // 加载 ONNX 模型文件，配置输入尺寸/均值/缩放/RB交换
    // skipForwardTest=true 时跳过前向测试（大模型首次 forward 可能 30+秒）
    bool loadModel(const QString& modelPath,
                   const QSize& inputSize = QSize(224, 224),
                   const cv::Scalar& mean = cv::Scalar(0, 0, 0),
                   double scale = 1.0 / 255.0,
                   bool swapRB = true,
                   bool skipForwardTest = false);
    bool unloadModel();

    // --- 推理 ---
    bool infer(const cv::Mat& input, cv::Mat& rawOutput, QJsonObject& result);
    bool infer(const cv::Mat& input, QJsonObject& result);
    bool inferBatch(const QList<cv::Mat>& inputs, QList<QJsonObject>& results);

    // --- 跳过预处理的推理（输入已是 NCHW blob，适用于 ZeroShotEngine 等自定义预处理场景） ---
    bool inferRaw(const cv::Mat& preprocessedBlob, cv::Mat& rawOutput, QJsonObject& result);

    // --- 多输入多输出推理（跳过预处理） ---
    // 适用于 MobileSAM 解码器等多输入模型。
    // inputBlobs 顺序必须与模型输入声明顺序一致（shape 由各 blob 实际维度决定，须为连续 CV_32F）。
    // outputBlobs 按模型输出声明顺序返回。
    bool inferMultiRaw(const std::vector<cv::Mat>& inputBlobs,
                       std::vector<cv::Mat>& outputBlobs,
                       QJsonObject& result);

    // --- 状态查询 ---
    bool isModelLoaded() const { return m_modelLoaded; }
    QString currentModelPath() const { return m_modelPath; }
    QSize modelInputSize() const { return m_inputSize; }
    InferenceMetrics lastMetrics() const { return m_lastMetrics; }
    ErrorState errorState() const { return m_errorState; }
    QString lastError() const { return m_lastError; }
    void resetError();
    ModelInputSpec inputSpec() const { return m_inputSpec; }

    // --- 预热 ---
    bool warmUp(int iterations = 3);

    // --- 取消与超时控制（线程安全） ---
    void cancelInference() { m_cancelFlag.store(true, std::memory_order_release); }
    bool isCancelled() const { return m_cancelFlag.load(std::memory_order_acquire); }
    void resetCancelFlag() { m_cancelFlag.store(false, std::memory_order_release); }
    void setSingleInferenceTimeoutMs(qint64 ms) { m_singleInferenceTimeoutMs = ms; }
    qint64 singleInferenceTimeoutMs() const { return m_singleInferenceTimeoutMs; }

signals:
    void modelLoaded(bool success);
    void inferenceCompleted(bool success);
    void progressUpdated(int current, int total);
    void stageProgress(const QString& stageDesc, qint64 elapsedMs);

private:
    // 预处理：resize + blobFromImage
    cv::Mat preprocess(const cv::Mat& input);
    // 后处理：根据输出维度分派到分类/YOLO/通用
    QJsonObject postprocess(const cv::Mat& output, const cv::Mat& preprocessed);
    // YOLOv5/v8 检测后处理
    void postprocessYOLO(const cv::Mat& output, QJsonObject& result);

    // OpenCV DNN 后端（用于对比基准）
    bool runOpenCVDNN(const cv::Mat& blob, cv::Mat& output);
    // ONNX Runtime 后端（M1 核心实现）
    bool runONNXRuntime(const cv::Mat& blob, cv::Mat& output);
    // ONNX Runtime 多输入多输出推理（M6: MobileSAM 解码器）
    bool runMultiONNXRuntime(const std::vector<cv::Mat>& inputBlobs,
                             std::vector<cv::Mat>& outputBlobs);

    // --- 成员变量 ---
    QString m_modelPath;
    bool m_modelLoaded = false;
    Backend m_backend = Backend::ONNXRuntime;
    QStringList m_classLabels;

    // OpenCV DNN 相关
    cv::dnn::Net m_net;

    // 预处理参数
    QSize m_inputSize;
    cv::Scalar m_mean;
    double m_scale = 1.0 / 255.0;
    bool m_swapRB = true;
    InferenceMetrics m_lastMetrics;

    // 错误状态
    ErrorState m_errorState = ErrorState::NoError;
    QString m_lastError;
    ModelInputSpec m_inputSpec;

    // 取消与超时
    std::atomic<bool> m_cancelFlag{false};
    qint64 m_singleInferenceTimeoutMs = 30000;

    // YOLO 后处理阈值
    float m_confThreshold = 0.25f;
    float m_iouThreshold = 0.45f;

    // ORT 会话配置
    ORTSessionConfig m_ortConfig;

#ifdef ZSU_HAS_ORT
    // ONNX Runtime 相关（使用 unique_ptr 管理生命周期）
    std::unique_ptr<Ort::Env> m_ortEnv;
    std::unique_ptr<Ort::Session> m_ortSession;
    std::unique_ptr<Ort::MemoryInfo> m_ortMemoryInfo;

    // 模型输入/输出信息
    std::vector<std::string> m_inputNames;
    std::vector<std::string> m_outputNames;
    std::vector<const char*> m_inputNamePtrs;   // ORT API 需要 char* 指针
    std::vector<const char*> m_outputNamePtrs;
    std::vector<std::vector<int64_t>> m_inputShapes;
    std::vector<std::vector<int64_t>> m_outputShapes;

    // 初始化 ORT 会话
    bool initORTSession(const QString& modelPath);
    // 创建输入张量
    Ort::Value createInputTensor(const cv::Mat& blob);
#endif
};

} // namespace zsu

#endif // ORTINFERENCEENGINE_H
