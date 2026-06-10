#include "ToolChainExecutor.h"
#include "Core/Logger.h"
#include <QtConcurrent/QtConcurrent>
#include <QThread>
#include <QCoreApplication>

using namespace QDV;

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
    // 先验证输入，再获取执行权（避免提前返回导致 m_running 残留）
    if (input.empty()) {
        Logger::error("Input image is empty");
        return false;
    }

    // 原子检查：防止多个线程同时执行工具链
    if (m_running.exchange(true)) {
        Logger::warn("ToolChainExecutor::execute() rejected — already running");
        return false;
    }

    if (QThread::currentThread() == QCoreApplication::instance()->thread()) {
        Logger::warn("ToolChainExecutor::execute() called from main thread, prefer executeAsync() to avoid UI blocking");
    }
    
    m_results.clear();
    m_running = true;
    
    cv::Mat currentInput = input.clone();
    int totalTools;
    QList<VisionTool*> toolsCopy;
    QMap<QString, BranchNode*> branchesCopy;
    {
        QMutexLocker locker(&m_mutex);
        m_results.clear();
        totalTools = m_tools.size();
        toolsCopy = m_tools;
        branchesCopy = m_branches;
    }
    
    int currentIndex = 0;
    
    for (VisionTool* tool : toolsCopy) {
        if (!m_running) {
            break;
        }
        
        ToolResult result;
        if (!executeTool(tool, currentInput, result)) {
            Logger::error("Tool execution failed: " + tool->name());
            continue;
        }
        
        {
            QMutexLocker locker(&m_mutex);
            m_results[tool->id()] = result;
        }
        emit toolExecuted(tool->id(), result);
        
        if (!result.overlayImage.empty()) {
            currentInput = result.overlayImage.clone();
        }
        
        currentIndex++;
        emit executionProgress(currentIndex, totalTools);
        
        if (branchesCopy.contains(tool->id())) {
            BranchNode* branch = branchesCopy[tool->id()];
            if (!evaluateBranch(branch)) {
                break;
            }
        }
    }
    
    m_running = false;
    emit chainCompleted(true);
    return true;
}

QFuture<bool> ToolChainExecutor::executeAsync(const cv::Mat& input) {
    return QtConcurrent::run([this, input]() {
        return execute(input);
    });
}

void ToolChainExecutor::stop() {
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
    QMutexLocker locker(&m_mutex);
    return m_results.value(toolId);
}