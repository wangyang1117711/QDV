#ifndef QDV_OPERATORLIBRARY_METADATAINFERRER_H
#define QDV_OPERATORLIBRARY_METADATAINFERRER_H

#include <QString>
#include "OperatorLibrary/OperatorDefinition.h"

namespace QDV {
namespace OperatorLibrary {

/// 算子元信息智能识别器
/// 针对导入的模块（manifest / dll / 示例数据），尽力推断：
///  - 输入/输出端口类型
///  - 依赖关系（引用的已存在算子）
///  - 识别依据说明（hints）
/// 目的：减少手动配置，自动补全定义。
class OperatorMetadataInferrer {
public:
    static InferredMeta infer(const QString& modulePath, QString* err = nullptr);
    /// 从已有 OperatorDef 推断缺失的端口/依赖（轻量启发式）
    static InferredMeta inferFromDef(const OperatorDef& def);
};

} // namespace OperatorLibrary
} // namespace QDV

#endif // QDV_OPERATORLIBRARY_METADATAINFERRER_H
