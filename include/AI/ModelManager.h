#ifndef MODELMANAGER_H
#define MODELMANAGER_H

#include <QObject>
#include <QString>
#include <QMap>
#include <QList>
#include <QPair>
#include <QSize>
#include <QJsonObject>
#include <QJsonArray>

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
    // DEFAULT_MODEL_DIR 已改为动态获取：applicationDirPath()/models（见 defaultModelDirectory()）
    static constexpr const char* DEFAULT_MODEL_ID = "default_model";
    static constexpr const char* DEFAULT_MODEL_NAME = "YOLO";

    void setCacheSize(int size);
    int cacheSize() const { return m_cacheSize; }

    // 返回最近一次 loadModel 失败的详细错误信息
    QString lastLoadError() const { return m_lastLoadError; }

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

    // === 新增方法（P0 Task 2）===
    // 添加自定义模型：复用 registerTrainedModel 逻辑，但接受 displayName 作为模型 ID
    bool addCustomModel(const QString& onnxPath, const QString& displayName,
                        const QString& labelsPath = QString());
    // 事务性删除：将模型文件 rename 为 .trash 后缀，manifest 记录删除操作
    // @param deleteRelatedData 是否同时清理关联数据（labels、训练记录、评估报告等）
    bool removeCustomModelTransactional(const QString& modelId, bool deleteRelatedData = false);
    // 校验模型完整性：流式计算 SHA256 并与 manifest 中记录的哈希/大小比对
    bool verifyModelIntegrity(const QString& modelId);
    // 自动识别模型类型："yolo" 或 "classification"
    QString autoDetectModelType(const QString& modelPath);
    // 自动识别输入尺寸：YOLO→640x640, 其他→224x224
    QSize autoDetectInputSize(const QString& modelPath);
    // v2.7.2：预检模型是否能被当前推理后端加载（不实际加载到缓存）
    // 返回 {兼容, 错误信息}；兼容时错误信息为空
    // 当前后端为 OpenCV DNN，部分 PyTorch 导出的 ONNX 算子不兼容
    QPair<bool, QString> probeModelLoadability(const QString& modelPath) const;
    // v2.7.3：更新单个或全部已注册模型的 loadable/load_error 字段并保存 manifest
    // modelId 为空时更新所有条目；返回是否全部成功
    bool updateModelLoadability(const QString& modelId = QString());
    // 加载 manifest.json 到内存（兼容旧格式）
    bool loadManifest();
    // 保存 manifest.json 到磁盘
    bool saveManifest();
    // 返回 manifest.json 的 models 数组
    QJsonArray manifestModels() const;

signals:
    void modelLoaded(const QString& modelId);
    void modelUnloaded(const QString& modelId);
    void modelEvicted(const QString& modelId);
    void warmUpCompleted(const QString& modelId, double avgMs);
    void defaultModelLoadFailed(const QString& error);
    void modelRegistered(const QString& modelName, const QString& onnxPath);
    // 模型列表变更时发射（注册/删除后）
    void modelsChanged();

private:
    ModelManager(QObject* parent = nullptr);
    ~ModelManager();

    void evictLRU();
    void touch(const QString& modelId);

    // v2.7.3：当 manifest 为空时，扫描默认模型目录中的 .onnx 文件并自动注册，
    // 保证启动后模型库与磁盘文件一致
    void syncManifestWithDirectory();

    QMap<QString, InferenceEngine*> m_engines;
    QMap<QString, ModelInfo> m_modelInfos;
    QList<QString> m_accessOrder;  // LRU: front=most recent, back=least recent
    int m_cacheSize = DEFAULT_CACHE_SIZE;

    // manifest.json 缓存与路径（P0 Task 2）
    QJsonObject m_manifest;       // manifest.json 缓存
    QString m_manifestPath;       // manifest.json 完整路径

    QString m_lastLoadError;      // 最近一次模型加载失败的错误信息

    static ModelManager* s_instance;
};

#endif