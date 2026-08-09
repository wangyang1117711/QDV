#include "OperatorLibrary/OperatorValidator.h"

// =====================================================================
// OperatorValidator 实现
// 实现契约（spec §3.2 + §5.4 + tasks.md Task 2.1）
// 注意：recipe 引用未知算子检查由 Importer 层做（避免 OperatorLibrary 依赖 UI）
// =====================================================================

#include <QRegularExpression>
#include <QSet>

namespace QDV {
namespace OperatorLibrary {

// 编译期构造正则（线程安全的隐式共享）
static const QRegularExpression reTypeName(QStringLiteral("^[A-Za-z][A-Za-z0-9_]{2,63}$"));
static const QRegularExpression reParamName(QStringLiteral("^[a-z][a-zA-Z0-9]*$"));
static const QRegularExpression reSemVer(QStringLiteral("^\\d+\\.\\d+\\.\\d+$"));

// ---------------------------------------------------------------------
// 命名规范
// ---------------------------------------------------------------------
bool OperatorValidator::isTypeName(const QString& s) {
    return reTypeName.match(s).hasMatch();
}

bool OperatorValidator::isParamName(const QString& s) {
    return reParamName.match(s).hasMatch();
}

bool OperatorValidator::isSemVer(const QString& s) {
    return reSemVer.match(s).hasMatch();
}

// ---------------------------------------------------------------------
// 受控词表
// ---------------------------------------------------------------------
QStringList OperatorValidator::categoryVocabulary() {
    // 与 DesignTokens.qml categoryColor() 中的分类列表一致
    return {
        QStringLiteral("图像采集"), QStringLiteral("预处理"),
        QStringLiteral("几何变换"), QStringLiteral("形态学"),
        QStringLiteral("图像分割"), QStringLiteral("Blob分析"),
        QStringLiteral("特征提取"), QStringLiteral("匹配定位"),
        QStringLiteral("几何测量"), QStringLiteral("3D视觉"),
        QStringLiteral("深度学习"), QStringLiteral("分支")
    };
}

QStringList OperatorValidator::portTypeVocabulary() {
    // 开放词表，仅用于 Warning 提示，不阻断
    return { QStringLiteral("Image"), QStringLiteral("Mat"),
             QStringLiteral("Number"), QStringLiteral("Region"),
             QStringLiteral("Contour"), QStringLiteral("Any") };
}

// ---------------------------------------------------------------------
// 完整校验
// ---------------------------------------------------------------------
ValidationResult OperatorValidator::validate(const OperatorDef& def) {
    ValidationResult r;

    // ----- type 校验（Error） -----
    if (def.type.isEmpty()) {
        r.addError(QStringLiteral("type"), QStringLiteral("type 不能为空"));
    } else if (!isTypeName(def.type)) {
        r.addError(QStringLiteral("type"),
                   QStringLiteral("type '%1' 不符合命名规范 ^[A-Za-z][A-Za-z0-9_]{2,63}$").arg(def.type));
    }

    // ----- cnName 校验（Error） -----
    if (def.cnName.isEmpty()) {
        r.addError(QStringLiteral("cnName"), QStringLiteral("cnName 不能为空"));
    } else if (def.cnName.length() > 32) {
        r.addError(QStringLiteral("cnName"),
                   QStringLiteral("cnName 长度 %1 超过 32 字符").arg(def.cnName.length()));
    }

    // ----- category 校验（空=Error，未命中词表=Warning） -----
    if (def.category.isEmpty()) {
        r.addError(QStringLiteral("category"), QStringLiteral("category 不能为空"));
    } else if (!categoryVocabulary().contains(def.category)) {
        r.addWarning(QStringLiteral("category"),
                     QStringLiteral("category '%1' 不在建议词表中，UI 可能无法正确着色").arg(def.category));
    }

    // ----- version 校验（Error） -----
    if (def.version.isEmpty()) {
        r.addError(QStringLiteral("version"), QStringLiteral("version 不能为空"));
    } else if (!isSemVer(def.version)) {
        r.addError(QStringLiteral("version"),
                   QStringLiteral("version '%1' 不符合 SemVer 格式 ^\\d+\\.\\d+\\.\\d+$").arg(def.version));
    }

    // ----- inputs 校验：至少 1 个 + name 唯一（Error） -----
    if (def.inputs.isEmpty()) {
        r.addError(QStringLiteral("inputs"), QStringLiteral("inputs 至少需要 1 个输入端口"));
    } else {
        QSet<QString> seen;
        for (const InputPort& p : def.inputs) {
            if (p.name.isEmpty()) {
                r.addError(QStringLiteral("inputs"), QStringLiteral("存在 name 为空的 input 端口"));
                continue;
            }
            if (seen.contains(p.name)) {
                r.addError(QStringLiteral("inputs"),
                           QStringLiteral("input 端口 name '%1' 重复").arg(p.name));
            } else {
                seen.insert(p.name);
            }
        }
    }

    // ----- outputs 校验：至少 1 个 + name 唯一（Error） -----
    if (def.outputs.isEmpty()) {
        r.addError(QStringLiteral("outputs"), QStringLiteral("outputs 至少需要 1 个输出端口"));
    } else {
        QSet<QString> seen;
        for (const OutputPort& p : def.outputs) {
            if (p.name.isEmpty()) {
                r.addError(QStringLiteral("outputs"), QStringLiteral("存在 name 为空的 output 端口"));
                continue;
            }
            if (seen.contains(p.name)) {
                r.addError(QStringLiteral("outputs"),
                           QStringLiteral("output 端口 name '%1' 重复").arg(p.name));
            } else {
                seen.insert(p.name);
            }
        }
    }

    // ----- params 校验：name 唯一 + Enum 必有 options（Error） + 参数名命名（Warning） -----
    {
        QSet<QString> seen;
        for (const ParamDef& p : def.params) {
            if (p.name.isEmpty()) {
                r.addError(QStringLiteral("params"), QStringLiteral("存在 name 为空的 param"));
                continue;
            }
            if (seen.contains(p.name)) {
                r.addError(QStringLiteral("params"),
                           QStringLiteral("param name '%1' 重复").arg(p.name));
            } else {
                seen.insert(p.name);
            }
            // Enum 类型必须有 options
            if (p.type == ParamType::Enum && p.options.isEmpty()) {
                r.addError(QStringLiteral("params"),
                           QStringLiteral("param '%1' 为 Enum 但未定义 options").arg(p.name));
            }
            // 参数名命名规范（Warning，不阻断）
            if (!isParamName(p.name)) {
                r.addWarning(QStringLiteral("params"),
                             QStringLiteral("param name '%1' 不符合小写驼峰规范（建议）").arg(p.name));
            }
        }
    }

    // ----- implementation 校验：recipe/script/library 至少一项非空（Error） -----
    {
        const bool hasRecipe   = !def.implementation.recipe.isEmpty();
        const bool hasScript   = !def.implementation.script.isEmpty();
        const bool hasLibrary  = !def.implementation.library.isEmpty();
        if (!hasRecipe && !hasScript && !hasLibrary) {
            r.addError(QStringLiteral("implementation"),
                       QStringLiteral("implementation 的 recipe/script/library 至少一项非空"));
        }
    }

    return r;
}

// ---------------------------------------------------------------------
// JSON 校验（先反序列化再校验）
// ---------------------------------------------------------------------
ValidationResult OperatorValidator::validateJson(const QJsonObject& obj, QString* parseErr) {
    QString err;
    OperatorDef def = OperatorDef::fromJson(obj, &err);
    if (def.type.isEmpty()) {
        // fromJson 失败：type 缺失或更早的解析错误
        const QString reason = err.isEmpty() ? QStringLiteral("type 缺失") : err;
        if (parseErr) *parseErr = reason;
        ValidationResult r;
        r.addError(QStringLiteral("type"), reason);
        return r;
    }
    if (parseErr) parseErr->clear();
    return validate(def);
}

} // namespace OperatorLibrary
} // namespace QDV
