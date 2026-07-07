#ifndef QDV_REGISTER_MACROS_H
#define QDV_REGISTER_MACROS_H

#include "OperatorSDK/IOperatorRegistry.h"

/// 算子自注册宏（静态变量自注册模式，参考 ToolFactory.h 的 REGISTER_TOOL）
/// 用法：在算子 .cpp 文件中 REGISTER_OPERATOR("Histogram", "1.0.0", HistogramOperator)
/// 注意：放在全局命名空间
#define REGISTER_OPERATOR(typeStr, versionStr, className) \
    static bool _op_registered_##className = []() { \
        QDV::IOperatorRegistry::instance().registerOperator( \
            typeStr, versionStr, []() -> QDV::IOperator* { return new className(); }); \
        return true; \
    }()

#endif // QDV_REGISTER_MACROS_H
