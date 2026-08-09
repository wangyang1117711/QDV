#pragma once

#include <QObject>
#include <QString>
#include <QDateTime>
#include <QJsonObject>
#include <QJsonArray>
#include <QVariantMap>
#include <QStringList>
#include <QList>

// 图像条目快照（序列化友好，不含 QPixmap）
struct ImageEntrySnapshot {
    QString filePath;        // 绝对路径(引用模式) 或 包内相对路径(打包模式)
    QString originalPath;    // 原始路径(打包模式保留，用于溯源)
    QString fileName;
    int width = 0;           // 图像宽度(像素)
    int height = 0;          // 图像高度(像素)
    int channels = 0;        // 通道数(1灰度/3彩色/4带透明)
    QString format;          // 格式字符串("PNG"/"JPEG"/"BMP"...)
    qint64 fileSize = 0;     // 文件大小(字节)
    bool isAnnotated = false;
    QString label;
    bool isSelected = false;
};

// 训练参数快照
struct TrainingParamsSnapshot {
    QString modelType = "resnet18";
    int numEpochs = 20;
    int batchSize = 8;
    double learningRate = 0.001;
    double valSplit = 0.2;
};

// 训练状态快照
struct TrainingStateSnapshot {
    bool hasTrained = false;
    QDateTime lastTrainedAt;
    QVariantMap lastMetrics;   // trainAcc/valAcc/trainLoss/valLoss
    QString onnxPath;          // 训练产物路径(仅记录，不打包)
};

// 历史记录条目
struct HistoryEntry {
    QDateTime time;
    QString action;            // "save" | "saveAs" | "load"
    QString summary;           // 人类可读摘要
};

// 修改历史上限
static constexpr int MAX_HISTORY_ENTRIES = 50;

class TrainingProject : public QObject {
    Q_OBJECT

public:
    explicit TrainingProject(QObject* parent = nullptr);

    // ===== 项目元数据 =====
    QString name() const;
    void setName(const QString& name);
    QString description() const;
    void setDescription(const QString& desc);
    QDateTime createdAt() const;
    QDateTime modifiedAt() const;
    void updateModifiedTime();          // 保存/加载时调用
    int schemaVersion() const;
    QString filePath() const;           // 当前项目文件路径
    void setFilePath(const QString& path);
    bool isDirty() const;               // 是否有未保存修改
    void markDirty();
    void markClean();

    // ===== 快照收集（保存时由各 Manager 填充）=====
    void setImagesSnapshot(const QList<ImageEntrySnapshot>& snapshots);
    void setCategoriesSnapshot(const QJsonObject& categoriesJson);
    void setTrainingSnapshot(const TrainingParamsSnapshot& params,
                             const TrainingStateSnapshot& state);

    // ===== 快照读取（加载时分发到各 Manager）=====
    QList<ImageEntrySnapshot> imagesSnapshot() const;
    QJsonObject categoriesSnapshot() const;
    TrainingParamsSnapshot trainingParams() const;
    TrainingStateSnapshot trainingState() const;

    // ===== 历史记录 =====
    void appendHistory(const QString& action, const QString& summary);
    QJsonArray historyJson() const;
    void loadHistory(const QJsonArray& arr);

    // ===== 序列化（供 ProjectSerializer 调用）=====
    QJsonObject toJson() const;          // 组装完整项目 JSON
    bool fromJson(const QJsonObject& obj, QString* errMsg = nullptr); // 解析项目 JSON

    // ===== 重置 =====
    void reset();                        // 清空所有数据，回到初始状态

signals:
    void modifiedChanged(bool dirty);
    void filePathChanged(const QString& path);

private:
    // 项目元数据
    QString m_name;
    QString m_description;
    QDateTime m_createdAt;
    QDateTime m_modifiedAt;
    int m_schemaVersion = 1;
    QString m_filePath;
    bool m_dirty = false;

    // 数据快照
    QList<ImageEntrySnapshot> m_imagesSnapshot;
    QJsonObject m_categoriesSnapshot;
    TrainingParamsSnapshot m_trainingParams;
    TrainingStateSnapshot m_trainingState;

    // 修改历史
    QList<HistoryEntry> m_history;
};
