#include "Scheme.h"
#include "VisionTool.h"
#include "BranchNode.h"
#include "CameraConfig.h"
#include "TriggerConfig.h"
#include "OutputConfig.h"
#include "ModelBinding.h"
#include "Logger.h"
#include "Vision/ToolFactory.h"   // v2.7.0：子链/并行分支反序列化时需要 ToolFactory::createTool
#include <algorithm>

using namespace QDV;

Scheme::Scheme() 
    : m_id(QUuid::createUuid().toString()),
      m_cameraConfig(std::make_unique<CameraConfig>()),
      m_triggerConfig(std::make_unique<TriggerConfig>()),
      m_outputConfig(std::make_unique<OutputConfig>()),
      m_modelBinding(std::make_unique<ModelBinding>()) {
    QDateTime now = QDateTime::currentDateTimeUtc();
    m_created = now.toString(Qt::ISODate);
    m_modified = m_created;
}

Scheme::Scheme(const QString& name) : Scheme() {
    m_name = name;
}

Scheme::~Scheme() = default;

void Scheme::setName(const QString& name) {
    if (name.isEmpty() || name.length() > 255) {
        Logger::warn("Invalid scheme name, using default");
        m_name = "Untitled Scheme";
    } else {
        m_name = name;
    }
}

void Scheme::addTool(std::unique_ptr<QDV::VisionTool> tool) {
    if (!tool) {
        Logger::error("Attempted to add null tool");
        return;
    }
    m_toolChain.push_back(std::move(tool));
    m_modified = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
}

void Scheme::removeTool(const QString& toolId) {
    auto it = std::find_if(m_toolChain.begin(), m_toolChain.end(),
        [&toolId](const std::unique_ptr<QDV::VisionTool>& t) {
            return t->id() == toolId;
        });
    if (it != m_toolChain.end()) {
        m_toolChain.erase(it);
        m_modified = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    }
}

QDV::VisionTool* Scheme::getTool(const QString& toolId) const {
    for (const auto& tool : m_toolChain) {
        if (tool->id() == toolId) {
            return tool.get();
        }
    }
    return nullptr;
}

void Scheme::moveTool(int fromIndex, int toIndex) {
    size_t sz = m_toolChain.size();
    if (fromIndex >= 0 && static_cast<size_t>(fromIndex) < sz &&
        toIndex >= 0 && static_cast<size_t>(toIndex) < sz &&
        fromIndex != toIndex) {
        auto tool = std::move(m_toolChain[fromIndex]);
        m_toolChain.erase(m_toolChain.begin() + fromIndex);
        m_toolChain.insert(m_toolChain.begin() + toIndex, std::move(tool));
        m_modified = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    }
}

void Scheme::addBranch(std::unique_ptr<BranchNode> branch) {
    if (!branch || branch->id.isEmpty()) {
        Logger::error("Attempted to add invalid branch");
        return;
    }
    QString branchId = branch->id;
    m_branches[branchId] = std::move(branch);
    m_modified = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
}

QMap<QString, BranchNode*> Scheme::branches() const {
    QMap<QString, BranchNode*> result;
    for (const auto& pair : m_branches) {
        result[pair.first] = pair.second.get();
    }
    return result;
}

// ============================================================================
// v2.7.0 子链管理实现
// ============================================================================

void Scheme::addSubChain(const QString& loopToolId, std::vector<std::unique_ptr<QDV::VisionTool>> tools) {
    m_subChains[loopToolId] = std::move(tools);
}

void Scheme::removeSubChain(const QString& loopToolId) {
    m_subChains.erase(loopToolId);
}

QMap<QString, QList<QDV::VisionTool*>> Scheme::subChainPtrs() const {
    QMap<QString, QList<QDV::VisionTool*>> result;
    for (const auto& pair : m_subChains) {
        QList<QDV::VisionTool*> ptrs;
        for (const auto& tool : pair.second) {
            ptrs.append(tool.get());
        }
        result[pair.first] = ptrs;
    }
    return result;
}

QList<QDV::VisionTool*> Scheme::subChainPtrs(const QString& loopToolId) const {
    QList<QDV::VisionTool*> ptrs;
    auto it = m_subChains.find(loopToolId);
    if (it != m_subChains.end()) {
        for (const auto& tool : it->second) {
            ptrs.append(tool.get());
        }
    }
    return ptrs;
}

// ============================================================================
// v2.7.0 并行分支管理实现
// ============================================================================

void Scheme::addParallelBranch(const QString& branchId, std::vector<std::unique_ptr<QDV::VisionTool>> tools) {
    m_parallelBranches[branchId] = std::move(tools);
}

