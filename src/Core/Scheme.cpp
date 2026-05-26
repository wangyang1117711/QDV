#include "Scheme.h"
#include "VisionTool.h"
#include "BranchNode.h"
#include "CameraConfig.h"
#include "TriggerConfig.h"
#include "OutputConfig.h"
#include "ModelBinding.h"
#include "Logger.h"

using namespace QDV;

Scheme::Scheme() 
    : m_id(QUuid::createUuid().toString()),
      m_cameraConfig(new CameraConfig),
      m_triggerConfig(new TriggerConfig),
      m_outputConfig(new OutputConfig),
      m_modelBinding(new ModelBinding) {
    QDateTime now = QDateTime::currentDateTimeUtc();
    m_created = now.toString(Qt::ISODate);
    m_modified = m_created;
}

Scheme::Scheme(const QString& name) : Scheme() {
    m_name = name;
}

void Scheme::setName(const QString& name) {
    if (name.isEmpty() || name.length() > 255) {
        Logger::warn("Invalid scheme name, using default");
        m_name = "Untitled Scheme";
    } else {
        m_name = name;
    }
}

void Scheme::addTool(QDV::VisionTool* tool) {
    if (!tool) {
        Logger::error("Attempted to add null tool");
        return;
    }
    m_toolChain.append(tool);
    m_modified = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
}

void Scheme::removeTool(QDV::VisionTool* tool) {
    m_toolChain.removeOne(tool);
    m_modified = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
}

void Scheme::removeTool(const QString& toolId) {
    for (auto it = m_toolChain.begin(); it != m_toolChain.end(); ++it) {
        if ((*it)->id() == toolId) {
            m_toolChain.erase(it);
            m_modified = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
            break;
        }
    }
}

QDV::VisionTool* Scheme::getTool(const QString& toolId) const {
    for (QDV::VisionTool* tool : m_toolChain) {
        if (tool->id() == toolId) {
            return tool;
        }
    }
    return nullptr;
}

void Scheme::moveTool(int fromIndex, int toIndex) {
    if (fromIndex >= 0 && fromIndex < m_toolChain.size() &&
        toIndex >= 0 && toIndex < m_toolChain.size() &&
        fromIndex != toIndex) {
        m_toolChain.move(fromIndex, toIndex);
        m_modified = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    }
}

void Scheme::addBranch(BranchNode* branch) {
    if (!branch || branch->id.isEmpty()) {
        Logger::error("Attempted to add invalid branch");
        return;
    }
    m_branches[branch->id] = branch;
    m_modified = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
}

void Scheme::setCameraConfig(CameraConfig* config) {
    if (config) {
        delete m_cameraConfig;
        m_cameraConfig = config;
    }
}

void Scheme::setTriggerConfig(TriggerConfig* config) {
    if (config) {
        delete m_triggerConfig;
        m_triggerConfig = config;
    }
}

void Scheme::setOutputConfig(OutputConfig* config) {
    if (config) {
        delete m_outputConfig;
        m_outputConfig = config;
    }
}

void Scheme::setModelBinding(ModelBinding* binding) {
    if (binding) {
        delete m_modelBinding;
        m_modelBinding = binding;
    }
}

bool Scheme::validateRequiredFields(const QJsonObject& data) {
    QStringList required = {"id", "name", "version", "created", "modified"};
    for (const QString& field : required) {
        if (!data.contains(field) || data[field].isNull()) {
            Logger::error("Missing required field: " + field);
            return false;
        }
    }
    return true;
}

bool Scheme::validateFieldTypes(const QJsonObject& data) {
    if (!data["id"].isString()) {
        Logger::error("Field 'id' must be string");
        return false;
    }
    if (!data["name"].isString()) {
        Logger::error("Field 'name' must be string");
        return false;
    }
    if (!data["version"].isString()) {
        Logger::error("Field 'version' must be string");
        return false;
    }
    if (!data["created"].isString()) {
        Logger::error("Field 'created' must be string");
        return false;
    }
    if (!data["modified"].isString()) {
        Logger::error("Field 'modified' must be string");
        return false;
    }
    
    if (data.contains("camera") && !data["camera"].isObject()) {
        Logger::error("Field 'camera' must be object");
        return false;
    }
    if (data.contains("trigger") && !data["trigger"].isObject()) {
        Logger::error("Field 'trigger' must be object");
        return false;
    }
    if (data.contains("output") && !data["output"].isObject()) {
        Logger::error("Field 'output' must be object");
        return false;
    }
    if (data.contains("toolChain") && !data["toolChain"].isArray()) {
        Logger::error("Field 'toolChain' must be array");
        return false;
    }
    
    return true;
}

bool Scheme::validateJsonSchema(const QJsonObject& data) {
    if (data.isEmpty()) {
        Logger::error("Empty JSON object");
        return false;
    }
    
    Scheme tempScheme;
    return tempScheme.validateRequiredFields(data) && tempScheme.validateFieldTypes(data);
}

QJsonObject Scheme::serialize() const {
    QJsonObject obj;
    obj["version"] = "1.0";
    obj["id"] = m_id;
    obj["name"] = m_name;
    obj["created"] = m_created;
    obj["modified"] = m_modified;
    
    obj["camera"] = m_cameraConfig->serialize();
    obj["trigger"] = m_triggerConfig->serialize();
    obj["output"] = m_outputConfig->serialize();
    obj["model"] = m_modelBinding->serialize();
    
    QJsonArray toolChainArray;
    for (QDV::VisionTool* tool : m_toolChain) {
        toolChainArray.append(tool->serialize());
    }
    obj["toolChain"] = toolChainArray;
    
    QJsonArray branchesArray;
    for (BranchNode* branch : m_branches.values()) {
        branchesArray.append(branch->serialize());
    }
    obj["branches"] = branchesArray;
    
    return obj;
}

bool Scheme::deserialize(const QJsonObject& data) {
    if (!validateJsonSchema(data)) {
        Logger::error("Invalid JSON schema for scheme");
        return false;
    }
    
    if (!validateRequiredFields(data) || !validateFieldTypes(data)) {
        return false;
    }
    
    m_id = data["id"].toString();
    m_name = data["name"].toString();
    m_created = data["created"].toString();
    m_modified = data["modified"].toString();
    
    if (data.contains("camera") && !data["camera"].isNull()) {
        m_cameraConfig->deserialize(data["camera"].toObject());
    }
    if (data.contains("trigger") && !data["trigger"].isNull()) {
        m_triggerConfig->deserialize(data["trigger"].toObject());
    }
    if (data.contains("output") && !data["output"].isNull()) {
        m_outputConfig->deserialize(data["output"].toObject());
    }
    if (data.contains("model") && !data["model"].isNull()) {
        m_modelBinding->deserialize(data["model"].toObject());
    }
    
    return true;
}

void Scheme::setFilePath(const QString& path) {
    if (path.isEmpty()) {
        Logger::warn("Empty file path provided");
        return;
    }
    m_filePath = path;
}

QValidator::State SchemeValidator::validate(QString& input, int& pos) const {
    Q_UNUSED(pos)
    
    if (input.isEmpty()) {
        return QValidator::Intermediate;
    }
    
    if (input.length() > 255) {
        return QValidator::Invalid;
    }
    
    return QValidator::Acceptable;
}