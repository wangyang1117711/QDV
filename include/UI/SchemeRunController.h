#ifndef SCHEME_RUN_CONTROLLER_H
#define SCHEME_RUN_CONTROLLER_H

// ============================================================================
// SchemeRunController —— 方案运行控制器（从 EditViewBridge 拆分，Task 6）
// ----------------------------------------------------------------------------
// 职责：
//   1. 整链运行 runScheme / runSchemeAsync（含输入图像解析、工具链构建、执行）
//   2. 单算子运行 runSingleOperator / runSingleOperatorAsync（含上游链计算）
//   3. 输入源解析 resolveInputImageForNode / isInputImageRequired /
//      resolveRunInput / resolveSchemeRunInput（统一判断是否需要用户选择图像）
//   4. 内部辅助：buildToolChainFromNodes / computeUpstreamChain / saveMatToTempPng
//
// 设计要点：
//   - 持有 EditViewBridge* 弱引用，通过其 public 接口访问 currentNodes()/
//     connections()/cameraFramePath()/variableManager()/imageVariableManager()。
//   - 异步执行状态（m_schemeWatcher/m_singleOpWatcher/m_schemeRunning/
//     m_singleOpRunning）由本类持有，EditViewBridge::isSingleOperatorRunning 委托查询。
//   - 通过 Qt 信号转发机制将 errorRaised / schemeDeploy* / singleOperator*
//     回传给 EditViewBridge（保持其信号契约与 phase 字符串不变）。
// ============================================================================

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QFutureWatcher>

class EditViewBridge;

namespace QDV { class VisionTool; }
namespace cv { class Mat; }

class SchemeRunController : public QObject {
    Q_OBJECT
public:
    explicit SchemeRunController(EditViewBridge* bridge, QObject* parent = nullptr);
    ~SchemeRunController() override;

    // === 整链运行 ===
    QVariantMap runScheme(const QString& inputImagePath);
    void runSchemeAsync(const QString& inputImagePath);

    // === 单算子运行 ===
    QVariantMap runSingleOperator(const QString& nodeId, const QString& inputImagePath);
    void runSingleOperatorAsync(const QString& nodeId, const QString& inputImagePath);

    bool isSingleOperatorRunning() const { return m_singleOpRunning; }

    // === 输入源解析 ===
    QString resolveInputImageForNode(const QString& nodeId) const;
    bool isInputImageRequired(const QString& nodeId) const;
    QVariantMap resolveRunInput(const QString& nodeId) const;
    QVariantMap resolveSchemeRunInput() const;

signals:
    void errorRaised(const QString& phase, const QString& message);
    void schemeDeployStarted();
    void schemeDeployFinished(const QVariantMap& result);
    void singleOperatorStarted();
    void singleOperatorFinished(const QVariantMap& result);

private:
    // 将 m_currentNodes 的子集转换为 VisionTool 列表（调用方拥有所有权）
    QList<QDV::VisionTool*> buildToolChainFromNodes(const QStringList& nodeIds) const;
    // 计算目标节点的上游链（拓扑有序，含目标节点本身）
    QStringList computeUpstreamChain(const QString& targetNodeId) const;
    // 递归收集上游节点（DFS，拓扑序）
    void collectUpstreamNodes(const QString& nodeId, QStringList& result, QSet<QString>& visited) const;
    // 保存 cv::Mat 到临时 PNG 文件
    static QString saveMatToTempPng(const cv::Mat& image, const QString& prefix);

    EditViewBridge* m_bridge;
    QFutureWatcher<QVariantMap>* m_schemeWatcher = nullptr;
    bool m_schemeRunning = false;
    QFutureWatcher<QVariantMap>* m_singleOpWatcher = nullptr;
    bool m_singleOpRunning = false;
};

#endif // SCHEME_RUN_CONTROLLER_H
