#include "OperatorLibrary/OperatorImporter.h"

// =====================================================================
// OperatorImporter 实现
// 实现契约（spec §2.2 + tasks.md Task 3.1）
//
// 流程：
//   1. 文件读取（QFile）
//   2. JSON 解析（QJsonDocument::fromJson + 行号）
//   3. OperatorDef::fromJson
//   4. Validator::validate（issues 转 conflicts/warnings）
//   5. detectConflicts（type 查重，与 RegistryStore 比对）
//   6. 产 ImportPreview{def, conflicts, warnings}
//
// 不在本切片（O1a）：
//   - recipe 引用未知算子检查（spec §1.4：避免 OperatorLibrary 依赖 UI，
//     此检查移到 Controller 层通过 type checker 回调注入完成）
//   - 端口兼容性检查（O2）
//   - 版本降级检查（O1e）
// =====================================================================

#include "OperatorLibrary/OperatorValidator.h"
#include "OperatorLibrary/OperatorRegistryStore.h"

#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>

namespace QDV {
namespace OperatorLibrary {

// ---------------------------------------------------------------------
// 从文件导入（解析+校验+查重 → ImportPreview）
// ---------------------------------------------------------------------
ImportPreview OperatorImporter::importFile(const QString& path, QString* err) {
    ImportPreview preview;

    // 1. 文件存在性 + 读权限
    QFileInfo fi(path);
    if (!fi.exists() || !fi.isFile()) {
        const QString msg = QStringLiteral("文件不存在: %1").arg(path);
        if (err) *err = msg;
        ValidationIssue issue;
        issue.level = ValidationIssue::Error;
        issue.field = QStringLiteral("file");
        issue.message = msg;
        preview.conflicts.append(issue);
        return preview;
    }

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        const QString msg = QStringLiteral("无法读取文件: %1 (%2)").arg(path, f.errorString());
        if (err) *err = msg;
        ValidationIssue issue;
        issue.level = ValidationIssue::Error;
        issue.field = QStringLiteral("file");
        issue.message = msg;
        preview.conflicts.append(issue);
        return preview;
    }
    const QByteArray data = f.readAll();
    f.close();

    // 异常大文件警告（spec §5.4：>1MB 不限制但日志警告）
    if (data.size() > 1024 * 1024) {
        qWarning().noquote() << "[OperatorImporter] 异常大的算子定义文件:" << path
                             << "大小:" << data.size() << "bytes";
    }

    // 2. JSON 解析
    QJsonParseError parseErr;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseErr);
    if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
        // 估算行号：parseErr.offset 是字节偏移，按 \n 计数得行号
        int line = 1;
        for (int i = 0; i < parseErr.offset && i < data.size(); ++i) {
            if (data.at(i) == '\n') ++line;
        }
        const QString msg = QStringLiteral("JSON 格式错误（行 %1）: %2")
                                .arg(line).arg(parseErr.errorString());
        if (err) *err = msg;
        ValidationIssue issue;
        issue.level = ValidationIssue::Error;
        issue.field = QStringLiteral("json");
        issue.message = msg;
        preview.conflicts.append(issue);
        return preview;
    }

    if (err) err->clear();
    return importFromJson(doc.object(), err);
}

// ---------------------------------------------------------------------
// 从 JSON 对象导入（跳过文件读取）
// ---------------------------------------------------------------------
ImportPreview OperatorImporter::importFromJson(const QJsonObject& obj, QString* err) {
    ImportPreview preview;

    // 3. OperatorDef::fromJson
    QString parseErr;
    OperatorDef def = OperatorDef::fromJson(obj, &parseErr);
    if (def.type.isEmpty()) {
        const QString msg = parseErr.isEmpty()
                                ? QStringLiteral("type 缺失")
                                : parseErr;
        if (err) *err = msg;
        ValidationIssue issue;
        issue.level = ValidationIssue::Error;
        issue.field = QStringLiteral("type");
        issue.message = msg;
        preview.conflicts.append(issue);
        return preview;
    }

    preview.def = def;

    // 4. Validator::validate（Error 转 conflicts，Warning 转 warnings）
    const ValidationResult vr = OperatorValidator::validate(def);
    for (const ValidationIssue& i : vr.issues) {
        if (i.level == ValidationIssue::Error) {
            preview.conflicts.append(i);
        } else {
            preview.warnings.append(i);
        }
    }

    if (err) err->clear();
    return preview;
}

// ---------------------------------------------------------------------
// 冲突检测（与注册表比对）
// ---------------------------------------------------------------------
QList<ValidationIssue> OperatorImporter::detectConflicts(const OperatorDef& def,
                                                          const OperatorRegistryStore* store) {
    QList<ValidationIssue> issues;
    if (!store) return issues;

    // type 查重（与已导入算子）
    if (store->contains(def.type)) {
        ValidationIssue i;
        i.level = ValidationIssue::Error;
        i.field = QStringLiteral("type");
        i.message = QStringLiteral("type '%1' 已存在（已导入算子）").arg(def.type);
        issues.append(i);
    }

    return issues;
}

} // namespace OperatorLibrary
} // namespace QDV
