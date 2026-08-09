#ifndef QDV_OPERATORLIBRARY_IMPORTER_H
#define QDV_OPERATORLIBRARY_IMPORTER_H

#include <QString>
#include "OperatorLibrary/OperatorDefinition.h"

namespace QDV {
namespace OperatorLibrary {

// 前向声明（detectConflicts 参数需要）
class OperatorRegistryStore;

/// 算子导入器（规范化与智能化）
/// 支持格式：
///  - .qdvop：本模块标准包（JSON，含完整 operator 定义）
///  - manifest.json：插件型算子清单（复用 OperatorManifest 字段 + 扩展）
///  - .dll：插件库（读取同目录 manifest 解析元数据）
/// 流程：解析 -> 校验 -> 智能识别元信息(可选) -> 冲突检测 -> 产出 ImportPreview
class OperatorImporter {
public:
    /// 从文件解析为导入预览（含冲突/警告）
    static ImportPreview importFile(const QString& path, QString* err = nullptr);
    /// 从 JSON 对象解析（供测试/内部调用）
    static ImportPreview importFromJson(const QJsonObject& obj, QString* err = nullptr);

    /// 与当前注册表比对，检测冲突（同名不同定义、版本降级、缺失依赖、端口不兼容）
    static QList<ValidationIssue> detectConflicts(const OperatorDef& def,
                                                   const OperatorRegistryStore* store);
};

} // namespace OperatorLibrary
} // namespace QDV

#endif // QDV_OPERATORLIBRARY_IMPORTER_H