void Scheme::removeParallelBranch(const QString& branchId) {
    m_parallelBranches.erase(branchId);
}

QMap<QString, QList<QDV::VisionTool*>> Scheme::parallelBranchPtrs() const {
    QMap<QString, QList<QDV::VisionTool*>> result;
    for (const auto& pair : m_parallelBranches) {
        QList<QDV::VisionTool*> ptrs;
        for (const auto& tool : pair.second) {
            ptrs.append(tool.get());
        }
        result[pair.first] = ptrs;
    }
    return result;
}

QList<QDV::VisionTool*> Scheme::toolPtrs() const {
    QList<QDV::VisionTool*> result;
    for (const auto& tool : m_toolChain) {
        result.append(tool.get());
    }
    return result;
}

void Scheme::setCameraConfig(CameraConfig* config) {
    if (config) {
        m_cameraConfig.reset(config);
    }
}

void Scheme::setTriggerConfig(TriggerConfig* config) {
    if (config) {
        m_triggerConfig.reset(config);
    }
}

void Scheme::setOutputConfig(OutputConfig* config) {
    if (config) {
        m_outputConfig.reset(config);
    }
}

void Scheme::setModelBinding(ModelBinding* binding) {
    if (binding) {
        m_modelBinding.reset(binding);
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
    for (const auto& tool : m_toolChain) {
        toolChainArray.append(tool->serialize());
    }
    obj["toolChain"] = toolChainArray;
    
    QJsonArray branchesArray;
    for (const auto& pair : m_branches) {
        branchesArray.append(pair.second->serialize());
    }
    obj["branches"] = branchesArray;

    // v2.7.0：子链序列化
    QJsonObject subChainsObj;
    for (const auto& pair : m_subChains) {
        QJsonArray arr;
        for (const auto& tool : pair.second) {
            if (tool) arr.append(tool->serialize());
        }
        subChainsObj[pair.first] = arr;
    }
    obj["subChains"] = subChainsObj;

    // v2.7.0：并行分支序列化
    QJsonObject parallelObj;
    for (const auto& pair : m_parallelBranches) {
        QJsonArray arr;
        for (const auto& tool : pair.second) {
            if (tool) arr.append(tool->serialize());
        }
        parallelObj[pair.first] = arr;
    }
    obj["parallelBranches"] = parallelObj;

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

    // v2.7.0：子链反序列化
    m_subChains.clear();
    if (data.contains("subChains") && data["subChains"].isObject()) {
        const QJsonObject subObj = data["subChains"].toObject();
        for (auto it = subObj.begin(); it != subObj.end(); ++it) {
            const QString loopId = it.key();
            std::vector<std::unique_ptr<QDV::VisionTool>> tools;
            const QJsonArray arr = it.value().toArray();
            for (const QJsonValue& v : arr) {
                if (!v.isObject()) continue;
                const QJsonObject toolObj = v.toObject();
                const QString type = toolObj.value("type").toString();
                if (type.isEmpty()) continue;
                QDV::VisionTool* tool = ::ToolFactory::instance()->createTool(type);
                if (tool) {
                    tool->deserialize(toolObj);
                    tools.emplace_back(tool);
                }
            }
            m_subChains[loopId] = std::move(tools);
        }
    }

    // v2.7.0：并行分支反序列化
    m_parallelBranches.clear();
    if (data.contains("parallelBranches") && data["parallelBranches"].isObject()) {
        const QJsonObject parObj = data["parallelBranches"].toObject();
        for (auto it = parObj.begin(); it != parObj.end(); ++it) {
            const QString branchId = it.key();
            std::vector<std::unique_ptr<QDV::VisionTool>> tools;
            const QJsonArray arr = it.value().toArray();
            for (const QJsonValue& v : arr) {
                if (!v.isObject()) continue;
                const QJsonObject toolObj = v.toObject();
                const QString type = toolObj.value("type").toString();
                if (type.isEmpty()) continue;
                QDV::VisionTool* tool = ::ToolFactory::instance()->createTool(type);
                if (tool) {
                    tool->deserialize(toolObj);
                    tools.emplace_back(tool);
                }
            }
            m_parallelBranches[branchId] = std::move(tools);
        }
    }

    return true;
}

QDV::Result<void> Scheme::tryDeserialize(const QJsonObject& data) {
    if (!validateJsonSchema(data)) {
        return QDV::Result<void>::err("Invalid JSON schema for scheme");
    }
    if (!validateRequiredFields(data)) {
        return QDV::Result<void>::err("Missing required fields");
    }
    if (!validateFieldTypes(data)) {
        return QDV::Result<void>::err("Invalid field types");
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

    return QDV::Result<void>::ok();
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