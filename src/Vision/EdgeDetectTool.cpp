#include "EdgeDetectTool.h"
#include "Core/Logger.h"
#include <opencv2/imgproc.hpp>
#include <algorithm>

using namespace QDV;

// P1-C1 修复：阈值范围常量。Canny 要求 0 <= low <= high <= 255，
// apertureSize 必须是 3/5/7 之一。
namespace {
constexpr int kThresholdMin = 0;
constexpr int kThresholdMax = 255;
}

EdgeDetectTool::EdgeDetectTool() {
    m_name = "边缘检测";
}

bool EdgeDetectTool::configure(const QJsonObject& params) {
    // P1-C1 修复（PreReleaseReviewReport Minor 10）：阈值范围验证
    // 之前未做范围检查，可能传入负数或 >255 的值导致 cv::Canny 行为未定义。
    if (params.contains("lowThreshold")) {
        const int v = params["lowThreshold"].toInt();
        if (v < kThresholdMin || v > kThresholdMax) {
            Logger::warn(QString("EdgeDetectTool: lowThreshold %1 超出有效范围 [%2,%3]，已钳制")
                .arg(v).arg(kThresholdMin).arg(kThresholdMax));
            m_lowThreshold = std::clamp(v, kThresholdMin, kThresholdMax);
        } else {
            m_lowThreshold = v;
        }
    }
    if (params.contains("highThreshold")) {
        const int v = params["highThreshold"].toInt();
        if (v < kThresholdMin || v > kThresholdMax) {
            Logger::warn(QString("EdgeDetectTool: highThreshold %1 超出有效范围 [%2,%3]，已钳制")
                .arg(v).arg(kThresholdMin).arg(kThresholdMax));
            m_highThreshold = std::clamp(v, kThresholdMin, kThresholdMax);
        } else {
            m_highThreshold = v;
        }
    }
    if (params.contains("apertureSize")) {
        const int v = params["apertureSize"].toInt();
        // cv::Canny 仅接受 3/5/7 三个值
        if (v != 3 && v != 5 && v != 7) {
            Logger::warn(QString("EdgeDetectTool: apertureSize %1 非法，仅支持 3/5/7，已回退到 3").arg(v));
            m_apertureSize = 3;
        } else {
            m_apertureSize = v;
        }
    }
    // 保证 low <= high（Canny 要求）
    if (m_lowThreshold > m_highThreshold) {
        Logger::warn(QString("EdgeDetectTool: lowThreshold(%1) > highThreshold(%2)，已交换")
            .arg(m_lowThreshold).arg(m_highThreshold));
        std::swap(m_lowThreshold, m_highThreshold);
    }
    return true;
}

bool EdgeDetectTool::execute(const cv::Mat& input, ToolResult& result) {
    if (input.empty()) {
        result.ok = false;
        return false;
    }
    
    cv::Mat gray, edges;
    
    if (input.channels() == 3) {
        cv::cvtColor(input, gray, cv::COLOR_BGR2GRAY);
    } else {
        gray = input.clone();
    }
    
    cv::Canny(gray, edges, m_lowThreshold, m_highThreshold, m_apertureSize);
    
    result.overlayImage = input.clone();
    result.overlayImage.setTo(cv::Scalar(0, 255, 0), edges);
    
    int edgeCount = cv::countNonZero(edges);
    result.ok = edgeCount > 0;
    result.score = static_cast<double>(edgeCount) / (edges.rows * edges.cols);
    
    result.data["edgeCount"] = edgeCount;
    result.data["lowThreshold"] = m_lowThreshold;
    result.data["highThreshold"] = m_highThreshold;
    
    return true;
}

QJsonObject EdgeDetectTool::serialize() const {
    QJsonObject obj;
    obj["id"] = m_id;
    obj["type"] = type();
    obj["name"] = m_name;
    obj["lowThreshold"] = m_lowThreshold;
    obj["highThreshold"] = m_highThreshold;
    obj["apertureSize"] = m_apertureSize;
    return obj;
}

bool EdgeDetectTool::deserialize(const QJsonObject& data) {
    // P0 修复：仅更新 data 中实际存在的字段，避免缺失字段被覆盖为默认值 0。
    // 之前 m_apertureSize = data["apertureSize"].toInt() 在 key 不存在时返回 0，
    // 覆盖了 configure() 已校验的合法值（3/5/7），导致 cv::Canny 崩溃。
    if (data.contains("id")) {
        m_id = data["id"].toString();
    }
    if (data.contains("name")) {
        m_name = data["name"].toString();
    }
    if (data.contains("lowThreshold")) {
        const int v = data["lowThreshold"].toInt();
        m_lowThreshold = std::clamp(v, kThresholdMin, kThresholdMax);
    }
    if (data.contains("highThreshold")) {
        const int v = data["highThreshold"].toInt();
        m_highThreshold = std::clamp(v, kThresholdMin, kThresholdMax);
    }
    if (data.contains("apertureSize")) {
        const int v = data["apertureSize"].toInt();
        // cv::Canny 仅接受 3/5/7，非法值回退到 3
        m_apertureSize = (v == 3 || v == 5 || v == 7) ? v : 3;
    }
    // 保证 low <= high
    if (m_lowThreshold > m_highThreshold) {
        std::swap(m_lowThreshold, m_highThreshold);
    }
    return true;
}