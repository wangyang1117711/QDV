#include "EdgesSubPixTool.h"
#include "Core/Logger.h"
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>

using namespace QDV;

namespace {
constexpr double kThresholdMin = 0.0;
constexpr double kThresholdMax = 255.0;

// 双线性采样浮点坐标处的像素值（越界返回 0）
inline float sampleBilinear(const cv::Mat& src, float x, float y) {
    if (x < 0.0f || y < 0.0f || x > src.cols - 1.0f || y > src.rows - 1.0f) {
        return 0.0f;
    }
    const int x0 = static_cast<int>(x);
    const int y0 = static_cast<int>(y);
    const int x1 = std::min(x0 + 1, src.cols - 1);
    const int y1 = std::min(y0 + 1, src.rows - 1);
    const float dx = x - static_cast<float>(x0);
    const float dy = y - static_cast<float>(y0);
    const float v00 = src.at<float>(y0, x0);
    const float v01 = src.at<float>(y0, x1);
    const float v10 = src.at<float>(y1, x0);
    const float v11 = src.at<float>(y1, x1);
    return v00 * (1.0f - dx) * (1.0f - dy) +
           v01 * dx * (1.0f - dy) +
           v10 * (1.0f - dx) * dy +
           v11 * dx * dy;
}
}

EdgesSubPixTool::EdgesSubPixTool() {
    m_name = "亚像素边缘";
}

bool EdgesSubPixTool::configure(const QJsonObject& params) {
    if (params.contains("lowThreshold")) {
        const double v = params["lowThreshold"].toDouble();
        if (v < kThresholdMin || v > kThresholdMax) {
            Logger::warn(QString("EdgesSubPixTool: lowThreshold %1 超出有效范围 [%2,%3]，已钳制")
                .arg(v).arg(kThresholdMin).arg(kThresholdMax));
            m_lowThreshold = std::clamp(v, kThresholdMin, kThresholdMax);
        } else {
            m_lowThreshold = v;
        }
    }
    if (params.contains("highThreshold")) {
        const double v = params["highThreshold"].toDouble();
        if (v < kThresholdMin || v > kThresholdMax) {
            Logger::warn(QString("EdgesSubPixTool: highThreshold %1 超出有效范围 [%2,%3]，已钳制")
                .arg(v).arg(kThresholdMin).arg(kThresholdMax));
            m_highThreshold = std::clamp(v, kThresholdMin, kThresholdMax);
        } else {
            m_highThreshold = v;
        }
    }
    if (params.contains("apertureSize")) {
        const int v = params["apertureSize"].toInt();
        // cv::Canny 仅接受 3/5/7
        if (v != 3 && v != 5 && v != 7) {
            Logger::warn(QString("EdgesSubPixTool: apertureSize %1 非法，仅支持 3/5/7，已回退到 3").arg(v));
            m_apertureSize = 3;
        } else {
            m_apertureSize = v;
        }
    }
    // 保证 low <= high（Canny 要求）
    if (m_lowThreshold > m_highThreshold) {
        Logger::warn(QString("EdgesSubPixTool: lowThreshold(%1) > highThreshold(%2)，已交换")
            .arg(m_lowThreshold).arg(m_highThreshold));
        std::swap(m_lowThreshold, m_highThreshold);
    }
    return true;
}

bool EdgesSubPixTool::execute(const cv::Mat& input, ToolResult& result) {
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
    if (gray.type() != CV_8UC1) {
        gray.convertTo(gray, CV_8U);
    }

    // Canny 提取边缘
    cv::Mat edges;
    cv::Canny(gray, edges, m_lowThreshold, m_highThreshold, m_apertureSize);

    // 计算 Sobel 梯度（与 Canny 同孔径），用于确定梯度方向与幅值
    cv::Mat dx, dy;
    cv::Sobel(gray, dx, CV_32F, 1, 0, m_apertureSize);
    cv::Sobel(gray, dy, CV_32F, 0, 1, m_apertureSize);

    // 梯度幅值（用于抛物线插值的响应）
    cv::Mat mag;
    cv::magnitude(dx, dy, mag);

    // overlay 必须为 BGR
    cv::Mat overlay;
    if (input.channels() == 1) {
        cv::cvtColor(input, overlay, cv::COLOR_GRAY2BGR);
    } else {
        overlay = input.clone();
    }

    int edgePointCount = 0;
    const float eps = 1e-6f;

    for (int y = 0; y < edges.rows; ++y) {
        const uchar* edgeRow = edges.ptr<uchar>(y);
        for (int x = 0; x < edges.cols; ++x) {
            if (edgeRow[x] == 0) {
                continue;
            }
            // 当前点梯度
            const float gx = dx.at<float>(y, x);
            const float gy = dy.at<float>(y, x);
            const float m = mag.at<float>(y, x);

            float sx = static_cast<float>(x);
            float sy = static_cast<float>(y);

            if (m >= eps) {
                // 梯度方向单位向量
                const float nx = gx / m;
                const float ny = gy / m;

                // 沿梯度方向采样两侧幅值（步长 1 像素）
                const float mMinus = sampleBilinear(mag, x - nx, y - ny);
                const float mPlus  = sampleBilinear(mag, x + nx, y + ny);

                // 抛物线插值求亚像素偏移 d ∈ [-1, 1]
                // 极值偏移: d = 0.5*(mMinus - mPlus) / (mMinus - 2*m + mPlus)
                const float denom = mMinus - 2.0f * m + mPlus;
                if (std::fabs(denom) > eps) {
                    float d = 0.5f * (mMinus - mPlus) / denom;
                    d = std::clamp(d, -1.0f, 1.0f);
                    sx = x + d * nx;
                    sy = y + d * ny;
                }
            }

            // 绿色标注亚像素边缘点（直接置像素，避免大量 cv::circle 调用开销）
            const int px = cvRound(sx);
            const int py = cvRound(sy);
            if (px >= 0 && px < overlay.cols && py >= 0 && py < overlay.rows) {
                cv::Vec3b& pix = overlay.at<cv::Vec3b>(py, px);
                pix[0] = 0; pix[1] = 255; pix[2] = 0;  // BGR 绿色
            }
            ++edgePointCount;
        }
    }

    result.overlayImage = overlay;
    result.ok = edgePointCount > 0;
    result.score = static_cast<double>(edgePointCount);

    result.data["count"] = edgePointCount;
    result.data["lowThreshold"] = m_lowThreshold;
    result.data["highThreshold"] = m_highThreshold;
    result.data["apertureSize"] = m_apertureSize;

    return true;
}

QJsonObject EdgesSubPixTool::serialize() const {
    QJsonObject obj;
    obj["id"] = m_id;
    obj["type"] = type();
    obj["name"] = m_name;
    obj["lowThreshold"] = m_lowThreshold;
    obj["highThreshold"] = m_highThreshold;
    obj["apertureSize"] = m_apertureSize;
    return obj;
}

bool EdgesSubPixTool::deserialize(const QJsonObject& data) {
    // 仅更新 data 中实际存在的字段，避免缺失字段被覆盖为默认值 0
    if (data.contains("id")) {
        m_id = data["id"].toString();
    }
    if (data.contains("name")) {
        m_name = data["name"].toString();
    }
    if (data.contains("lowThreshold")) {
        const double v = data["lowThreshold"].toDouble();
        m_lowThreshold = std::clamp(v, kThresholdMin, kThresholdMax);
    }
    if (data.contains("highThreshold")) {
        const double v = data["highThreshold"].toDouble();
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
