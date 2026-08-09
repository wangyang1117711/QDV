#ifndef QDV_OPERATORLIBRARY_VALIDATOR_H
#define QDV_OPERATORLIBRARY_VALIDATOR_H

// =====================================================================
// OperatorValidator — 算子定义字段校验器
//
// 设计契约（spec §3.2 + §5.4）：
//   - type:         ^[A-Za-z][A-Za-z0-9_]{2,63}$  （大小写敏感）
//   - cnName:       非空 + ≤32 字符
//   - category:     建议命中现有分类（参考 DesignTokens.qml categoryColor()），未命中仅 Warning
//   - version:      ^\d+\.\d+\.\d+$  （SemVer 简化版）
//   - inputs:       至少 1 个 + 每个 name 唯一
//   - outputs:      至少 1 个 + 每个 name 唯一
//   - params:       可为空 + 每个 name 唯一 + Enum 必有 options
//   - implementation: recipe 与 library 至少一项非空
//   - recipe 中每个 type: 在 OperatorDescriptors::allTypes() 中存在（Warning，由 Importer 层做）
//
// 不在本切片（O1a）：
//   - 端口类型受控词表强制校验（仅 Warning）
//   - 参数 min/max/default 一致性校验（推迟到 O2）
//   - 循环依赖检测（O2，依赖 OperatorDependencyGraph）
// =====================================================================

#include <QString>
#include <QStringList>
#include "OperatorLibrary/OperatorDefinition.h"

namespace QDV {
namespace OperatorLibrary {

class OperatorValidator {
public:
    /// 完整校验 OperatorDef，返回 ValidationResult（含所有 Error/Warning）
    static ValidationResult validate(const OperatorDef& def);

    /// 校验 JSON 对象（先尝试 fromJson 再校验，err 内附解析错误）
    static ValidationResult validateJson(const QJsonObject& obj, QString* parseErr = nullptr);

    // ---------- 命名规范（spec §3.2 + §5.4）----------
    /// type 命名：^[A-Za-z][A-Za-z0-9_]{2,63}$（字母开头，3-64 字符，含下划线，大小写敏感）
    static bool isTypeName(const QString& s);
    /// 参数名命名：^[a-z][a-zA-Z0-9]*$（小写驼峰，仅供 Warning 提示，不阻断）
    static bool isParamName(const QString& s);
    /// 语义化版本：^\d+\.\d+\.\d+$
    static bool isSemVer(const QString& s);

    // ---------- 受控词表（仅 Warning 级别）----------
    /// 算子分类建议词表（来自 DesignTokens.qml categoryColor()）
    static QStringList categoryVocabulary();
    /// 端口类型词表（Image/Mat/Number/Region/Contour/Any 等开放词表，仅 Warning）
    static QStringList portTypeVocabulary();
};

} // namespace OperatorLibrary
} // namespace QDV

#endif // QDV_OPERATORLIBRARY_VALIDATOR_H
