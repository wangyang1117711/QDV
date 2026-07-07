#ifndef QDV_IOPERATOR_H
#define QDV_IOPERATOR_H

#include "Core/VisionTool.h"
#include "OperatorSDK/IInferenceEngine.h"

namespace QDV {

/// 算子接口（继承 VisionTool 以保证与现有 ToolFactory 兼容）
/// 设计：新算子实现 IOperator，旧算子保持 VisionTool；两者均可被 ToolFactory 创建
class IOperator : public QDV::VisionTool {
public:
    /// 算子接口版本（用于动态库兼容性校验）
    virtual QString version() const = 0;

    /// 依赖注入 AI 引擎；默认实现空体（非 AI 算子无需重写）
    virtual void setInferenceEngine(IInferenceEngine* engine) { Q_UNUSED(engine); }

    /// 多态克隆，用于注册表创建实例
    virtual IOperator* clone() const = 0;
};

} // namespace QDV

#endif // QDV_IOPERATOR_H
