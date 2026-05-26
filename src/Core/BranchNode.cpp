#include "BranchNode.h"
#include "VisionTool.h"

bool BranchNode::evaluate(const ToolResult& result) const {
    if (!isValid()) {
        return false;
    }

    if (conditionOp == "==") {
        if (conditionValue.typeId() == QMetaType::Bool) {
            return result.ok == conditionValue.toBool();
        }
        if (conditionValue.typeId() == QMetaType::Double) {
            return qAbs(result.score - conditionValue.toDouble()) < 0.0001;
        }
        if (conditionValue.typeId() == QMetaType::Int) {
            if (result.data.contains("count")) {
                return result.data["count"].toInt() == conditionValue.toInt();
            }
            return static_cast<int>(result.score) == conditionValue.toInt();
        }
        return result.data.contains("value") &&
               result.data["value"].toVariant() == conditionValue;
    }

    if (conditionOp == "!=") {
        if (conditionValue.typeId() == QMetaType::Bool) {
            return result.ok != conditionValue.toBool();
        }
        if (conditionValue.typeId() == QMetaType::Double) {
            return qAbs(result.score - conditionValue.toDouble()) >= 0.0001;
        }
        return true;
    }

    if (conditionOp == ">") {
        double th = conditionValue.toDouble();
        if (result.data.contains("count")) {
            return result.data["count"].toDouble() > th;
        }
        return result.score > th;
    }

    if (conditionOp == ">=") {
        double th = conditionValue.toDouble();
        if (result.data.contains("count")) {
            return result.data["count"].toDouble() >= th;
        }
        return result.score >= th;
    }

    if (conditionOp == "<") {
        double th = conditionValue.toDouble();
        if (result.data.contains("count")) {
            return result.data["count"].toDouble() < th;
        }
        return result.score < th;
    }

    if (conditionOp == "<=") {
        double th = conditionValue.toDouble();
        if (result.data.contains("count")) {
            return result.data["count"].toDouble() <= th;
        }
        return result.score <= th;
    }

    if (conditionOp == "ok") {
        return result.ok;
    }

    if (conditionOp == "ng") {
        return !result.ok;
    }

    return false;
}

bool BranchNode::isValid() const {
    return !id.isEmpty() &&
           !sourceToolId.isEmpty() &&
           !conditionOp.isEmpty() &&
           (!trueBranchToolIds.isEmpty() || !falseBranchToolIds.isEmpty());
}