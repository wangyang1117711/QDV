#ifndef TOOLCHAINEXECUTOR_H
#define TOOLCHAINEXECUTOR_H

#include <QObject>
#include <QList>
#include <QMap>
#include <QThread>
#include <QMutex>
#include "VisionTool.h"
#include "BranchNode.h"

class ToolChainExecutor : public QObject {
    Q_OBJECT
    
public:
    explicit ToolChainExecutor(QObject* parent = nullptr);
    
    void setTools(const QList<VisionTool*>& tools);
    void setBranches(const QMap<QString, BranchNode*>& branches);
    
    bool execute(const cv::Mat& input);
    void stop();
    
    QMap<QString, ToolResult> getResults() const { return m_results; }
    ToolResult getResult(const QString& toolId) const;
    
signals:
    void toolExecuted(const QString& toolId, const ToolResult& result);
    void chainCompleted(bool success);
    void executionProgress(int current, int total);
    
private:
    QList<VisionTool*> m_tools;
    QMap<QString, BranchNode*> m_branches;
    QMap<QString, ToolResult> m_results;
    bool m_running = false;
    QMutex m_mutex;
    
    bool executeTool(VisionTool* tool, const cv::Mat& input, ToolResult& result);
    bool evaluateBranch(const BranchNode* branch);
    QList<QString> getNextToolIds(VisionTool* currentTool);
};

#endif // TOOLCHAINEXECUTOR_H