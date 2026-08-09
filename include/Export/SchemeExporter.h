#ifndef SCHEME_EXPORTER_H
#define SCHEME_EXPORTER_H

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QFutureWatcher>

struct ExportConfig;

/**
 * @brief 算子流程导出编排器（对外门面）
 *
 * 异步执行导出流程（QtConcurrent::run + QFutureWatcher）：
 *  1. 校验配置与模板
 *  2. 创建产物目录
 *  3. 复制模板二进制
 *  4. 收集运行时依赖
 *  5. 序列化方案
 *  6. 生成文档与示例
 *  7. 写入 manifest.json
 *
 * 用法：
 *   SchemeExporter exporter;
 *   connect(&exporter, &SchemeExporter::exportFinished, ...);
 *   exporter.exportAsync(config, nodes, connections);
 */
class SchemeExporter : public QObject {
    Q_OBJECT

public:
    explicit SchemeExporter(QObject* parent = nullptr);
    ~SchemeExporter() override;

    /// 异步导出（立即返回，通过信号通知结果）
    /// @param config 导出配置
    /// @param nodes 当前方案节点（QVariantList of QVariantMap）
    /// @param connections 当前方案连边
    void exportAsync(const ExportConfig& config,
                     const QVariantList& nodes,
                     const QVariantList& connections);

    /// 当前是否正在导出
    bool isBusy() const { return m_busy; }

    /// 取消导出（尽力中止，已生成文件不回滚）
    void cancel();

signals:
    /// 进度更新（0-100）
    void exportProgress(int percent);
    /// 导出完成（success=true 时 message 含产物路径）
    void exportFinished(bool success, const QString& message);
    /// 需要用户确认覆盖（UI 弹二次确认）
    void confirmOverwriteRequested(const QString& existingPath);
    /// 错误通知
    void errorOccurred(const QString& message);

private slots:
    void onExportFinished();

private:
    /// 工作线程：实际执行导出
    bool doExport(const ExportConfig& config,
                  const QVariantList& nodes,
                  const QVariantList& connections,
                  QString* resultMsg);

    /// 序列化方案到 JSON 文件
    bool serializeScheme(const QString& outPath,
                         const QVariantList& nodes,
                         const QVariantList& connections,
                         const QString& schemeName);

    /// 写入 manifest.json
    bool writeManifest(const QString& dir,
                      const ExportConfig& config,
                      const QVariantList& nodes,
                      bool containsAi);

    QFutureWatcher<bool>* m_watcher = nullptr;
    bool m_busy = false;
    QString m_lastMessage;
};

#endif // SCHEME_EXPORTER_H
