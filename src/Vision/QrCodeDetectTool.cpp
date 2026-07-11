#include "QrCodeDetectTool.h"
#include "Core/Logger.h"
#include <opencv2/imgproc.hpp>
#include <opencv2/objdetect.hpp>

using namespace QDV;

QrCodeDetectTool::QrCodeDetectTool() {
    m_name = "二维码识别";
}

bool QrCodeDetectTool::configure(const QJsonObject& params) {
    // 无可配置参数，保留基类调用约定
    m_params = params;
    return true;
}

bool QrCodeDetectTool::execute(const cv::Mat& input, ToolResult& result) {
    if (input.empty()) {
        result.ok = false;
        return false;
    }

    // overlay 必须为 BGR
    cv::Mat overlay;
    if (input.channels() == 1) {
        cv::cvtColor(input, overlay, cv::COLOR_GRAY2BGR);
    } else {
        overlay = input.clone();
    }

    cv::QRCodeDetector detector;
    cv::Mat points;
    const std::string decoded = detector.detectAndDecode(input, points);

    const bool detected = !points.empty();
    const bool hasText = !decoded.empty();

    if (detected) {
        // points 通常是 4x1 CV_32FC2，含 4 个角点
        std::vector<cv::Point2f> pts2f(4);
        bool ptsOk = false;
        if (points.type() == CV_32FC2 && points.rows == 4 && points.cols == 1) {
            for (int i = 0; i < 4; ++i) {
                pts2f[i] = points.at<cv::Point2f>(i, 0);
            }
            ptsOk = true;
        } else if (!points.empty()) {
            // 其它布局：尝试 reshape 为 4 行单通道读取
            cv::Mat flat = points.reshape(1);
            if (flat.type() == CV_32F && flat.rows * flat.cols >= 8) {
                for (int i = 0; i < 4; ++i) {
                    pts2f[i] = cv::Point2f(flat.at<float>(i, 0), flat.at<float>(i, 1));
                }
                ptsOk = true;
            }
        }
        if (ptsOk) {
            std::vector<cv::Point> pts;
            for (const auto& p : pts2f) {
                pts.emplace_back(cvRound(p.x), cvRound(p.y));
            }
            cv::polylines(overlay, pts, true, cv::Scalar(0, 255, 0), 2);
            for (const auto& p : pts) {
                cv::circle(overlay, p, 4, cv::Scalar(0, 0, 255), -1);
            }
        }
    }

    // 显示解码文本（UTF8，中文可能乱码仅显示）
    if (hasText) {
        cv::putText(overlay, decoded, cv::Point(10, 30),
                    cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 255, 0), 2);
    } else if (detected) {
        cv::putText(overlay, "detected (no decode)", cv::Point(10, 30),
                    cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 200, 255), 2);
    } else {
        cv::putText(overlay, "no QR", cv::Point(10, 30),
                    cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 0, 255), 2);
    }

    result.overlayImage = overlay;
    result.ok = true;  // 算子执行成功，无论是否检测到
    result.data["text"] = QString::fromStdString(decoded);
    result.data["count"] = hasText ? 1 : 0;
    result.data["detected"] = detected;
    return true;
}

QJsonObject QrCodeDetectTool::serialize() const {
    QJsonObject obj;
    obj["id"] = m_id;
    obj["type"] = type();
    obj["name"] = m_name;
    return obj;
}

bool QrCodeDetectTool::deserialize(const QJsonObject& data) {
    if (data.contains("id")) m_id = data["id"].toString();
    if (data.contains("name")) m_name = data["name"].toString();
    return true;
}
