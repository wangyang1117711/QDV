#include "UI/OperatorLibraryBridge.h"
#include "UI/EditViewBridge.h"          // 画布预检需要访问 currentNodes()
#include "OperatorLibrary/OperatorLibraryController.h"
#include "OperatorLibrary/OperatorDefinition.h"

#include <QDebug>

// =====================================================================
// OperatorLibraryBridge 实现（spec §2.2/§2.3 / tasks.md Task 6.1）
//
// 桥接流程：
//   requestPreview → Controller::importFile → 缓存 ImportPreview → 返回 QVariantMap
//   commitImport   → 从缓存取 ImportPreview → Controller::commitImport
//   removeImported → 画布预检 → Controller::removeImported
// =====================================================================

OperatorLibraryBridge::OperatorLibraryBridge(EditViewBridge* editViewBridge, QObject* parent)
    : QObject(parent)
    , m_editViewBridge(editViewBridge)
{
    // 转发 Controller::importedChanged 信号到 QML
    // instance() 返回指针，直接传入（QObject* 可隐式转换为信号槽连接的参数）
    connect(QDV::OperatorLibrary::OperatorLibraryController::instance(),
            &QDV::OperatorLibrary::OperatorLibraryController::importedChanged,
            this, &OperatorLibraryBridge::importedChanged);
}

OperatorLibraryBridge::~OperatorLibraryBridge() = default;

// ---------------------------------------------------------------------
// 请求预览导入文件
// ---------------------------------------------------------------------
QVariantMap OperatorLibraryBridge::requestPreview(const QString& path) {
    QVariantMap result;
    QString err;

    // 委托 Controller::importFile（解析+校验+查重 → ImportPreview）
    QDV::OperatorLibrary::ImportPreview preview =
        QDV::OperatorLibrary::OperatorLibraryController::instance()->importFile(path, &err);

    // 即使有冲突也返回预览数据（QML 端根据 conflicts 决定是否禁用提交按钮）
    result["ok"] = err.isEmpty();
    result["def"] = preview.def.toMap();

    // conflicts → QVariantList
    QVariantList conflicts;
    for (const auto& c : preview.conflicts) {
        QVariantMap m;
        m["field"] = c.field;
        m["message"] = c.message;
        conflicts.append(m);
    }
    result["conflicts"] = conflicts;

    // warnings → QVariantList
    QVariantList warnings;
    for (const auto& w : preview.warnings) {
        QVariantMap m;
        m["field"] = w.field;
        m["message"] = w.message;
        warnings.append(m);
    }
    result["warnings"] = warnings;

    if (!err.isEmpty()) {
        result["error"] = err;
    }

    // 缓存 ImportPreview（供 commitImport 使用）
    // 仅在 type 非空时缓存（解析失败时 type 可能为空）
    if (!preview.def.type.isEmpty()) {
        m_previewCache[preview.def.type] = preview;
    }

    // 通知 QML 预览就绪
    emit previewReady(result);

    return result;
}

// ---------------------------------------------------------------------
// 提交导入
// ---------------------------------------------------------------------
bool OperatorLibraryBridge::commitImport(const QVariantMap& preview) {
    // 从 QVariantMap 中提取 type
    const QString type = preview.value("def").toMap().value("type").toString();
    if (type.isEmpty()) {
        emit importFailed(QStringLiteral("提交导入失败：preview 中缺少 type 字段"));
        return false;
    }

    // 从缓存中取出原始 ImportPreview
    auto it = m_previewCache.find(type);
    if (it == m_previewCache.end()) {
        emit importFailed(QStringLiteral("提交导入失败：未找到 type='%1' 的预览缓存，请重新选择文件").arg(type));
        return false;
    }

    // 委托 Controller::commitImport（含 TOCTOU 防护 + 事务回滚）
    QString err;
    const bool ok = QDV::OperatorLibrary::OperatorLibraryController::instance()->commitImport(it.value(), &err);

    if (!ok) {
        emit importFailed(err.isEmpty() ? QStringLiteral("提交导入失败") : err);
        return false;
    }

    // 成功：清除缓存
    m_previewCache.remove(type);

    return true;
}

// ---------------------------------------------------------------------
// 删除已导入算子（含画布预检）
// ---------------------------------------------------------------------
QVariantMap OperatorLibraryBridge::removeImported(const QString& type) {
    QVariantMap result;
    result["ok"] = false;

    // 画布预检：检查画布上是否有节点使用该 type
    const int usage = countCanvasUsage(type);
    if (usage > 0) {
        result["error"] = QStringLiteral("画布上存在 %1 个使用该算子的节点，请先删除").arg(usage);
        result["blockedBy"] = usage;
        return result;
    }

    // 委托 Controller::removeImported
    const QVariantMap ctrlResult =
        QDV::OperatorLibrary::OperatorLibraryController::instance()->removeImported(type);

    result["ok"] = ctrlResult.value("ok", false);
    if (!ctrlResult.value("ok").toBool()) {
        result["error"] = ctrlResult.value("error", QStringLiteral("删除失败"));
    }

    return result;
}

// ---------------------------------------------------------------------
// 列出所有已导入算子
// ---------------------------------------------------------------------
QVariantList OperatorLibraryBridge::listImported() const {
    // Controller::query(空分类, 空关键词) 返回全部
    return QDV::OperatorLibrary::OperatorLibraryController::instance()->query(QString(), QString());
}

// ---------------------------------------------------------------------
// 已导入算子数量
// ---------------------------------------------------------------------
int OperatorLibraryBridge::importedCount() const {
    return listImported().size();
}

// ---------------------------------------------------------------------
// 获取指定算子的完整定义
// ---------------------------------------------------------------------
QVariantMap OperatorLibraryBridge::getImported(const QString& type) const {
    return QDV::OperatorLibrary::OperatorLibraryController::instance()->getOperator(type);
}

// ---------------------------------------------------------------------
// 画布预检：统计画布上使用指定 type 的节点数量
// ---------------------------------------------------------------------
int OperatorLibraryBridge::countCanvasUsage(const QString& type) const {
    if (!m_editViewBridge || type.isEmpty()) return 0;

    int count = 0;
    const QVariantList nodes = m_editViewBridge->currentNodes();
    for (const QVariant& v : nodes) {
        const QVariantMap node = v.toMap();
        if (node.value("type").toString() == type) {
            ++count;
        }
    }
    return count;
}
