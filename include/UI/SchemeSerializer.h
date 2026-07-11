#ifndef SCHEME_SERIALIZER_H
#define SCHEME_SERIALIZER_H

/**
 * @file SchemeSerializer.h
 * @brief 方案异步 I/O 序列化器（v2.1.0 M4 引入）
 *
 * 设计原则：
 * 1. **异步执行**：保存/加载通过 QtConcurrent::run 在工作线程执行，
 *    避免大方案 JSON 解析时阻塞 UI 线程
 * 2. **结果回调**：完成时通过 signal 通知 EditViewBridge，由 QML 端 Toast 提示
 * 3. **AI 零侵入**：只读写 m_currentNodes（QVariantList）+ m_connections，
 *    不依赖 Core/Scheme*，确保 M5+ 阶段与推理协同时不被污染
 * 4. **多格式支持**（v5.3.1）：
 *    - JSON (.json)：UTF-8 编码 + 缩进美化，便于用户查看
 *    - 压缩包 (.qdvz)：gzip 压缩的 JSON，体积小约 5-10 倍
 *    - XML (.xml)：标签式结构，便于与其他系统集成
 *    加载时按扩展名自动识别格式。
 *    JSON 结构：
 *    {
 *      "version": "2.1.0",
 *      "schemeName": "方案名",
 *      "savedAt": "2026-06-04T...",
 *      "nodes": [{ id, type, x, y, params }],
 *      "connections": [{ fromId, fromPort, toId, toPort }],
 *      "variables": [ {name, type, value, description}, ... ]   // v2.6.0 控制变量
 *    }
 *
 * 与 Core/SchemeManager 区别：
 * - Core/SchemeManager：操作 Scheme* 业务对象（同步）—— AI 推理上游
 * - UI/SchemeSerializer：操作 QVariantList UI 快照（异步）—— QML 画布上游
 */

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QFutureWatcher>

namespace QDV {
namespace UI {

/**
 * @brief 方案异步序列化器
 *
 * 用法：
 * 1. EditViewBridge 创建时构造一个 SchemeSerializer 实例
 * 2. connect saveFinished/loadFinished 到 EditViewBridge 对应槽
 * 3. 调 saveAsync(filePath, nodes, connections, name) / loadAsync(filePath)
 * 4. 等待 saveFinished / loadFinished 信号回调
 */
class SchemeSerializer : public QObject {
    Q_OBJECT

public:
    /// v5.3.1：方案文件格式
    enum SchemeFormat {
        FormatJson,       ///< .json（默认，UTF-8 缩进美化）
        FormatCompressed, ///< .qdvz（gzip 压缩 JSON）
        FormatXml         ///< .xml（标签式结构）
    };

    explicit SchemeSerializer(QObject* parent = nullptr);
    ~SchemeSerializer() override;

    /// 当前是否正在执行 I/O（防止并发）
    bool isBusy() const { return m_saveWatcher.isRunning() || m_loadWatcher.isRunning(); }

    /// v5.3.1：根据文件扩展名推断格式
    static SchemeFormat detectFormat(const QString& filePath);

    /// 异步保存（v5.3.1：按扩展名自动选择格式）
    /// @param filePath  目标文件路径（.json / .qdvz / .xml）
    /// @param nodes     节点列表（QVariantList of QVariantMap）
    /// @param connections  连边列表
    /// @param schemeName  方案名（写入 header）
    void saveAsync(const QString& filePath,
                   const QVariantList& nodes,
                   const QVariantList& connections,
                   const QString& schemeName);

    /// 异步加载（v5.3.1：按扩展名自动识别格式）
    /// @param filePath  源文件路径（.json / .qdvz / .xml）
    void loadAsync(const QString& filePath);

    /// v2.6.0：设置待保存的控制变量 JSON（保存前由 EditViewBridge 调用注入）
    /// @param variablesJson 控制变量 JSON 数组文本（[] 表示无变量）
    void setVariablesJson(const QString& variablesJson);

signals:
    /// 保存完成（success=false 时 message 包含错误详情）
    void saveFinished(const QString& filePath, bool success, const QString& message);
    /// 加载完成（success=true 时通过 message 携带 JSON 文本；
    ///  接收方在 UI 线程解析后写入 m_currentNodes/m_connections）
    void loadFinished(const QString& filePath, bool success,
                      const QString& message, const QString& jsonText);
    /// v2.6.0：加载完成时携带的控制变量 JSON 文本（无 variables 字段时为 "[]"）
    void variablesLoaded(const QString& variablesJson);

private slots:
    void onSaveFinished();
    void onLoadFinished();

private:
    /// 工作线程：把 QVariantList 序列化为 JSON 文本
    static QString serializeToJson(const QVariantList& nodes,
                                   const QVariantList& connections,
                                   const QString& schemeName,
                                   const QString& variablesJson,
                                   QString* errMsg);

    /// v5.3.1：序列化为 XML 文本
    static QString serializeToXml(const QVariantList& nodes,
                                  const QVariantList& connections,
                                  const QString& schemeName,
                                  const QString& variablesJson,
                                  QString* errMsg);

    /// v5.3.1：从 XML 文本解析回 JSON 文本（统一走 JSON 中间格式，复用 applyLoadedJson）
    static QString xmlToJson(const QString& xmlText, QString* errMsg);

    /// v5.3.1：保存格式（由 saveAsync 设置，供 onSaveFinished 读取）
    SchemeFormat m_saveFormat = FormatJson;
    SchemeFormat m_loadFormat = FormatJson;

    QFutureWatcher<QString>  m_saveWatcher;  ///< 保存任务（结果=错误消息，"OK"=成功）
    QFutureWatcher<QString>  m_loadWatcher;  ///< 加载任务（结果=JSON 文本，error=错误消息）

    // 保存任务的输入参数（worker 线程不能直接捕获 Qt 对象地址，必须副本）
    QString     m_saveFilePath;
    QString     m_loadFilePath;
    QString     m_saveSchemeName;
    QVariantList m_saveNodes;       ///< 保存节点的副本
    QVariantList m_saveConnections; ///< 保存连边的副本
    QString     m_saveVariablesJson;  ///< v2.6.0 待保存的控制变量 JSON 文本
};

} // namespace UI
} // namespace QDV

#endif // SCHEME_SERIALIZER_H
