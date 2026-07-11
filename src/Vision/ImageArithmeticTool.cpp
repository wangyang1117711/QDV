#include "ImageArithmeticTool.h"
#include <opencv2/core.hpp>

using namespace QDV;

bool ImageArithmeticTool::configure(const QJsonObject& params) {
    if (!params.contains("operation")) {
        return false;
    }

    m_operation = params["operation"].toString();
    QStringList validOps = {"add", "subtract", "multiply", "divide", "and", "or", "xor", "not"};
    if (!validOps.contains(m_operation)) {
        return false;
    }

    // P1-B4 修复：优先使用元数据定义的 useScalar 布尔参数
    // 之前 m_useScalar = params.contains("scalar") 仅检查 scalar 字段是否存在
    // 但 OperatorDescriptors 同时定义了 useScalar(Bool) 和 scalar(Float) 两个参数，
    // UI 总会带 scalar 默认值，导致用户取消勾选 useScalar 时仍被错误识别为标量模式
    if (params.contains("useScalar")) {
        m_useScalar = params["useScalar"].toBool();
    } else {
        // 回退兼容：旧存档无 useScalar 字段时，按 scalar 字段是否存在判断
        m_useScalar = params.contains("scalar");
    }
    if (m_useScalar && params.contains("scalar")) {
        m_scalar = params["scalar"].toDouble();
    }

    return true;
}

bool ImageArithmeticTool::execute(const cv::Mat& input, ToolResult& result) {
    result.elapsedMs = 0;
    cv::Mat output;

    if (m_operation == "add") {
        if (m_useScalar) {
            output = input + cv::Scalar(m_scalar, m_scalar, m_scalar);
        } else if (m_hasSecondImage && m_secondImage.size() == input.size()) {
            cv::add(input, m_secondImage, output);
        } else {
            output = input.clone();
        }
    } else if (m_operation == "subtract") {
        if (m_useScalar) {
            output = input - cv::Scalar(m_scalar, m_scalar, m_scalar);
        } else if (m_hasSecondImage && m_secondImage.size() == input.size()) {
            cv::subtract(input, m_secondImage, output);
        } else {
            output = input.clone();
        }
    } else if (m_operation == "multiply") {
        if (m_useScalar) {
            output = input * m_scalar;
        } else if (m_hasSecondImage && m_secondImage.size() == input.size()) {
            cv::multiply(input, m_secondImage, output);
        } else {
            output = input.clone();
        }
    } else if (m_operation == "divide") {
        if (m_useScalar && m_scalar != 0.0) {
            output = input / m_scalar;
        } else {
            output = input.clone();
        }
    } else if (m_operation == "and") {
        if (m_hasSecondImage && m_secondImage.size() == input.size()) {
            cv::bitwise_and(input, m_secondImage, output);
        } else {
            output = input.clone();
        }
    } else if (m_operation == "or") {
        if (m_hasSecondImage && m_secondImage.size() == input.size()) {
            cv::bitwise_or(input, m_secondImage, output);
        } else {
            output = input.clone();
        }
    } else if (m_operation == "xor") {
        if (m_hasSecondImage && m_secondImage.size() == input.size()) {
            cv::bitwise_xor(input, m_secondImage, output);
        } else {
            output = input.clone();
        }
    } else if (m_operation == "not") {
        cv::bitwise_not(input, output);
    }

    result.overlayImage = output;
    result.ok = !output.empty();
    result.score = result.ok ? 1.0 : 0.0;
    result.data["operation"] = m_operation;

    return result.ok;
}

QJsonObject ImageArithmeticTool::serialize() const {
    QJsonObject obj;
    obj["id"] = m_id;
    obj["type"] = type();
    obj["operation"] = m_operation;
    // P1-B4 修复：持久化 useScalar 布尔状态，避免加载后状态丢失
    obj["useScalar"] = m_useScalar;
    if (m_useScalar) {
        obj["scalar"] = m_scalar;
    }
    return obj;
}

bool ImageArithmeticTool::deserialize(const QJsonObject& data) {
    m_id = data["id"].toString();
    m_operation = data["operation"].toString("add");
    // P1-B4 修复：优先读取 useScalar 字段，回退到旧格式（仅 scalar 字段）
    if (data.contains("useScalar")) {
        m_useScalar = data["useScalar"].toBool();
    } else {
        m_useScalar = data.contains("scalar");
    }
    if (m_useScalar && data.contains("scalar")) {
        m_scalar = data["scalar"].toDouble();
    }
    return true;
}