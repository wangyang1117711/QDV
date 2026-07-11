#include "ToolChainExecutor.h"
#include "Core/Logger.h"
#include <QtConcurrent/QtConcurrent>
#include <QThread>
#include <QCoreApplication>
#include <opencv2/core.hpp>    // P0 修复：cv::Exception

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
        // v5.3.2：降级为 debug，避免 PreviewManager 预览时刷屏
        // （PreviewManager.doPreview 已通过 QtConcurrent::run 在子线程执行，
        //  但某些同步调用路径如 runScheme 仍可能在主线程触发，属已知情况）
        Logger::info("ToolChainExecutor::execute() running on main thread (consider using async API)");
    }
    
    m_running = true;

    cv::Mat currentInput = input.clone();
    int totalTools;
    QList<VisionTool*> toolsCopy;
    QMap<QString, BranchNode*> branchesCopy;
    {
        QMutexLocker locker(&m_mutex);
        // P1-B13 修复：m_results.clear() 移到锁内，避免与 getResult() 数据竞争
        // 之前 line 39 锁外 clear + line 48 锁内 clear 重复，删除锁外那次
        m_results.clear();
        totalTools = m_tools.size();
        toolsCopy = m_tools;
        branchesCopy = m_branches;
    }
    
    int currentIndex = 0;
    int failCount = 0;   // P1-A2 修复：统计失败工具数，链失败时返回 false

    for (VisionTool* tool : toolsCopy) {
        if (!m_running) {
            break;
        }

        ToolResult result;
        if (!executeTool(tool, currentInput, result)) {
            Logger::error("Tool execution failed: " + tool->name());
            // P1-A2 修复：之前仅 continue 且最后无条件 return true，调用方认为链成功
            failCount++;
            emit toolFailed(tool->id(), QStringLiteral("工具执行失败: %1").arg(tool->name()));
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
    // P1-A2 修复：根据失败数决定返回值，链失败时通知调用方
    const bool overallSuccess = (failCount == 0) && (currentIndex > 0 || totalTools == 0);
    if (!overallSuccess) {
        Logger::error(QStringLiteral("ToolChain execution completed with %1/%2 failures")
                          .arg(failCount).arg(totalTools));
    }
    emit chainCompleted(overallSuccess);
    return overallSuccess;
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
    // P1-C3 修复（PreReleaseReviewReport Minor 12）：空指针防御
    // 之前直接 tool->execute，若 setTools 传入含 nullptr 的列表会崩溃
    if (!tool) {
        Logger::error("ToolChainExecutor::executeTool: null tool pointer");
        result.ok = false;
        return false;
    }
    if (input.empty()) {
        Logger::error(QString("ToolChainExecutor::executeTool: empty input for tool %1")
            .arg(tool->name()));
        result.ok = false;
        return false;
    }

    qint64 startTime = QDateTime::currentMSecsSinceEpoch();

    // P0 修复：try-catch 捕获 OpenCV 异常，防止参数校验遗漏导致程序崩溃。
    // 之前 cv::Canny/cv::threshold 等函数在参数非法时抛出 cv::Exception，
    // 未被捕获直接导致应用闪退。
    bool success = false;
    try {
        success = tool->execute(input, result);
    } catch (const cv::Exception& e) {
        Logger::error(QString("ToolChainExecutor::executeTool: OpenCV异常 in %1: %2")
            .arg(tool->name()).arg(QString::fromStdString(e.what())));
        result.ok = false;
        result.data["error"] = QString("OpenCV错误: %1").arg(QString::fromStdString(e.what()));
        success = false;
    } catch (const std::exception& e) {
        Logger::error(QString("ToolChainExecutor::executeTool: 标准异常 in %1: %2")
            .arg(tool->name()).arg(QString::fromStdString(e.what())));
        result.ok = false;
        result.data["error"] = QString("运行错误: %1").arg(QString::fromStdString(e.what()));
        success = false;
    }

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

    // P1-B3 修复：直接委托给 BranchNode::evaluate，支持 8 种操作符
    // (==, !=, >, >=, <, <=, ok, ng)，之前 executor 只实现 3 种导致 5 种永远 false
    const ToolResult& sourceResult = m_results[branch->sourceToolId];
    return branch->evaluate(sourceResult);
}

ToolResult ToolChainExecutor::getResult(const QString& toolId) const {
    QMutexLocker locker(&m_mutex);
    return m_results.value(toolId);
}