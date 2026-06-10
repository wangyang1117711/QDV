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