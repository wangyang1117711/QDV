#include "RgbExtractTool.h"
#include "Core/Logger.h"
#include <opencv2/imgproc.hpp>

using namespace QDV;

RgbExtractTool::RgbExtractTool() {
    m_name = "RGB通道提取";
}

bool RgbExtractTool::configure(const QJsonObject& params) {
    if (params.contains("channel")) {
        const QString v = params["channel"].toString().toLower();
        if (v == "r" || v == "g" || v == "b") {
            m_channel = v;
        } else {
            Logger::warn(QString("RgbExtractTool: channel '%1' 非法，回退为 'r'").arg(v));
            m_channel = "r";
        }
    }
    return true;
}

bool RgbExtractTool::execute(const cv::Mat& input, ToolResult& result) {
    if (input.empty()) {
        result.ok = false;
        return false;
    }

    // 通道数不足 3：警告并直接克隆转 BGR 作为 overlay
    if (input.channels() < 3) {
        Logger::warn("RgbExtractTool: 输入通道数 < 3，无法提取 RGB，直接转 BGR 返回");
        if (input.channels() == 1) {
            cv::cvtColor(input, result.overlayImage, cv::COLOR_GRAY2BGR);
        } else {
            cv::cvtColor(input, result.overlayImage, cv::COLOR_GRAY2BGR);
        }
        result.data["channel"] = m_channel;
        result.data["mean"] = 0.0;
        result.ok = true;
        return true;
    }

    // OpenCV BGR 顺序：B=0, G=1, R=2
    int chIdx = 2;
    if (m_channel == "g") chIdx = 1;
    else if (m_channel == "b") chIdx = 0;

    cv::Mat ch;
    cv::extractChannel(input, ch, chIdx);

    // 单通道转 BGR 作为 overlay
    cv::cvtColor(ch, result.overlayImage, cv::COLOR_GRAY2BGR);

    // 通道均值
    const cv::Scalar mean = cv::mean(ch);

    result.data["channel"] = m_channel;
    result.data["mean"] = mean[0];
    result.ok = true;
    return true;
}

QJsonObject RgbExtractTool::serialize() const {
    QJsonObject obj;
    obj["id"] = m_id;
    obj["type"] = type();
    obj["name"] = m_name;
    obj["channel"] = m_channel;
    return obj;
}

bool RgbExtractTool::deserialize(const QJsonObject& data) {
    if (data.contains("id")) m_id = data["id"].toString();
    if (data.contains("name")) m_name = data["name"].toString();
    if (data.contains("channel")) {
        const QString v = data["channel"].toString().toLower();
        if (v == "r" || v == "g" || v == "b") {
            m_channel = v;
        }
    }
    return true;
}
