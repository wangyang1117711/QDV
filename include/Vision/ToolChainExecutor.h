#ifndef TOOLCHAINEXECUTOR_H
#define TOOLCHAINEXECUTOR_H

#include <QObject>
#include <QList>
#include <QMap>
#include <QThread>
#include <QMutex>
#include <QFuture>
#include <atomic>
#include "VisionTool.h"
#include "BranchNode.h"

// P1-B12 架构约束（文档化）：
// 当前 ToolChainExecutor::execute 在调用线程同步执行工具链，
// executeAsync 通过 QtConcurrent::run 在线程池执行。
// 但各 VisionTool 实例的内部状态（如 ImageMergeTool::m_storedImage、
// ImageArithmeticTool::m_secondImage）在多次 execute 间共享，非线程安全。
// 异步并发执行同一 Tool 实例会导致状态竞争。
// 调用方约束：若需并发执行多条工具链，必须为每条链创建独立的 Tool 实例
// （通过 ToolFactory::createTool 创建新实例，而非共享同一指针）。
// 用户已决策：P1-B 阶段不实现自动状态隔离，仅文档化约束。
class ToolChainExecutor : public QObject {
    Q_OBJECT

public:
    explicit ToolChainExecutor(QObject* parent = nullptr);
    
    void setTools(const QList<QDV::VisionTool*>& tools);
    void setBranches(const QMap<QString, BranchNode*>& branches);
    
    bool execute(const cv::Mat& input);
    QFuture<bool> executeAsync(const cv::Mat& input);
    void stop();
    
    QMap<QString, ToolResult> getResults() const { return m_results; }
    ToolResult getResult(const QString& toolId) const;
    
signals:
    void toolExecuted(const QString& toolId, const ToolResult& result);
    void chainCompleted(bool success);
    void executionProgress(int current, int total);
    /// P1-A2 修复：单个工具执行失败信号（供上层桥接到 errorRaised）
    void toolFailed(const QString& toolId, const QString& errorMessage);
    
private:
    // NON-OWNING: tools are owned by Scheme. Do not delete.
    QList<QDV::VisionTool*> m_tools;
    QMap<QString, BranchNode*> m_branches;
    QMap<QString, ToolResult> m_results;
    std::atomic<bool> m_running{false};
    mutable QMutex m_mutex;
    
    bool executeTool(QDV::VisionTool* tool, const cv::Mat& input, ToolResult& result);
    bool evaluateBranch(const BranchNode* branch);
    QList<QString> getNextToolIds(QDV::VisionTool* currentTool);
};

#endif // TOOLCHAINEXECUTOR_H