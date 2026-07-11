#ifndef QDV_IMAGE_VARIABLE_MANAGER_H
#define QDV_IMAGE_VARIABLE_MANAGER_H

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QHash>
#include <QDateTime>
#include <QMutex>

namespace QDV {

/**
 * @brief 图像变量管理器（v2.6.0）
 *
 * 功能：
 * 1. 自动收集算子执行后的输出图像（按节点 ID 索引）
 * 2. 支持按节点 ID 查询图像路径
 * 3. 支持切换预览不同算子节点的输出
 * 4. 变量随方案保存（仅保存路径，不保存图像数据）
 *
 * 设计要点：
 * - 图像变量名 = 节点 ID（唯一）
 * - 图像存储为临时文件路径（QML 端用 Image 加载）
 * - 支持时间戳，便于判断是否为最新输出
 */
class ImageVariableManager : public QObject {
    Q_OBJECT
public:
    /// 图像变量结构
    struct ImageVariable {
        QString nodeId;         ///< 算子节点 ID
        QString toolName;       ///< 算子名称（如 "ReadImage"、"Threshold"）
        QString outputImagePath;///< 输出图像临时文件路径
        qint64 timestamp;       ///< 最后更新时间戳（ms since epoch）
        int width;              ///< 图像宽度
        int height;             ///< 图像高度
        int channels;           ///< 图像通道数
    };

    explicit ImageVariableManager(QObject* parent = nullptr);
    ~ImageVariableManager() override;

    // === 图像变量管理 ===
    /// 更新（或创建）指定节点的图像变量
    /// @param nodeId 算子节点 ID
    /// @param toolName 算子名称
    /// @param outputImagePath 输出图像路径
    /// @param width/height/channels 图像信息
    void updateImageVariable(const QString& nodeId, const QString& toolName,
                              const QString& outputImagePath,
                              int width = 0, int height = 0, int channels = 0);

    /// 获取指定节点的图像变量信息
    Q_INVOKABLE QVariantMap imageVariable(const QString& nodeId) const;

    /// 获取所有图像变量列表（供 QML ListView 显示）
    Q_INVOKABLE QVariantList imageVariables() const;

    /// 获取指定节点的输出图像路径
    Q_INVOKABLE QString imagePath(const QString& nodeId) const;

    /// 删除指定节点的图像变量（节点删除时调用）
    Q_INVOKABLE bool removeImageVariable(const QString& nodeId);

    /// 清空所有图像变量
    Q_INVOKABLE void clear();

    /// 图像变量数量
    Q_INVOKABLE int count() const;

    /// 是否存在指定节点的图像变量
    Q_INVOKABLE bool exists(const QString& nodeId) const;

signals:
    /// 图像变量被更新（触发预览窗口刷新）
    void imageVariableUpdated(const QString& nodeId, const QString& outputPath);
    /// 图像变量被删除
    void imageVariableRemoved(const QString& nodeId);
    /// 整体变更（清空/批量更新后触发）
    void imageVariablesChanged();

private:
    QHash<QString, ImageVariable> m_imageVariables;
    // 线程安全：异步执行算子时，子线程更新图像变量，主线程读取列表。
    // 必须用互斥锁保护 m_imageVariables，否则并发读写会导致 QHash 内部结构损坏崩溃。
    // 使用 mutable：const 成员函数（imageVariable/imageVariables/imagePath/count/exists）
    // 也需要在读取时加锁，否则 const 校验会阻止 lock() 调用。
    mutable QMutex m_mutex;
};

} // namespace QDV

#endif // QDV_IMAGE_VARIABLE_MANAGER_H
