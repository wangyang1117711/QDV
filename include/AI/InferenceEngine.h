#ifndef INFERENCEENGINE_H
#define INFERENCEENGINE_H

#include <QObject>
#include <QString>
#include <QJsonObject>
#include <QJsonArray>
#include <QSize>
#include <QCache>
#include <QDateTime>
#include <vector>
#include <opencv2/opencv.hpp>
#include <opencv2/dnn.hpp>

struct InferenceMetrics {
    qint64 preprocessMs = 0;
    qint64 inferenceMs = 0;
    qint64 postprocessMs = 0;
    qint64 totalMs = 0;
    QString backend;
};

struct Prediction {
    QString   categoryId;
    QString   categoryName;
    double    confidence = 0.0;
    int       rank = 0;
};

struct RecognitionResult {
    QString   imageId;
    QList<Prediction> predictions;
    double    inferenceTimeMs = 0.0;
    QString   modelName;
};

struct ErrorState {
    bool      hasError      = false;
    int       errorCode     = 0;
    QString   errorType;
    QString   errorMessage;
    int       retryCount    = 0;
    static constexpr int MAX_RETRIES = 3;

    void set(int code, const QString& type, const QString& msg) {
        hasError = true;
        errorCode = code;
        errorType = type;
        errorMessage = msg;
    }

    void clear() {
        hasError = false;
        errorCode = 0;
        errorType.clear();
        errorMessage.clear();
        retryCount = 0;
    }

    bool canRetry() const { return retryCount < MAX_RETRIES; }
    void recordRetry() { retryCount++; }
};

struct ModelInputSpec {
    int width = 224;
    int height = 224;
    int channels = 3;
    double scale = 1.0 / 255.0;
    QList<double> mean;
    QList<double> std;
    bool swapRB = true;
};

class InferenceCache;  // 前向声明：第二层推理结果缓存

class InferenceEngine : public QObject {
    Q_OBJECT

public:
    enum Backend {
        BackendOpenCVDNN = 0,
        BackendONNXRuntime = 1
    };

    enum Precision {
        FP32 = 0,
        FP16 = 1,
        INT8 = 2
    };

    enum InferenceMode {
        SingleMode   = 0,
        BatchMode    = 1,
        RealtimeMode = 2
    };

    explicit InferenceEngine(QObject* parent = nullptr);
    ~InferenceEngine();

    void setBackend(Backend backend);
    Backend backend() const { return m_backend; }

    void setInferenceMode(InferenceMode mode);
    InferenceMode inferenceMode() const { return m_inferenceMode; }

    void setCategoryLabels(const QStringList& labels);
    QStringList categoryLabels() const { return m_categoryLabels; }

    // ImageNet 标准归一化常量（训练与推理必须一致）
    static constexpr double IMAGENET_MEAN_R = 0.485;
    static constexpr double IMAGENET_MEAN_G = 0.456;
    static constexpr double IMAGENET_MEAN_B = 0.406;
    static constexpr double IMAGENET_STD_R  = 0.229;
    static constexpr double IMAGENET_STD_G  = 0.224;
    static constexpr double IMAGENET_STD_B  = 0.225;

    bool loadModel(const QString& modelPath,
                   const QSize& inputSize = QSize(224, 224),
                   const cv::Scalar& mean = cv::Scalar(0, 0, 0),
                   double scale = 1.0 / 255.0,
                   bool swapRB = true,
                   const cv::Scalar& std = cv::Scalar(1.0, 1.0, 1.0));
    bool unloadModel();

    bool infer(const cv::Mat& input, cv::Mat& output, QJsonObject& result);
    bool infer(const cv::Mat& input, QJsonObject& result);
    // v5.4.0：检测/分割推理接口（扩展 IInferenceEngine）
    bool detect(const cv::Mat& input, double confThreshold, double iouThreshold,
                QJsonObject& result);
    bool segment(const cv::Mat& input, cv::Mat& mask, cv::Mat& overlay,
                 QJsonObject& result);
    bool inferBatch(const QList<cv::Mat>& inputs, QList<QJsonObject>& results);

