// AgentOperatorRouter.cpp - Agent协作框架算子路由组件实现
// 路由策略：静态硬编码映射表（参照 Halcon 算子分类标准）

#include "AgentOperatorRouter.h"

namespace QDV {

AgentOperatorRouter::AgentOperatorRouter() {
    initRoutingTable();
}

void AgentOperatorRouter::initRoutingTable() {
    // 12 类任务映射（参照 Halcon 算子参考手册分类 + .agent_mesh/roles/*.md 角色职责）
    // priority: 1=首选, 2=备选

    // 1. 图像采集（Developer）
    m_routingTable["图像采集"] = {
        {"ReadImage",        "Developer", 1},
        {"GrabImage",        "Developer", 2},
        {"OpenFramegrabber", "Developer", 3},
    };

    // 2. 滤波预处理（Developer）
    m_routingTable["滤波预处理"] = {
        {"GaussFilter",  "Developer", 1},
        {"MeanImage",    "Developer", 2},
        {"MedianImage",  "Developer", 3},
        {"FftGeneric",   "Developer", 4},
    };

    // 3. 形态学（Developer）
    m_routingTable["形态学"] = {
        {"Erosion",  "Developer", 1},
        {"Dilation", "Developer", 2},
        {"Opening",  "Developer", 3},
        {"Closing",  "Developer", 4},
        {"TopHat",   "Developer", 5},
        {"BottomHat", "Developer", 6},
    };

    // 4. 图像分割（Developer）
    m_routingTable["图像分割"] = {
        {"Threshold",    "Developer", 1},
        {"DynThreshold", "Developer", 2},
        {"Watershed",    "Developer", 3},
        {"RegionGrowing", "Developer", 4},
    };

    // 5. Blob 分析（Reviewer）
    m_routingTable["Blob分析"] = {
        {"BlobDetect",    "Reviewer", 1},
        {"Connection",    "Reviewer", 2},
        {"ContourAnalyze", "Reviewer", 3},
        {"SelectShape",   "Reviewer", 4},
    };

    // 6. 特征提取（Developer）
    m_routingTable["特征提取"] = {
        {"PointsHarris",     "Developer", 1},
        {"EdgesSubPix",      "Developer", 2},
        {"LineCircleDetect", "Developer", 3},
    };

    // 7. 匹配定位（Developer）
    m_routingTable["匹配定位"] = {
        {"TemplateMatch",  "Developer", 1},
        {"FindNccModel",   "Developer", 2},
        {"FindShapeModel", "Developer", 3},
    };

    // 8. 几何测量（Reviewer）
    m_routingTable["几何测量"] = {
        {"GeometryMeasure", "Reviewer", 1},
        {"DistancePp",      "Reviewer", 2},
        {"AngleLl",         "Reviewer", 3},
    };

    // 9. 3D 视觉（Developer）
    m_routingTable["3D视觉"] = {
        {"Reconstruct3D",      "Developer", 1},
        {"BinocularDisparity", "Developer", 2},
        {"SurfaceMatching",    "Developer", 3},
    };

    // 10. 深度学习（Reviewer）
    m_routingTable["深度学习"] = {
        {"AiClassify",      "Reviewer", 1},
        {"SegmentDl",       "Reviewer", 2},
        {"DetectObjectsDl", "Reviewer", 3},
    };

    // 11. 评审验证（Gatekeeper）- Agent 角色 mock
    m_routingTable["评审验证"] = {
        {"MockClassify",  "Gatekeeper", 1},
        {"MockValidate",  "Gatekeeper", 2},
    };

    // 12. 审计检查（SpecGuardian）- Agent 角色 mock
    m_routingTable["审计检查"] = {
        {"MockAudit",      "SpecGuardian", 1},
        {"MockPlan",       "SpecGuardian", 2},
        {"MockCoordinate", "SpecGuardian", 3},
    };
}

QVector<OperatorAgentMapping> AgentOperatorRouter::route(const QString& taskType) const {
    // 空任务或未知任务返回空列表（不崩溃）
    if (taskType.isEmpty()) {
        return {};
    }
    auto it = m_routingTable.find(taskType);
    if (it == m_routingTable.end()) {
        return {};
    }
    return it.value();
}

QStringList AgentOperatorRouter::supportedTaskTypes() const {
    return m_routingTable.keys();
}

bool AgentOperatorRouter::isSupported(const QString& taskType) const {
    if (taskType.isEmpty()) return false;
    return m_routingTable.contains(taskType);
}

OperatorAgentMapping AgentOperatorRouter::firstChoice(const QString& taskType) const {
    auto mappings = route(taskType);
    for (const auto& m : mappings) {
        if (m.priority == 1) {
            return m;
        }
    }
    // 不存在返回空
    return OperatorAgentMapping{};
}

QJsonArray AgentOperatorRouter::routeAsJson(const QString& taskType) const {
    QJsonArray arr;
    for (const auto& m : route(taskType)) {
        arr.append(m.toJson());
    }
    return arr;
}

} // namespace QDV
