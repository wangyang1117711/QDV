#include "Core/ImageVariableManager.h"
#include "Core/Logger.h"

namespace QDV {

ImageVariableManager::ImageVariableManager(QObject* parent) : QObject(parent) {}

ImageVariableManager::~ImageVariableManager() = default;

void ImageVariableManager::updateImageVariable(const QString& nodeId, const QString& toolName,
                                                const QString& outputImagePath,
                                                int width, int height, int channels) {
    ImageVariable v;
    v.nodeId = nodeId;
    v.toolName = toolName;
    v.outputImagePath = outputImagePath;
    v.timestamp = QDateTime::currentMSecsSinceEpoch();
    v.width = width;
    v.height = height;
    v.channels = channels;
    {
        QMutexLocker locker(&m_mutex);
        m_imageVariables.insert(nodeId, v);
    }
    emit imageVariableUpdated(nodeId, outputImagePath);
    emit imageVariablesChanged();
    Logger::info(QString("ImageVariableManager: 更新 %1 (%2) %3x%4")
                 .arg(toolName).arg(nodeId).arg(width).arg(height));
}

QVariantMap ImageVariableManager::imageVariable(const QString& nodeId) const {
    QMutexLocker locker(&m_mutex);
    QVariantMap result;
    auto it = m_imageVariables.constFind(nodeId);
    if (it == m_imageVariables.constEnd()) return result;
    result["nodeId"] = it->nodeId;
    result["toolName"] = it->toolName;
    result["outputImagePath"] = it->outputImagePath;
    result["timestamp"] = it->timestamp;
    result["width"] = it->width;
    result["height"] = it->height;
    result["channels"] = it->channels;
    return result;
}

QVariantList ImageVariableManager::imageVariables() const {
    QMutexLocker locker(&m_mutex);
    QVariantList list;
    for (auto it = m_imageVariables.constBegin(); it != m_imageVariables.constEnd(); ++it) {
        QVariantMap vm;
        vm["nodeId"] = it->nodeId;
        vm["toolName"] = it->toolName;
        vm["outputImagePath"] = it->outputImagePath;
        vm["timestamp"] = it->timestamp;
        vm["width"] = it->width;
        vm["height"] = it->height;
        vm["channels"] = it->channels;
        list.append(vm);
    }
    return list;
}

QString ImageVariableManager::imagePath(const QString& nodeId) const {
    QMutexLocker locker(&m_mutex);
    auto it = m_imageVariables.constFind(nodeId);
    if (it == m_imageVariables.constEnd()) return QString();
    return it->outputImagePath;
}

bool ImageVariableManager::removeImageVariable(const QString& nodeId) {
    bool removed = false;
    {
        QMutexLocker locker(&m_mutex);
        removed = m_imageVariables.remove(nodeId) > 0;
    }
    if (removed) {
        emit imageVariableRemoved(nodeId);
        emit imageVariablesChanged();
    }
    return removed;
}

void ImageVariableManager::clear() {
    bool needEmit = false;
    {
        QMutexLocker locker(&m_mutex);
        needEmit = !m_imageVariables.isEmpty();
        if (needEmit) m_imageVariables.clear();
    }
    if (needEmit) emit imageVariablesChanged();
}

int ImageVariableManager::count() const {
    QMutexLocker locker(&m_mutex);
    return m_imageVariables.size();
}

bool ImageVariableManager::exists(const QString& nodeId) const {
    QMutexLocker locker(&m_mutex);
    return m_imageVariables.contains(nodeId);
}

} // namespace QDV
