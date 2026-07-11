#include "MeanImageTool.h"
#include "Core/Logger.h"
#include <opencv2/imgproc.hpp>

using namespace QDV;

MeanImageTool::MeanImageTool() {
    m_name = "均值滤波";
}

bool MeanImageTool::configure(const QJsonObject& params) {
    if (params.contains("kernelSize")) {
        // 防御性校验：kernelSize 必须 >= 1 且为奇数，否则 cv::blur 会断言失败
        int ks = params["kernelSize"].toInt();
        if (ks < 1) {
            ks = 1;
            Logger::warn("MeanImageTool: kernelSize < 1，已钳制为 1");
        } else if (ks > 31) {
            ks = 31;
            Logger::warn("MeanImageTool: kernelSize > 31，已钳制为 31");
        } else if (ks % 2 == 0) {
            // 偶数 → 强制 +1 变奇数（cv::blur 要求奇数）
            ks += 1;
            Logger::warn(QString("MeanImageTool: kernelSize 为偶数 %1，已调整为奇数 %2")
                         .arg(ks - 1).arg(ks));
        }
        m_kernelSize = ks;
    }
    return true;
}

bool MeanImageTool::execute(const cv::Mat& input, ToolResult& result) {
    if (input.empty()) {
        result.ok = false;
        return false;
    }

    // 转灰度
    cv::Mat gray;
    if (input.channels() == 3) {
        cv::cvtColor(input, gray, cv::COLOR_BGR2GRAY);
    } else {
        gray = input.clone();
    }

    // 运行时双保险：确保 kernelSize 合法（即使 configure 被绕过）
    int ks = m_kernelSize;
    if (ks < 1) ks = 1;
    if (ks > 31) ks = 31;
    if (ks % 2 == 0) ks += 1;

    cv::Mat blurred;
    cv::blur(gray, blurred, cv::Size(ks, ks));

    // overlayImage 必须为 BGR：左侧原图(转BGR) + 右侧滤波结果(转BGR) 并排
    cv::Mat grayBgr, blurredBgr;
    cv::cvtColor(gray, grayBgr, cv::COLOR_GRAY2BGR);
    cv::cvtColor(blurred, blurredBgr, cv::COLOR_GRAY2BGR);

    cv::Mat canvas(grayBgr.rows, grayBgr.cols + blurredBgr.cols, grayBgr.type());
    cv::Mat left = canvas(cv::Rect(0, 0, grayBgr.cols, grayBgr.rows));
    cv::Mat right = canvas(cv::Rect(grayBgr.cols, 0, blurredBgr.cols, blurredBgr.rows));
    grayBgr.copyTo(left);
    blurredBgr.copyTo(right);
    result.overlayImage = canvas;

    result.ok = true;
    result.score = 1.0;
    result.data["kernelSize"] = ks;

    return true;
}

QJsonObject MeanImageTool::serialize() const {
    QJsonObject obj;
    obj["id"] = m_id;
    obj["type"] = type();
    obj["name"] = m_name;
    obj["kernelSize"] = m_kernelSize;
    return obj;
}

bool MeanImageTool::deserialize(const QJsonObject& data) {
    // 使用 contains 检查，避免缺失字段被覆盖为默认值
    if (data.contains("id")) m_id = data["id"].toString();
    if (data.contains("name")) m_name = data["name"].toString();
    if (data.contains("kernelSize")) {
        QJsonObject params;
        params["kernelSize"] = data["kernelSize"].toInt();
        configure(params);
    }
    return true;
}