    RecognitionResult inferWithResult(const cv::Mat& input, const QString& imageId = QString());

    bool isModelLoaded() const { return m_modelLoaded; }
    QString currentModelPath() const { return m_modelPath; }
    QSize modelInputSize() const { return m_inputSize; }
    InferenceMetrics lastMetrics() const { return m_lastMetrics; }
    ErrorState lastError() const { return m_lastError; }

    bool warmUp(int iterations = 3);

    static QStringList availableBackends();

    int cacheSize() const { return m_inferenceCache.maxCost(); }
    void setCacheSize(int maxEntries) { m_inferenceCache.setMaxCost(maxEntries); }
    void clearCache() { m_inferenceCache.clear(); }

    // 第二层缓存（InferenceCache）外部注入接口
    void setResultCache(InferenceCache* cache) { m_resultCache = cache; }

    // YOLO 检测参数配置
    void setConfThreshold(double conf) { m_confThreshold = conf; }
    void setIoUThreshold(double iou) { m_iouThreshold = iou; }
    double confThreshold() const { return m_confThreshold; }
    double iouThreshold() const { return m_iouThreshold; }

    // 公开预处理函数：供测试与外部验证输入格式转换
    cv::Mat preprocess(const cv::Mat& input);

signals:
    void modelLoaded(bool success);
    void inferenceCompleted(bool success);
    void progressUpdated(int current, int total);
    void inferenceError(int errorCode, const QString& errorType, const QString& message);

private:
    QJsonObject postprocess(const cv::Mat& output, const cv::Mat& preprocessed);
    // 分割后处理：将模型输出转换为类别掩膜 + 彩色叠加图
    QJsonObject postprocessSegment(const cv::Mat& output, cv::Mat& mask, cv::Mat& overlay,
                                    const cv::Mat& originalInput);
    QList<Prediction> extractTopK(const cv::Mat& softmax, int k);
    double formatConfidence(double confidence) const;

    // YOLO 后处理：YOLOv8 输出 [1, 4+nc, anchors]
    QJsonObject postprocessYoloV8(const cv::Mat& output, int numClasses);
    // YOLO 后处理：YOLOv5 输出 [1, anchors, 5+nc]
    QJsonObject postprocessYoloV5(const cv::Mat& output, int numClasses);
    // 非极大值抑制（NMS）
    std::vector<int> nms(const std::vector<cv::Rect2d>& boxes,
                         const std::vector<double>& scores,
                         double iouThreshold);
    // 计算两个矩形的 IoU
    double computeIoU(const cv::Rect2d& a, const cv::Rect2d& b);

    bool runOpenCVDNN(const cv::Mat& blob, cv::Mat& output);
    bool runONNXRuntime(const cv::Mat& input, cv::Mat& output);

    QString m_modelPath;
    bool m_modelLoaded = false;
    Backend m_backend = BackendOpenCVDNN;
    InferenceMode m_inferenceMode = SingleMode;

    cv::dnn::Net m_net;
    QSize m_inputSize;
    cv::Scalar m_mean;
    double m_scale = 1.0 / 255.0;
    bool m_swapRB = true;
    cv::Scalar m_std = cv::Scalar(1.0, 1.0, 1.0);  // 归一化 std（默认 1.0 = 不做除法）
    InferenceMetrics m_lastMetrics;
    ErrorState m_lastError;
    QStringList m_categoryLabels;

    void* m_ortSession = nullptr;
    void* m_ortEnv = nullptr;
    void* m_ortMemoryInfo = nullptr;
    QStringList m_ortOutputNames;

    QCache<QString, RecognitionResult> m_inferenceCache{50};

    InferenceCache* m_resultCache = nullptr;  // 第二层推理结果缓存（可选，外部注入）

    double m_confThreshold = 0.25;  // YOLO 置信度阈值
    double m_iouThreshold = 0.45;   // NMS IoU 阈值
};

#endif