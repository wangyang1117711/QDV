#ifndef INFERENCEENGINE_H
#define INFERENCEENGINE_H

#include <QObject>
#include <QString>
#include <QJsonObject>
#include <QJsonArray>
#include <QSize>
#include <QCache>
#include <QDateTime>
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

    bool loadModel(const QString& modelPath,
                   const QSize& inputSize = QSize(224, 224),
                   const cv::Scalar& mean = cv::Scalar(0, 0, 0),
                   double scale = 1.0 / 255.0,
                   bool swapRB = true);
    bool unloadModel();

    bool infer(const cv::Mat& input, cv::Mat& output, QJsonObject& result);
    bool infer(const cv::Mat& input, QJsonObject& result);
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

signals:
    void modelLoaded(bool success);
    void inferenceCompleted(bool success);
    void progressUpdated(int current, int total);
    void inferenceError(int errorCode, const QString& errorType, const QString& message);

private:
    cv::Mat preprocess(const cv::Mat& input);
    QJsonObject postprocess(const cv::Mat& output, const cv::Mat& preprocessed);
    QList<Prediction> extractTopK(const cv::Mat& softmax, int k);
    double formatConfidence(double confidence) const;

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
    InferenceMetrics m_lastMetrics;
    ErrorState m_lastError;
    QStringList m_categoryLabels;

    void* m_ortSession = nullptr;
    void* m_ortEnv = nullptr;
    void* m_ortMemoryInfo = nullptr;
    QStringList m_ortOutputNames;

    QCache<QString, RecognitionResult> m_inferenceCache{50};
};

#endif