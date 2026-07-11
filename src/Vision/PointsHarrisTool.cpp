#include "PointsHarrisTool.h"
#include "Core/Logger.h"
#include <opencv2/imgproc.hpp>
#include <algorithm>

using namespace QDV;

namespace {
constexpr int kBlockSizeMin = 1;
constexpr int kBlockSizeMax = 10;
constexpr int kKsizeMin = 3;
constexpr int kKsizeMax = 31;
constexpr double kKMin = 0.01;
constexpr double kKMax = 0.25;
constexpr double kThresholdMin = 0.0;
constexpr double kThresholdMax = 1.0;

// 将 ksize 钳制为 [kKsizeMin, kKsizeMax] 内的奇数
int clampOddKsize(int v) {
    if (v < kKsizeMin) v = kKsizeMin;
    if (v > kKsizeMax) v = kKsizeMax;
    if ((v % 2) == 0) v += 1;             // 偶数则 +1 变奇数
    if (v > kKsizeMax) v = kKsizeMax - 1; // +1 后若越界（仅当上限为偶数时可能），退到最大奇数
    return v;
}
}

PointsHarrisTool::PointsHarrisTool() {
    m_name = "Harris角点";
}

bool PointsHarrisTool::configure(const QJsonObject& params) {
    if (params.contains("blockSize")) {
        const int v = params["blockSize"].toInt();
        if (v < kBlockSizeMin || v > kBlockSizeMax) {
            Logger::warn(QString("PointsHarrisTool: blockSize %1 超出有效范围 [%2,%3]，已钳制")
                .arg(v).arg(kBlockSizeMin).arg(kBlockSizeMax));
            m_blockSize = std::clamp(v, kBlockSizeMin, kBlockSizeMax);
        } else {
            m_blockSize = v;
        }
    }
    if (params.contains("ksize")) {
        const int v = params["ksize"].toInt();
        // cornerHarris 要求 ksize 为正奇数，范围 [3, 31]
        const int clamped = clampOddKsize(v);
        if (clamped != v) {
            Logger::warn(QString("PointsHarrisTool: ksize %1 非法（需为 [%2,%3] 内奇数），已调整为 %4")
                .arg(v).arg(kKsizeMin).arg(kKsizeMax).arg(clamped));
        }
        m_ksize = clamped;
    }
    if (params.contains("k")) {
        const double v = params["k"].toDouble();
        if (v < kKMin || v > kKMax) {
            Logger::warn(QString("PointsHarrisTool: k %1 超出有效范围 [%2,%3]，已钳制")
                .arg(v).arg(kKMin).arg(kKMax));
            m_k = std::clamp(v, kKMin, kKMax);
        } else {
            m_k = v;
        }
    }
    if (params.contains("threshold")) {
        const double v = params["threshold"].toDouble();
        if (v < kThresholdMin || v > kThresholdMax) {
            Logger::warn(QString("PointsHarrisTool: threshold %1 超出有效范围 [%2,%3]，已钳制")
                .arg(v).arg(kThresholdMin).arg(kThresholdMax));
            m_threshold = std::clamp(v, kThresholdMin, kThresholdMax);
        } else {
            m_threshold = v;
        }
    }
    return true;
}

bool PointsHarrisTool::execute(const cv::Mat& input, ToolResult& result) {
    if (input.empty()) {
        result.ok = false;
        return false;
    }

    cv::Mat gray;
    if (input.channels() == 3) {
        cv::cvtColor(input, gray, cv::COLOR_BGR2GRAY);
    } else {
        gray = input.clone();
    }
    // cornerHarris 要求 CV_8UC1 或 CV_32FC1
    if (gray.type() != CV_8UC1) {
        gray.convertTo(gray, CV_8U);
    }

    cv::Mat harrisResp;
    cv::cornerHarris(gray, harrisResp, m_blockSize, m_ksize, m_k);

    // 归一化到 [0, 1]，便于按 threshold 筛选
    cv::Mat respNorm;
    cv::normalize(harrisResp, respNorm, 0.0, 1.0, cv::NORM_MINMAX, CV_32F);

    // overlay 必须为 BGR
    cv::Mat overlay;
    if (input.channels() == 1) {
        cv::cvtColor(input, overlay, cv::COLOR_GRAY2BGR);
    } else {
        overlay = input.clone();
    }

    int cornerCount = 0;
    for (int y = 0; y < respNorm.rows; ++y) {
        for (int x = 0; x < respNorm.cols; ++x) {
            const float resp = respNorm.at<float>(y, x);
            if (resp > static_cast<float>(m_threshold)) {
                ++cornerCount;
                // 红色圆圈标注角点
                cv::circle(overlay, cv::Point(x, y), 3, cv::Scalar(0, 0, 255), 1);
            }
        }
    }

    result.overlayImage = overlay;
    result.ok = cornerCount > 0;
    result.score = static_cast<double>(cornerCount);

    result.data["count"] = cornerCount;
    result.data["blockSize"] = m_blockSize;
    result.data["ksize"] = m_ksize;
    result.data["k"] = m_k;
    result.data["threshold"] = m_threshold;

    return true;
}

QJsonObject PointsHarrisTool::serialize() const {
    QJsonObject obj;
    obj["id"] = m_id;
    obj["type"] = type();
    obj["name"] = m_name;
    obj["blockSize"] = m_blockSize;
    obj["ksize"] = m_ksize;
    obj["k"] = m_k;
    obj["threshold"] = m_threshold;
    return obj;
}

bool PointsHarrisTool::deserialize(const QJsonObject& data) {
    // 仅更新 data 中实际存在的字段，避免缺失字段被覆盖为默认值 0
    if (data.contains("id")) {
        m_id = data["id"].toString();
    }
    if (data.contains("name")) {
        m_name = data["name"].toString();
    }
    if (data.contains("blockSize")) {
        const int v = data["blockSize"].toInt();
        m_blockSize = std::clamp(v, kBlockSizeMin, kBlockSizeMax);
    }
    if (data.contains("ksize")) {
        const int v = data["ksize"].toInt();
        m_ksize = clampOddKsize(v);
    }
    if (data.contains("k")) {
        const double v = data["k"].toDouble();
        m_k = std::clamp(v, kKMin, kKMax);
    }
    if (data.contains("threshold")) {
        const double v = data["threshold"].toDouble();
        m_threshold = std::clamp(v, kThresholdMin, kThresholdMax);
    }
    return true;
}
