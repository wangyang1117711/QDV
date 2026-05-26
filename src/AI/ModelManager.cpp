#include "AI/ModelManager.h"
#include "AI/InferenceEngine.h"
#include <QDir>
#include <QFileInfo>

ModelManager* ModelManager::s_instance = nullptr;

ModelManager* ModelManager::instance() {
    if (!s_instance) {
        s_instance = new ModelManager();
    }
    return s_instance;
}

ModelManager::ModelManager(QObject* parent) : QObject(parent) {
}

bool ModelManager::loadModel(const QString& modelPath, const QString& modelId) {
    if (m_engines.contains(modelId)) {
        return false;
    }

    InferenceEngine* engine = new InferenceEngine(this);
    if (!engine->loadModel(modelPath)) {
        delete engine;
        return false;
    }

    m_engines[modelId] = engine;
    emit modelLoaded(modelId);
    return true;
}

bool ModelManager::unloadModel(const QString& modelId) {
    if (!m_engines.contains(modelId)) {
        return false;
    }

    InferenceEngine* engine = m_engines.take(modelId);
    engine->unloadModel();
    delete engine;

    emit modelUnloaded(modelId);
    return true;
}

InferenceEngine* ModelManager::getEngine(const QString& modelId) {
    return m_engines.value(modelId, nullptr);
}

QStringList ModelManager::listModels(const QString& directory) {
    QStringList models;
    QDir dir(directory);
    if (!dir.exists()) return models;

    QStringList filters = {"*.onnx", "*.pth", "*.pt", "*.bin"};
    QFileInfoList files = dir.entryInfoList(filters, QDir::Files);
    for (const auto& file : files) {
        models.append(file.absoluteFilePath());
    }
    return models;
}