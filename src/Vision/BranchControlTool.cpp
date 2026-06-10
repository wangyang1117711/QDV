#include "BranchControlTool.h"
#include "BranchNode.h"

using namespace QDV;

bool BranchControlTool::configure(const QJsonObject& params) {
    if (params.isEmpty()) {
        return false;
    }

    if (m_branchNode) {
        delete m_branchNode;
    }

    m_branchNode = new BranchNode();
    m_branchNode->deserialize(params);
    return m_branchNode->isValid();
}

bool BranchControlTool::execute(const cv::Mat& input, ToolResult& result) {
    m_inputImage = input.clone();
    result.overlayImage = input.clone();
    result.ok = true;
    result.elapsedMs = 0;
    return true;
}

QJsonObject BranchControlTool::serialize() const {
    QJsonObject obj;
    obj["id"] = m_id;
    obj["type"] = type();
    if (m_branchNode) {
        QJsonObject branchObj = m_branchNode->serialize();
        for (auto it = branchObj.begin(); it != branchObj.end(); ++it) {
            obj[it.key()] = it.value();
        }
    }
    return obj;
}

bool BranchControlTool::deserialize(const QJsonObject& data) {
    m_id = data["id"].toString();
    if (m_branchNode) {
        delete m_branchNode;
    }
    m_branchNode = new BranchNode();
    m_branchNode->deserialize(data);
    return true;
}

void BranchControlTool::setBranchNode(BranchNode* node) {
    if (m_branchNode) {
        delete m_branchNode;
    }
    m_branchNode = node;
}