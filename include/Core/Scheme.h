#ifndef SCHEME_H
#define SCHEME_H

#include <QString>
#include <QList>
#include <QMap>
#include <QJsonObject>
#include <QUuid>
#include <QValidator>

namespace QDV {
class VisionTool;
}

class BranchNode;
class CameraConfig;
class TriggerConfig;
class OutputConfig;
class ModelBinding;

class SchemeValidator : public QValidator {
public:
    QValidator::State validate(QString& input, int& pos) const override;
};

class Scheme {
public:
    Scheme();
    explicit Scheme(const QString& name);
    
    QString id() const { return m_id; }
    QString name() const { return m_name; }
    void setName(const QString& name);
    
    QList<QDV::VisionTool*> toolChain() const { return m_toolChain; }
    void addTool(QDV::VisionTool* tool);
    void removeTool(QDV::VisionTool* tool);
    void removeTool(const QString& toolId);
    QDV::VisionTool* getTool(const QString& toolId) const;
    void moveTool(int fromIndex, int toIndex);
    
    QMap<QString, BranchNode*> branches() const { return m_branches; }
    void addBranch(BranchNode* branch);
    
    CameraConfig* cameraConfig() const { return m_cameraConfig; }
    void setCameraConfig(CameraConfig* config);
    
    TriggerConfig* triggerConfig() const { return m_triggerConfig; }
    void setTriggerConfig(TriggerConfig* config);
    
    OutputConfig* outputConfig() const { return m_outputConfig; }
    void setOutputConfig(OutputConfig* config);
    
    ModelBinding* modelBinding() const { return m_modelBinding; }
    void setModelBinding(ModelBinding* binding);
    
    QJsonObject serialize() const;
    bool deserialize(const QJsonObject& data);
    
    static bool validateJsonSchema(const QJsonObject& data);
    
    QString filePath() const { return m_filePath; }
    void setFilePath(const QString& path);
    
private:
    QString m_id;
    QString m_name;
    QString m_filePath;
    QString m_created;
    QString m_modified;
    QList<QDV::VisionTool*> m_toolChain;
    QMap<QString, BranchNode*> m_branches;
    CameraConfig* m_cameraConfig;
    TriggerConfig* m_triggerConfig;
    OutputConfig* m_outputConfig;
    ModelBinding* m_modelBinding;
    
    bool validateRequiredFields(const QJsonObject& data);
    bool validateFieldTypes(const QJsonObject& data);
};

#endif // SCHEME_H