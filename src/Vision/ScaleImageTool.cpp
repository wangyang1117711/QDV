#include "ScaleImageTool.h"
#include "Core/Logger.h"
#include <opencv2/imgproc.hpp>

using namespace QDV;

ScaleImageTool::ScaleImageTool() {
    m_name = "灰度缩放";
}

bool ScaleImageTool::configure(const QJsonObject& params) {
    if (params.contains("scale")) {
        // scale 范围 0.01~255，超出则钳制
        double s = params["scale"].toDouble();
        if (s < 0.01) {
            s = 0.01;
            Logger::warn("ScaleImageTool: scale < 0.01，已钳制为 0.01");
        } else if (s > 255.0) {
            s = 255.0;
            Logger::warn("ScaleImageTool: scale > 255，已钳制为 255");
        }
        m_scale = s;
    }
    if (params.contains("offset")) {
        // offset 范围 -255~255，超出则钳制
        double o = params["offset"].toDouble();
        if (o < -255.0) {
            o = -255.0;
            Logger::warn("ScaleImageTool: offset < -255，已钳制为 -255");
        } else if (o > 255.0) {
            o = 255.0;
            Logger::warn("ScaleImageTool: offset > 255，已钳制为 255");
        }
        m_offset = o;
    }
    return true;
}

bool ScaleImageTool::execute(const cv::Mat& input, ToolResult& result) {
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

    // cv::convertScaleAbs: dst = saturate_cast<uchar>(|src * scale + offset|)
    // 注：convertScaleAbs 会取绝对值后再转 uchar，与题目要求一致
    cv::Mat scaled;
    cv::convertScaleAbs(gray, scaled, m_scale, m_offset);

    // overlayImage 必须为 BGR：左侧原图(转BGR) + 右侧缩放结果(转BGR) 并排
    cv::Mat grayBgr, scaledBgr;
    cv::cvtColor(gray, grayBgr, cv::COLOR_GRAY2BGR);
    cv::cvtColor(scaled, scaledBgr, cv::COLOR_GRAY2BGR);

    cv::Mat canvas(grayBgr.rows, grayBgr.cols + scaledBgr.cols, grayBgr.type());
    cv::Mat left = canvas(cv::Rect(0, 0, grayBgr.cols, grayBgr.rows));
    cv::Mat right = canvas(cv::Rect(grayBgr.cols, 0, scaledBgr.cols, scaledBgr.rows));
    grayBgr.copyTo(left);
    scaledBgr.copyTo(right);
    result.overlayImage = canvas;

    result.ok = true;
    result.score = 1.0;
    result.data["scale"] = m_scale;
    result.data["offset"] = m_offset;

    return true;
}

QJsonObject ScaleImageTool::serialize() const {
    QJsonObject obj;
    obj["id"] = m_id;
    obj["type"] = type();
    obj["name"] = m_name;
    obj["scale"] = m_scale;
    obj["offset"] = m_offset;
    return obj;
}

bool ScaleImageTool::deserialize(const QJsonObject& data) {
    // 使用 contains 检查，避免缺失字段被覆盖为默认值
    if (data.contains("id")) m_id = data["id"].toString();
    if (data.contains("name")) m_name = data["name"].toString();
    // scale/offset 走 configure 钳制逻辑，保证反序列化后的值仍合法
    QJsonObject params;
    if (data.contains("scale"))  params["scale"]  = data["scale"].toDouble();
    if (data.contains("offset")) params["offset"] = data["offset"].toDouble();
    if (!params.isEmpty()) configure(params);
    return true;
}
