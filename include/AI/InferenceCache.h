#ifndef INFERENCECACHE_H
#define INFERENCECACHE_H

#include <QObject>
#include <QString>
#include <QCache>
#include <QJsonObject>
#include <QSize>
#include <QMutex>
#include <QMutexLocker>
#include <opencv2/opencv.hpp>

/**
 * @brief 推理结果缓存
 *
 * 三层缓存的第二层：缓存推理结果（QJsonObject）
 * Key: modelPath|mtime|inputSize|conf|imgFingerprint
 * 容量: 50 条
 * 失效条件: 模型/尺寸/阈值/图像变化
 */
class InferenceCache : public QObject {
    Q_OBJECT

public:
    explicit InferenceCache(QObject* parent = nullptr, int capacity = 50);
    ~InferenceCache();

    /**
     * @brief 生成缓存 Key
     * @param modelPath 模型路径
     * @param modelMtime 模型文件修改时间
     * @param inputSize 输入尺寸
     * @param confThreshold 置信度阈值
     * @param image 输入图像
     * @return 缓存 Key 字符串
     */
    static QString generateKey(const QString& modelPath,
                                qint64 modelMtime,
                                const QSize& inputSize,
                                double confThreshold,
                                const cv::Mat& image);

    /**
     * @brief 查询缓存
     * @param key 缓存 Key
     * @param result 输出参数，缓存的结果
     * @return true=命中，false=未命中
     */
    bool lookup(const QString& key, QJsonObject& result);

    /**
     * @brief 写入缓存
     * @param key 缓存 Key
     * @param result 推理结果
     */
    void insert(const QString& key, const QJsonObject& result);

    /**
     * @brief 清空缓存
     */
    void clear();

    /**
     * @brief 获取缓存条目数
     */
    int size() const;

    /**
     * @brief 获取缓存容量
     */
    int capacity() const;

    /**
     * @brief 设置缓存容量
     */
    void setCapacity(int capacity);

    /**
     * @brief 计算图像指纹
     * 16x16 灰度 → meanStdDev mean + sum
     * @param image 输入图像
     * @return 指纹字符串
     */
    static QString computeImageFingerprint(const cv::Mat& image);

private:
    QCache<QString, QJsonObject> m_cache;
    mutable QMutex m_mutex;
};

#endif // INFERENCECACHE_H
