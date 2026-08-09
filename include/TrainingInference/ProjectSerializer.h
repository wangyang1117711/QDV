#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QFutureWatcher>
#include "TrainingInference/TrainingProject.h"

class ProjectSerializer : public QObject {
    Q_OBJECT

public:
    // 保存模式枚举
    enum SaveMode {
        ModeReference,  // 仅引用(轻量 JSON)
        ModeBundled     // 完整打包(gzip，含图像副本)
    };
    Q_ENUM(SaveMode)

    explicit ProjectSerializer(QObject* parent = nullptr);

    // ===== 异步保存 =====
    // mode 决定是否打包图像
    // 注意：project 必须在主线程预先填充好快照数据，工作线程不访问 project 对象
    void saveAsync(const QString& filePath,
                   TrainingProject* project,
                   SaveMode mode);

    // ===== 异步加载 =====
    // 自动识别格式(引用/打包)，工作线程只做文件I/O和JSON解析
    // 加载完成后通过 loadFinished 信号返回 QJsonObject，主线程负责填充 project
    void loadAsync(const QString& filePath);

    // ===== 格式检测(同步，快速) =====
    static SaveMode detectMode(const QString& filePath);

    // ===== 路径失效重定位 =====
    // 用户指定新目录，批量重定位缺失图像，返回成功重定位的路径列表
    static QStringList relocateMissingImages(QList<ImageEntrySnapshot>& images,
                                             const QString& newDir);

    // ===== 同步保存/加载方法（供测试和直接调用）=====
    // 引用模式保存：纯 JSON
    static bool saveReferenceMode(const QString& filePath,
                                  TrainingProject* project,
                                  QString* errMsg);
    // 打包模式保存：gzip 压缩包，内含 project.json + images/
    static bool saveBundledMode(const QString& filePath,
                                TrainingProject* project,
                                QString* errMsg);

    // ===== 线程安全版本（不访问 project 对象，供异步工作线程使用）=====
    // 引用模式保存：纯 JSON（使用预收集的 projectJson）
    static bool saveReferenceModeWithData(const QString& filePath,
                                          const QJsonObject& projectJson,
                                          QString* errMsg);
    // 打包模式保存：gzip 压缩包（使用预收集的 projectJson 和 imagesCopy）
    // 注意：imagesCopy 是副本，修改不影响原始 project
    static bool saveBundledModeWithData(const QString& filePath,
                                        const QJsonObject& projectJson,
                                        QList<ImageEntrySnapshot> imagesCopy,
                                        QString* errMsg);

    // ===== 线程安全加载（只做文件I/O和JSON解析，返回 QJsonObject）=====
    // 引用模式加载：读取 JSON 文件，返回原始 QJsonObject
    static bool loadReferenceModeToJson(const QString& filePath,
                                        QJsonObject* outJson,
                                        QStringList* missingImages,
                                        QString* errMsg);
    // 打包模式加载：解压 + 解析，返回原始 QJsonObject
    // 注意：图像文件解压到临时目录，QJsonObject 中的图像路径已重写为临时目录绝对路径
    static bool loadBundledModeToJson(const QString& filePath,
                                      QJsonObject* outJson,
                                      QString* errMsg);

    // ===== 旧版同步加载方法（保留兼容，内部委托新方法）=====
    // 加载引用模式
    static bool loadReferenceMode(const QString& filePath,
                                  TrainingProject* project,
                                  QStringList* missingImages,
                                  QString* errMsg);
    // 加载打包模式：解压 + 路径重写
    static bool loadBundledMode(const QString& filePath,
                                TrainingProject* project,
                                QString* errMsg);

signals:
    // 保存进度(0~100)
    void saveProgress(int percent, const QString& stage);
    // 保存完成
    void saveFinished(const QString& filePath, bool success, const QString& message);
    // 加载进度(0~100)
    void loadProgress(int percent, const QString& stage);
    // 加载完成：返回 QJsonObject（主线程负责填充 project），避免跨线程 QObject 所有权问题
    void loadFinished(const QString& filePath, bool success, const QString& message,
                      const QJsonObject& projectJson,
                      const QStringList& missingImages);

private:
    // 打包模式：复制图像到临时目录，重写路径
    static bool bundleImages(TrainingProject* project,
                             const QString& tempDir,
                             QString* errMsg);

    // 校验引用模式图像路径是否存在
    static QStringList checkMissingImages(const QList<ImageEntrySnapshot>& images);

    // 从 QJsonObject 提取图像快照列表
    static QList<ImageEntrySnapshot> extractImagesFromJson(const QJsonObject& root);

    QFutureWatcher<bool> m_saveWatcher;
    QFutureWatcher<bool> m_loadWatcher;

    // 工作线程与主线程通信的数据
    QString m_currentSavePath;
    QString m_currentLoadPath;
    SaveMode m_currentMode = ModeReference;

    // 加载结果数据（工作线程写入，主线程读取后清空）
    QJsonObject m_loadedJson;
    QStringList m_missingImages;
    QString m_lastError;
};
