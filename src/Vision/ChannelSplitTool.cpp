#include "ChannelSplitTool.h"
#include "Core/Logger.h"
#include <opencv2/imgproc.hpp>

using namespace QDV;

ChannelSplitTool::ChannelSplitTool() {
    m_name = "通道分离";
}

bool ChannelSplitTool::configure(const QJsonObject& params) {
    if (params.contains("colorSpace")) {
        const QString v = params["colorSpace"].toString().toUpper();
        if (v == "RGB" || v == "HSV" || v == "LAB" || v == "YUV") {
            m_colorSpace = v;
        } else {
            Logger::warn(QString("ChannelSplitTool: colorSpace '%1' 非法，回退为 'RGB'").arg(v));
            m_colorSpace = "RGB";
        }
    }
    if (params.contains("channelIndex")) {
        int idx = params["channelIndex"].toInt();
        if (idx >= 0 && idx <= 2) {
            m_channelIndex = idx;
        } else {
            Logger::warn(QString("ChannelSplitTool: channelIndex %1 越界，回退为 0").arg(idx));
            m_channelIndex = 0;
        }
    }
    return true;
}

bool ChannelSplitTool::execute(const cv::Mat& input, ToolResult& result) {
    if (input.empty()) {
        result.ok = false;
        return false;
    }

    // 灰度图直接返回
    if (input.channels() == 1) {
        cv::cvtColor(input, result.overlayImage, cv::COLOR_GRAY2BGR);
        result.data["colorSpace"] = m_colorSpace;
        result.data["channelIndex"] = m_channelIndex;
        result.data["mean"] = cv::mean(input)[0];
        result.ok = true;
        return true;
    }

    // 通道数不足 3：警告并转 BGR
    if (input.channels() < 3) {
        Logger::warn("ChannelSplitTool: 输入通道数 < 3，无法分离，直接返回");
        cv::cvtColor(input, result.overlayImage, cv::COLOR_GRAY2BGR);
        result.data["colorSpace"] = m_colorSpace;
        result.data["channelIndex"] = m_channelIndex;
        result.data["mean"] = 0.0;
        result.ok = true;
        return true;
    }

    // 颜色空间转换
    cv::Mat converted;
    if (m_colorSpace == "RGB") {
        // OpenCV 默认 BGR，交换 R/B 通道得到 RGB
        cv::cvtColor(input, converted, cv::COLOR_BGR2RGB);
    } else if (m_colorSpace == "HSV") {
        cv::cvtColor(input, converted, cv::COLOR_BGR2HSV);
    } else if (m_colorSpace == "LAB") {
        cv::cvtColor(input, converted, cv::COLOR_BGR2Lab);
    } else if (m_colorSpace == "YUV") {
        cv::cvtColor(input, converted, cv::COLOR_BGR2YUV);
    } else {
        converted = input.clone();
    }

    // 通道分离
    std::vector<cv::Mat> channels;
    cv::split(converted, channels);

    if (m_channelIndex < 0 || m_channelIndex >= static_cast<int>(channels.size())) {
        Logger::warn(QString("ChannelSplitTool: channelIndex %1 越界（共 %2 通道），回退为 0")
                         .arg(m_channelIndex).arg(channels.size()));
        m_channelIndex = 0;
    }

    cv::Mat ch = channels[m_channelIndex];

    // 单通道转 BGR 作为 overlay
    cv::cvtColor(ch, result.overlayImage, cv::COLOR_GRAY2BGR);

    // 通道均值
    const cv::Scalar mean = cv::mean(ch);

    result.data["colorSpace"] = m_colorSpace;
    result.data["channelIndex"] = m_channelIndex;
    result.data["mean"] = mean[0];
    result.ok = true;
    return true;
}

QJsonObject ChannelSplitTool::serialize() const {
    QJsonObject obj;
    obj["id"] = m_id;
    obj["type"] = type();
    obj["name"] = m_name;
    obj["colorSpace"] = m_colorSpace;
    obj["channelIndex"] = m_channelIndex;
    return obj;
}

bool ChannelSplitTool::deserialize(const QJsonObject& data) {
    if (data.contains("id")) m_id = data["id"].toString();
    if (data.contains("name")) m_name = data["name"].toString();
    if (data.contains("colorSpace")) {
        const QString v = data["colorSpace"].toString().toUpper();
        if (v == "RGB" || v == "HSV" || v == "LAB" || v == "YUV") {
            m_colorSpace = v;
        }
    }
    if (data.contains("channelIndex")) {
        int idx = data["channelIndex"].toInt();
        if (idx >= 0 && idx <= 2) {
            m_channelIndex = idx;
        }
    }
    return true;
}
