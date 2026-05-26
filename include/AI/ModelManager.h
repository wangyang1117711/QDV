#ifndef MODELMANAGER_H
#define MODELMANAGER_H

#include <QObject>
#include <QString>
#include <QMap>

class InferenceEngine;

class ModelManager : public QObject {
    Q_OBJECT
    
public:
    static ModelManager* instance();
    
    bool loadModel(const QString& modelPath, const QString& modelId);
    bool unloadModel(const QString& modelId);
    
    InferenceEngine* getEngine(const QString& modelId);
    
    QStringList listModels(const QString& directory);
    
signals:
    void modelLoaded(const QString& modelId);
    void modelUnloaded(const QString& modelId);
    
private:
    ModelManager(QObject* parent = nullptr);
    
    QMap<QString, InferenceEngine*> m_engines;
    static ModelManager* s_instance;
};

#endif // MODELMANAGER_H