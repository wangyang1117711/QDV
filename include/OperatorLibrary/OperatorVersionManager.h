#ifndef QDV_OPERATORLIBRARY_VERSIONMANAGER_H
#define QDV_OPERATORLIBRARY_VERSIONMANAGER_H

#include <QString>
#include "OperatorLibrary/OperatorDefinition.h"

namespace QDV {
namespace OperatorLibrary {

/// 算子版本管理器
/// 遵循 SemVer 2.0.0；每次创建/发布/重大编辑生成不可变版本快照；
/// 回滚=基于历史快照创建新版本；提供变更摘要。
class OperatorVersionManager {
public:
    static QString bumpMajor(const QString& v);
    static QString bumpMinor(const QString& v);
    static QString bumpPatch(const QString& v);

    /// 生成版本快照记录
    static VersionRecord snapshot(const OperatorDef& def,
                                  const QString& author,
                                  const QString& comment);

    /// a 是否比 b 旧（a < b）
    static bool isDowngrade(const QString& a, const QString& b);

    /// 生成两个定义间的变更摘要（供 UI diff 展示）
    static QString diffSummary(const OperatorDef& a, const OperatorDef& b);
};

} // namespace OperatorLibrary
} // namespace QDV

#endif // QDV_OPERATORLIBRARY_VERSIONMANAGER_H
