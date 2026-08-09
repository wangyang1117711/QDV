#ifndef QDV_UI_OPERATOR_LIBRARY_BRIDGE_H
#define QDV_UI_OPERATOR_LIBRARY_BRIDGE_H

// =====================================================================
// OperatorLibraryBridge — QML ↔ Controller 桥接层（spec §1.2 / tasks.md Task 6.1）
//
// 职责：
//   1. 暴露 Q_INVOKABLE 方法供 QML 端调用导入/删除/列表操作
//   2. 转发 Controller 信号到 QML
//   3. 画布预检（删除前检查画布是否有节点使用该算子）
//
// 生命周期：
//   - 由 EditViewBridge 持有（在构造函数中创建）
//   - QML 端通过 editViewBridge.operatorLibraryBridge 访问
//
// 缓存策略：
//   - requestPreview 时缓存 ImportPreview（内部 QMap<type, ImportPreview>）
//   - commitImport 时从缓存取出（避免 QVariantMap → ImportPreview 反序列化）
// =====================================================================

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QMap>

namespace QDV {
namespace OperatorLibrary { struct ImportPreview; }
}

class EditViewBridge;  // 前向声明（避免头文件循环）

class OperatorLibraryBridge : public QObject {
    Q_OBJECT

public:
    explicit OperatorLibraryBridge(EditViewBridge* editViewBridge = nullptr, QObject* parent = nullptr);
    ~OperatorLibraryBridge() override;

    // === Q_INVOKABLE 方法（QML 端调用） ===

    /// 请求预览导入文件（解析+校验+查重 → ImportPreview）
    /// @param path .qdvop 文件路径
    /// @return QVariantMap 含字段：
    ///   ok: bool — 是否解析成功（即使有冲突也返回 true，冲突在 conflicts 中）
    ///   def: QVariantMap — 算子定义（OperatorDef::toMap()）
    ///   conflicts: QVariantList — 冲突列表 [{field, message}, ...]
    ///   warnings: QVariantList — 警告列表 [{field, message}, ...]
    ///   error: string — 解析失败时的错误信息（ok=false 时）
    Q_INVOKABLE QVariantMap requestPreview(const QString& path);

    /// 提交导入（写入文件 + 注册到 OperatorDescriptors）
    /// @param preview QVariantMap（requestPreview 返回的 map，需含 def.type）
    /// @return bool — true=成功；false=冲突/写入失败（错误通过 importFailed 信号发出）
    Q_INVOKABLE bool commitImport(const QVariantMap& preview);

    /// 删除已导入算子（含画布预检）
    /// @param type 算子 type
    /// @return QVariantMap 含字段：
    ///   ok: bool — 是否删除成功
    ///   error: string — 失败原因
    ///   blockedBy: int — 画布上使用该算子的节点数（>0 时阻断删除）
    Q_INVOKABLE QVariantMap removeImported(const QString& type);

    /// 列出所有已导入算子
    /// @return QVariantList 每个元素为 QVariantMap（OperatorDef::toMap() 的子集）
    Q_INVOKABLE QVariantList listImported() const;

    /// 已导入算子数量
    Q_INVOKABLE int importedCount() const;

    /// 获取指定算子的完整定义（供 QML 详情面板使用）
    /// @param type 算子 type
    /// @return QVariantMap（OperatorDef::toMap()；不存在时返回空）
    Q_INVOKABLE QVariantMap getImported(const QString& type) const;

signals:
    /// 算子导入/删除通知（转发自 Controller::importedChanged）
    /// action: "added" | "removed"
    void importedChanged(const QString& type, const QString& action);

    /// 导入失败通知（QML 端转 toast）
    void importFailed(const QString& message);

    /// 预览就绪通知（requestPreview 成功后发出，QML 端可监听刷新预览面板）
    void previewReady(const QVariantMap& preview);

private:
    /// 画布预检：检查画布上是否有节点使用指定 type
    /// @return 使用该 type 的节点数量（0=无依赖，可安全删除）
    int countCanvasUsage(const QString& type) const;

    EditViewBridge* m_editViewBridge;  ///< 弱引用（不拥有所有权）

    /// 预览缓存：type → ImportPreview
    /// requestPreview 时存入，commitImport 时取出（避免反序列化）
    QMap<QString, QDV::OperatorLibrary::ImportPreview> m_previewCache;
};

#endif // QDV_UI_OPERATOR_LIBRARY_BRIDGE_H
