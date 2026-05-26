#include "ToolChainExecutor.h"
#include "Logger.h"

ToolChainExecutor::ToolChainExecutor(QObject* parent) : QObject(parent) {
}

void ToolChainExecutor::setTools(const QList<VisionTool*>& tools) {
    QMutexLocker locker(&m_mutex);
    m_tools = tools;
}

void ToolChainExecutor::setBranches(const QMap<QString, BranchNode*>& branches) {
    QMutexLocker locker(&m_mutex);
    m_branches = branches;
}

bool ToolChainExecutor::execute(const cv::Mat& input) {
    QMutexLocker locker(&m_mutex);
    
    if (input.empty()) {
        Logger::error("Input image is empty");
        return false;
    }
    
    m_running = true;
    m_results.clear();
    
    cv::Mat currentInput = input.clone();
    int totalTools = m_tools.size();
    int currentIndex = 0;
    
    for (VisionTool* tool : m_tools) {
        if (!m_running) {
            break;
        }
        
        ToolResult result;
        if (!executeTool(tool, currentInput, result)) {
            Logger::error("Tool execution failed: " + tool->name());
            continue;
        }
        
        m_results[tool->id()] = result;
        emit toolExecuted(tool->id(), result);
        
        if (!result.overlayImage.empty()) {
            currentInput = result.overlayImage.clone();
        }
        
        currentIndex++;
        emit executionProgress(currentIndex, totalTools);
        
        if (m_branches.contains(tool->id())) {
            BranchNode* branch = m_branches[tool->id()];
            if (!evaluateBranch(branch)) {
                break;
            }
        }
    }
    
    m_running = false;
    emit chainCompleted(true);
    return true;
}

void ToolChainExecutor::stop() {
    QMutexLocker locker(&m_mutex);
    m_running = false;
}

bool ToolChainExecutor::executeTool(VisionTool* tool, const cv::Mat& input, ToolResult& result) {
    qint64 startTime = QDateTime::currentMSecsSinceEpoch();
    
    bool success = tool->execute(input, result);
    
    qint64 endTime = QDateTime::currentMSecsSinceEpoch();
    result.elapsedMs = endTime - startTime;
    
    return success;
}

bool ToolChainExecutor::evaluateBranch(const BranchNode* branch) {
    if (!branch) {
        return true;
    }
    
    if (!m_results.contains(branch->sourceToolId)) {
        return true;
    }
    
    const ToolResult& sourceResult = m_results[branch->sourceToolId];
    bool conditionResult = false;
    
    if (branch->conditionOp == "==") {
        conditionResult = sourceResult.ok == branch->conditionValue.toBool();
    } else if (branch->conditionOp == ">") {
        conditionResult = sourceResult.score > branch->conditionValue.toDouble();
    } else if (branch->conditionOp == "<") {
        conditionResult = sourceResult.score < branch->conditionValue.toDouble();
    }
    
    return conditionResult;
}

ToolResult ToolChainExecutor::getResult(const QString& toolId) const {
    QMutexLocker locker(const_cast<QMutex*>(&m_mutex));
    return m_results.value(toolId);
}