#include "UI/PreviewManager.h"
#include "UI/EditViewBridge.h"
#include "Core/ImageVariableManager.h"
#include "Core/VariableManager.h"
#include "Core/Logger.h"

#include <QtConcurrent/QtConcurrent>

// EditViewBridge 在全局命名空间，已在 PreviewManager.h 前置声明
// 这里 include 完整定义供 m_bridge->runSingleOperator() 等调用使用

namespace QDV {

PreviewManager::PreviewManager(QObject* parent)
    : QObject(parent)
    , m_bridge(nullptr)
    , m_ivm(nullptr)
    , m_vm(nullptr)
    , m_autoPreviewEnabled(true)
    , m_isRunning(false)
{
    // 300ms 防抖定时器
    m_debounceTimer.setSingleShot(true);
    m_debounceTimer.setInterval(300);
    connect(&m_debounceTimer, &QTimer::timeout, this, &PreviewManager::doPreview);

    // v2.6.1：异步执行完成回调（子线程结果 → 主线程）
    connect(&m_watcher, &QFutureWatcher<QVariantMap>::finished,
            this, &PreviewManager::onPreviewFinished);
}

PreviewManager::~PreviewManager() = default;

void PreviewManager::setBridge(::EditViewBridge* bridge) {
    m_bridge = bridge;
}

void PreviewManager::setImageVariableManager(ImageVariableManager* ivm) {
    m_ivm = ivm;
}

void PreviewManager::setVariableManager(VariableManager* vm) {
    m_vm = vm;
}

void PreviewManager::setAutoPreviewEnabled(bool enabled) {
    if (m_autoPreviewEnabled == enabled) return;
    m_autoPreviewEnabled = enabled;
    emit autoPreviewEnabledChanged();
    if (!enabled) {
        m_debounceTimer.stop();
    }
}

void PreviewManager::setPreviewNodeId(const QString& nodeId) {
    if (m_previewNodeId == nodeId) return;
    m_previewNodeId = nodeId;
    emit previewNodeIdChanged(nodeId);
    // 切换预览节点 → 立即执行（跳过防抖）
    if (m_autoPreviewEnabled && !nodeId.isEmpty()) {
        previewNow();
    }
}

void PreviewManager::requestPreview() {
    if (!m_autoPreviewEnabled) return;
    // 防抖：300ms 内多次变更只执行最后一次
    m_pendingNodeId = m_previewNodeId;
    m_debounceTimer.start();
}

void PreviewManager::previewNow() {
    m_debounceTimer.stop();
    m_pendingNodeId = m_previewNodeId;
    doPreview();
}

void PreviewManager::onNodeParamsChanged(const QString& nodeId) {
    // 参数变更的节点如果是当前预览节点或其上游 → 请求预览
    if (nodeId == m_previewNodeId) {
        requestPreview();
    }
    // 上游节点参数变更也会影响下游，简化处理：只要选中节点非空就请求
    if (!m_previewNodeId.isEmpty()) {
        requestPreview();
    }
}

void PreviewManager::onVariableChanged(const QString& varName) {
    Q_UNUSED(varName);
    // 变量变更可能影响任何引用该变量的算子，简化处理：请求预览
    if (!m_previewNodeId.isEmpty()) {
        requestPreview();
    }
}

void PreviewManager::onNodeSelected(const QString& nodeId) {
    // 自动跟随选中节点
    setPreviewNodeId(nodeId);
}

void PreviewManager::onNodeDeleted(const QString& nodeId) {
    // 清理对应图像变量
    if (m_ivm) {
        m_ivm->removeImageVariable(nodeId);
    }
    // 如果删除的是当前预览节点，清空预览
    if (nodeId == m_previewNodeId) {
        setPreviewNodeId(QString());
    }
}

void PreviewManager::onConnectionsChanged() {
    // 连线变更可能影响上游链，请求预览
    if (!m_previewNodeId.isEmpty()) {
        requestPreview();
    }
}

void PreviewManager::doPreview() {
    if (!m_bridge || m_pendingNodeId.isEmpty()) return;

    // 重入保护：如果上一次异步执行尚未完成，则推迟本次请求
    // （不直接 restart timer，避免形成"执行→信号→请求→执行"的持续循环导致主线程卡顿）
    if (m_isRunning) {
        return;
    }
    m_isRunning = true;
    emit isRunningChanged();

    // v2.6.1 修复卡死 bug：
    // 原实现在主线程同步调用 runSingleOperator，FFT 等耗时算子会阻塞 UI 主线程，
    // 导致界面完全无响应（卡死）。现改为子线程异步执行，结果通过 QFutureWatcher 回主线程。
    //
    // 注意：runSingleOperator 内部会调用 ImageVariableManager::updateImageVariable，
    // ImageVariableManager 已加 QMutex 保护，子线程更新是安全的；
    // 其发出的信号通过 AutoConnection 在跨线程时自动使用 QueuedConnection 回主线程。
    m_runningNodeId = m_pendingNodeId;
    const QString nodeIdToRun = m_pendingNodeId;

    m_watcher.setFuture(QtConcurrent::run([this, nodeIdToRun]() -> QVariantMap {
        // 在子线程执行算子链（不阻塞 UI 主线程）
        return m_bridge->runSingleOperator(nodeIdToRun, QString());
    }));
}

void PreviewManager::onPreviewFinished() {
    const QVariantMap result = m_watcher.result();
    const QString nodeId = m_runningNodeId;

    const bool success = result.value("success").toBool();
    const bool skipped = result.value("skipped").toBool();
    const QString outputPath = result.value("outputImagePath").toString();

    // v5.3.2：跳过的预览（如非图像算子无输入图像）不作为错误处理
    if (skipped) {
        m_runningNodeId.clear();
        m_isRunning = false;
        emit isRunningChanged();
        // 不触发 previewFailed，也不记录 warn 日志
        return;
    }

    // v2.6.1 修复 bug：
    // 原实现在此处又调用了一次 m_ivm->updateImageVariable(nodeId, toolName, outputPath)，
    // 但用的是 3 参数版本（width/height/channels 默认为 0），导致有效图像被 0x0 覆盖。
    // 而 runSingleOperator 内部已经用 6 参数版本正确更新了 ImageVariableManager，
    // 此处重复更新不仅浪费，还会清空图像尺寸信息，触发 QML 端反复刷新。
    // 修复：删除此处的重复 updateImageVariable 调用。

    if (success) {
        emit previewCompleted(nodeId, true, outputPath);
    } else {
        const QString error = result.value("error").toString();
        // v5.3.2：日志节流——避免重复的"未指定输入图像路径"刷屏
        if (error != QStringLiteral("未指定输入图像路径") &&
            error != QStringLiteral("no_input_needed")) {
            Logger::warn("PreviewManager: 预览失败 " + nodeId + " - " + error);
        }
        emit previewFailed(nodeId, error);
        emit previewCompleted(nodeId, false, QString());
    }

    m_runningNodeId.clear();
    m_isRunning = false;
    emit isRunningChanged();

    // 如果执行期间有新的预览请求被推迟（因 m_isRunning=true 而 return），
    // 这里重新触发防抖，确保最新请求被执行。
    // 但仅在 pending 与 running 不同时才触发，避免重复执行相同节点。
    if (m_autoPreviewEnabled && !m_pendingNodeId.isEmpty() && m_pendingNodeId != nodeId) {
        requestPreview();
    }
}

} // namespace QDV
