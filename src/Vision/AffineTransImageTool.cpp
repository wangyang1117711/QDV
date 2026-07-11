#include "AffineTransImageTool.h"
#include <opencv2/imgproc.hpp>
#include <algorithm>

using namespace QDV;

AffineTransImageTool::AffineTransImageTool() {
    m_name = "仿射变换";
}

bool AffineTransImageTool::configure(const QJsonObject& params) {
    if (params.contains("angle")) m_angle = params["angle"].toDouble();
    if (params.contains("scale")) m_scale = params["scale"].toDouble();
    if (params.contains("tx")) m_tx = params["tx"].toDouble();
    if (params.contains("ty")) m_ty = params["ty"].toDouble();

    // 钳制缩放比例到有效范围，避免退化变换
    m_scale = std::clamp(m_scale, 0.01, 100.0);

    return true;
}

bool AffineTransImageTool::execute(const cv::Mat& input, ToolResult& result) {
    if (input.empty()) {
        result.ok = false;
        return false;
    }

    // 以图像中心作为旋转中心
    cv::Point2f center(input.cols / 2.0f, input.rows / 2.0f);
    // 构建 2x3 仿射矩阵（同时包含旋转与缩放）
    cv::Mat rotMat = cv::getRotationMatrix2D(center, m_angle, m_scale);

    // 在仿射矩阵第三列叠加平移量
    rotMat.at<double>(0, 2) += m_tx;
    rotMat.at<double>(1, 2) += m_ty;

    cv::Mat output;
    cv::warpAffine(input, output, rotMat, input.size(),
                   cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar(0, 0, 0));

    // overlayImage 必须为 BGR 格式：单通道则转换，多通道直接克隆
    if (output.channels() == 1) {
        cv::cvtColor(output, result.overlayImage, cv::COLOR_GRAY2BGR);
    } else {
        result.overlayImage = output.clone();
    }

    result.ok = !output.empty();
    result.score = result.ok ? 1.0 : 0.0;
    result.data["angle"] = m_angle;
    result.data["scale"] = m_scale;
    result.data["tx"] = m_tx;
    result.data["ty"] = m_ty;
    result.data["outputWidth"] = output.cols;
    result.data["outputHeight"] = output.rows;

    return result.ok;
}

QJsonObject AffineTransImageTool::serialize() const {
    QJsonObject obj;
    obj["id"] = m_id;
    obj["type"] = type();
    obj["name"] = m_name;
    obj["angle"] = m_angle;
    obj["scale"] = m_scale;
    obj["tx"] = m_tx;
    obj["ty"] = m_ty;
    return obj;
}

bool AffineTransImageTool::deserialize(const QJsonObject& data) {
    if (data.contains("id")) m_id = data["id"].toString();
    if (data.contains("name")) m_name = data["name"].toString();
    if (data.contains("angle")) m_angle = data["angle"].toDouble();
    if (data.contains("scale")) m_scale = data["scale"].toDouble();
    if (data.contains("tx")) m_tx = data["tx"].toDouble();
    if (data.contains("ty")) m_ty = data["ty"].toDouble();

    // 反序列化后同样钳制，保证状态一致
    m_scale = std::clamp(m_scale, 0.01, 100.0);

    return true;
}
