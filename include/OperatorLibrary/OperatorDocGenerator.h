#ifndef QDV_OPERATORLIBRARY_DOCGENERATOR_H
#define QDV_OPERATORLIBRARY_DOCGENERATOR_H

#include <QString>
#include "OperatorLibrary/OperatorDefinition.h"

namespace QDV {
namespace OperatorLibrary {

/// 算子文档自动生成器
/// 根据 OperatorDef 生成 Markdown / HTML 使用说明：
/// 概述、输入/输出端口表、超参表、依赖、版本历史、示例、权限。
class OperatorDocGenerator {
public:
    static QString generateMarkdown(const OperatorDef& def);
    static QString generateHtml(const OperatorDef& def);
};

} // namespace OperatorLibrary
} // namespace QDV

#endif // QDV_OPERATORLIBRARY_DOCGENERATOR_H
