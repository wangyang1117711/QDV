#include "ConnectionTool.h"
#include "Core/Logger.h"
#include <opencv2/imgproc.hpp>
#include <QJsonArray>
#include <algorithm>

using namespace QDV;

namespace {
constexpr double kThresholdMin = 0.0;
constexpr double kThresholdMax = 255.0;
}

ConnectionTool::ConnectionTool() {
    m_name = "连通域分析";
}

bool ConnectionTool::configure(const QJsonObject& params) {
    // 二值化阈值范围校验
    if (params.contains("threshold")) {
        const double v = params["threshold"].toDouble();
        if (v < kThresholdMin || v > kThresholdMax) {
            Logger::warn(QString("ConnectionTool: threshold %1 超出有效范围 [%2,%3]，已钳制")
                .arg(v).arg(kThresholdMin).arg(kThresholdMax));
            m_threshold = std::clamp(v, kThresholdMin, kThresholdMax);
        } else {
            m_threshold = v;
        }
    }
    if (params.contains("connectivity")) {
        const int v = params["connectivity"].toInt();
        // cv::connectedComponentsWithStats 仅接受 4 或 8
        if (v != 4 && v != 8) {
            Logger::warn(QString("ConnectionTool: connectivity %1 非法，仅支持 4/8，已回退到 8").arg(v));
            m_connectivity = 8;
        } else {
            m_connectivity = v;
        }
    }
    if (params.contains("minArea")) {
        m_minArea = params["minArea"].toDouble();
    }
    if (params.contains("maxArea")) {
        m_maxArea = params["maxArea"].toDouble();
    }
    // 保证 minArea <= maxArea
    if (m_minArea > m_maxArea) {
        Logger::warn(QString("ConnectionTool: minArea(%1) > maxArea(%2)，已交换")
            .arg(m_minArea).arg(m_maxArea));
        std::swap(m_minArea, m_maxArea);
    }
    return true;
}

bool ConnectionTool::execute(const cv::Mat& input, ToolResult& result) {
    if (input.empty()) {
        result.ok = false;
        return false;
    }

    cv::Mat gray, binary;
    if (input.channels() == 3) {
        cv::cvtColor(input, gray, cv::COLOR_BGR2GRAY);
    } else {
        gray = input.clone();
    }

    // 二值化：阈值分割
    cv::threshold(gray, binary, m_threshold, 255, cv::THRESH_BINARY);

    // 连通域标记，stats 列: [LEFT, TOP, WIDTH, HEIGHT, AREA]
    cv::Mat labels, stats, centroids;
    int numLabels = cv::connectedComponentsWithStats(binary, labels, stats, centroids,
                                                     m_connectivity, CV_32S);

    // overlay 必须为 BGR；若输入为单通道，先转 BGR
    cv::Mat overlay;
    if (input.channels() == 1) {
        cv::cvtColor(input, overlay, cv::COLOR_GRAY2BGR);
    } else {
        overlay = input.clone();
    }

    // 固定种子生成随机颜色，保证可复现（label 0 为背景，不绘制）
    cv::RNG rng(0xFFFFFFFFu);
    QJsonArray areasArray;
    int validCount = 0;

    for (int i = 1; i < numLabels; ++i) {
        const double area = static_cast<double>(stats.at<int>(i, cv::CC_STAT_AREA));
        if (area < m_minArea || area > m_maxArea) {
            continue;
        }
        ++validCount;
        areasArray.append(area);

        // 用随机颜色标注该连通域
        const cv::Scalar color(rng.uniform(0, 256), rng.uniform(0, 256), rng.uniform(0, 256));
        cv::Mat mask = (labels == i);
        overlay.setTo(color, mask);
    }

    result.overlayImage = overlay;
    result.ok = validCount > 0;
    result.score = static_cast<double>(validCount);

    result.data["count"] = validCount;
    result.data["areas"] = areasArray;
    result.data["threshold"] = m_threshold;
    result.data["connectivity"] = m_connectivity;

    return true;
}

QJsonObject ConnectionTool::serialize() const {
    QJsonObject obj;
    obj["id"] = m_id;
    obj["type"] = type();
    obj["name"] = m_name;
    obj["threshold"] = m_threshold;
    obj["connectivity"] = m_connectivity;
    obj["minArea"] = m_minArea;
    obj["maxArea"] = m_maxArea;
    return obj;
}

bool ConnectionTool::deserialize(const QJsonObject& data) {
    // 仅更新 data 中实际存在的字段，避免缺失字段被覆盖为默认值 0
    if (data.contains("id")) {
        m_id = data["id"].toString();
    }
    if (data.contains("name")) {
        m_name = data["name"].toString();
    }
    if (data.contains("threshold")) {
        const double v = data["threshold"].toDouble();
        m_threshold = std::clamp(v, kThresholdMin, kThresholdMax);
    }
    if (data.contains("connectivity")) {
        const int v = data["connectivity"].toInt();
        // cv::connectedComponentsWithStats 仅接受 4/8，非法值回退到 8
        m_connectivity = (v == 4 || v == 8) ? v : 8;
    }
    if (data.contains("minArea")) {
        m_minArea = data["minArea"].toDouble();
    }
    if (data.contains("maxArea")) {
        m_maxArea = data["maxArea"].toDouble();
    }
    if (m_minArea > m_maxArea) {
        std::swap(m_minArea, m_maxArea);
    }
    return true;
}
