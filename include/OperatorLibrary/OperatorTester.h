#ifndef QDV_OPERATORLIBRARY_TESTER_H
#define QDV_OPERATORLIBRARY_TESTER_H

#include <QObject>
#include <QString>
#include "OperatorLibrary/OperatorDefinition.h"

namespace QDV {
namespace OperatorLibrary {

/// 算子测试与预览执行器
/// - config 算子：经 ToolChainExecutor 执行 recipe 组合
/// - plugin 算子：经 OperatorPluginLoader 加载 dll 并运行 IOperator
/// 返回输出图像路径与指标（mse/ssim/耗时），与 expectedOutput 按 tolerance 判定。
class OperatorTester : public QObject {
    Q_OBJECT
public:
    /// 同步执行（桩：返回未实现结果；后续接入真实执行链路）
    static TestResult run(const OperatorDef& def,
                          const QString& sampleInput,
                          QString* err = nullptr);

signals:
    void finished(const QJsonObject& result);
};

} // namespace OperatorLibrary
} // namespace QDV

#endif // QDV_OPERATORLIBRARY_TESTER_H
