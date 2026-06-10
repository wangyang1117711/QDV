#ifndef SCHEME_H
#define SCHEME_H

#include <QString>
#include <QList>
#include <QMap>
#include <QJsonObject>
#include <memory>
#include <vector>
#include <QUuid>
#include <QValidator>
#include "Core/Result.h"

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
    ~Scheme();
    
    QString id() const { return m_id; }
    QString name() const { return m_name; }
    void setName(const QString& name);
    
    QDV::VisionTool* getTool(const QString& toolId) const;
    void moveTool(int fromIndex, int toIndex);
    
    QMap<QString, BranchNode*> branches() const;
    void addBranch(std::unique_ptr<BranchNode> branch);
    
    CameraConfig* cameraConfig() const { return m_cameraConfig.get(); }
    void setCameraConfig(CameraConfig* config);
    
    TriggerConfig* triggerConfig() const { return m_triggerConfig.get(); }
    void setTriggerConfig(TriggerConfig* config);
    
    OutputConfig* outputConfig() const { return m_outputConfig.get(); }
    void setOutputConfig(OutputConfig* config);
    
    ModelBinding* modelBinding() const { return m_modelBinding.get(); }
    void setModelBinding(ModelBinding* binding);
    
    QJsonObject serialize() const;
    bool deserialize(const QJsonObject& data);
    QDV::Result<void> tryDeserialize(const QJsonObject& data);
    
    static bool validateJsonSchema(const QJsonObject& data);
    
    QString filePath() const { return m_filePath; }
    void setFilePath(const QString& path);
    
    std::vector<std::unique_ptr<QDV::VisionTool>>& toolChain() { return m_toolChain; }
    const std::vector<std::unique_ptr<QDV::VisionTool>>& toolChain() const { return m_toolChain; }
    QList<QDV::VisionTool*> toolPtrs() const;
    void addTool(std::unique_ptr<QDV::VisionTool> tool);
    void removeTool(const QString& toolId);
    size_t toolCount() const { return m_toolChain.size(); }
    
private:
    QString m_id;
    QString m_name;
    QString m_filePath;
    QString m_created;
    QString m_modified;
    std::vector<std::unique_ptr<QDV::VisionTool>> m_toolChain;
    std::map<QString, std::unique_ptr<BranchNode>> m_branches;
    std::unique_ptr<CameraConfig> m_cameraConfig;
    std::unique_ptr<TriggerConfig> m_triggerConfig;
    std::unique_ptr<OutputConfig> m_outputConfig;
    std::unique_ptr<ModelBinding> m_modelBinding;
    
    bool validateRequiredFields(const QJsonObject& data);
    bool validateFieldTypes(const QJsonObject& data);
};

#endif // SCHEME_H