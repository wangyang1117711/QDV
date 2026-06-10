#ifndef MODELMANAGER_H
#define MODELMANAGER_H

#include <QObject>
#include <QString>
#include <QMap>
#include <QList>
#include <QPair>
#include <QSize>

class InferenceEngine;

struct ModelInfo {
    QString id;
    QString path;
    QSize inputSize;
    double avgInferenceMs = 0.0;
    int accessCount = 0;
};

class ModelManager : public QObject {
    Q_OBJECT

public:
    static ModelManager* instance();

    static constexpr int DEFAULT_CACHE_SIZE = 3;
    
    // 默认模型相关常量
    static constexpr const char* DEFAULT_MODEL_DIR = "E:/anchor/Trae/QDV/models";
    static constexpr const char* DEFAULT_MODEL_ID = "default_model";
    static constexpr const char* DEFAULT_MODEL_NAME = "YOLO";

    void setCacheSize(int size);
    int cacheSize() const { return m_cacheSize; }

    bool loadModel(const QString& modelPath, const QString& modelId,
                   const QSize& inputSize = QSize(224, 224));
    bool unloadModel(const QString& modelId);
    void unloadAll();
    
    // 默认模型相关方法
    QString defaultModelDirectory() const;
    QStringList findDefaultModels() const;
    QString getDefaultModelPath() const;
    bool loadDefaultModel(const QString& modelId = DEFAULT_MODEL_ID);
    bool isDefaultModelAvailable() const;
    QStringList getAvailableModelNames() const;
    QString getModelPathByName(const QString& modelName) const;

    bool warmUp(const QString& modelId, int iterations = 3);

    InferenceEngine* getEngine(const QString& modelId);

    QStringList loadedModelIds() const;
    QList<ModelInfo> loadedModelInfo() const;

    QStringList listModels(const QString& directory);

    // 注册训练完成的新模型：将onnx和labels文件复制到models/目录，
    // 更新manifest.json，处理命名冲突（加时间戳后缀）
    bool registerTrainedModel(const QString& onnxPath,
                              const QString& labelsPath,
                              const QString& modelName);

signals:
    void modelLoaded(const QString& modelId);
    void modelUnloaded(const QString& modelId);
    void modelEvicted(const QString& modelId);
    void warmUpCompleted(const QString& modelId, double avgMs);
    void defaultModelLoadFailed(const QString& error);
    void modelRegistered(const QString& modelName, const QString& onnxPath);

private:
    ModelManager(QObject* parent = nullptr);
    ~ModelManager();

    void evictLRU();
    void touch(const QString& modelId);

    QMap<QString, InferenceEngine*> m_engines;
    QMap<QString, ModelInfo> m_modelInfos;
    QList<QString> m_accessOrder;  // LRU: front=most recent, back=least recent
    int m_cacheSize = DEFAULT_CACHE_SIZE;

    static ModelManager* s_instance;
};

#endif