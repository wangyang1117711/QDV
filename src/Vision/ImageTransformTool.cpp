#include "ImageTransformTool.h"

bool ImageTransformTool::configure(const QJsonObject& params) {
    if (!params.contains("transformType")) {
        return false;
    }

    m_transformType = params["transformType"].toString();
    QStringList validTypes = {"resize", "rotate", "flip", "affine", "perspective"};
    if (!validTypes.contains(m_transformType)) {
        return false;
    }

    if (params.contains("angle")) m_angle = params["angle"].toDouble();
    if (params.contains("scaleX")) m_scaleX = params["scaleX"].toDouble();
    if (params.contains("scaleY")) m_scaleY = params["scaleY"].toDouble();
    if (params.contains("flipCode")) m_flipCode = params["flipCode"].toInt();
    if (params.contains("targetWidth")) m_targetWidth = params["targetWidth"].toInt();
    if (params.contains("targetHeight")) m_targetHeight = params["targetHeight"].toInt();

    if (m_scaleX <= 0.0 || m_scaleY <= 0.0) return false;
    if (m_targetWidth <= 0 || m_targetHeight <= 0) return false;

    return true;
}

bool ImageTransformTool::execute(const cv::Mat& input, ToolResult& result) {
    result.elapsedMs = 0;
    cv::Mat output;

    if (m_transformType == "resize") {
        cv::resize(input, output, cv::Size(m_targetWidth, m_targetHeight),
                   0, 0, cv::INTER_LINEAR);
    } else if (m_transformType == "rotate") {
        cv::Point2f center(input.cols / 2.0f, input.rows / 2.0f);
        cv::Mat rotMat = cv::getRotationMatrix2D(center, m_angle, 1.0);
        cv::warpAffine(input, output, rotMat, input.size(),
                       cv::INTER_LINEAR, cv::BORDER_CONSTANT,
                       cv::Scalar(0, 0, 0));
    } else if (m_transformType == "flip") {
        cv::flip(input, output, m_flipCode);
    } else if (m_transformType == "affine") {
        cv::Point2f srcTri[3];
        cv::Point2f dstTri[3];
        srcTri[0] = cv::Point2f(0, 0);
        srcTri[1] = cv::Point2f(static_cast<float>(input.cols - 1), 0);
        srcTri[2] = cv::Point2f(0, static_cast<float>(input.rows - 1));
        dstTri[0] = cv::Point2f(0, 0);
        dstTri[1] = cv::Point2f(static_cast<float>(input.cols - 1) * static_cast<float>(m_scaleX), 0);
        dstTri[2] = cv::Point2f(0, static_cast<float>(input.rows - 1) * static_cast<float>(m_scaleY));
        cv::Mat warpMat = cv::getAffineTransform(srcTri, dstTri);
        cv::warpAffine(input, output, warpMat, input.size(),
                       cv::INTER_LINEAR, cv::BORDER_CONSTANT,
                       cv::Scalar(0, 0, 0));
    } else if (m_transformType == "perspective") {
        cv::Point2f srcQuad[4];
        cv::Point2f dstQuad[4];
        int w = input.cols, h = input.rows;
        srcQuad[0] = cv::Point2f(0, 0);
        srcQuad[1] = cv::Point2f(static_cast<float>(w), 0);
        srcQuad[2] = cv::Point2f(static_cast<float>(w), static_cast<float>(h));
        srcQuad[3] = cv::Point2f(0, static_cast<float>(h));
        dstQuad[0] = cv::Point2f(0, 0);
        dstQuad[1] = cv::Point2f(static_cast<float>(w) * static_cast<float>(m_scaleX), 0);
        dstQuad[2] = cv::Point2f(static_cast<float>(w) * static_cast<float>(m_scaleX),
                                 static_cast<float>(h) * static_cast<float>(m_scaleY));
        dstQuad[3] = cv::Point2f(0, static_cast<float>(h) * static_cast<float>(m_scaleY));
        cv::Mat perspMat = cv::getPerspectiveTransform(srcQuad, dstQuad);
        cv::warpPerspective(input, output, perspMat, input.size(),
                           cv::INTER_LINEAR, cv::BORDER_CONSTANT,
                           cv::Scalar(0, 0, 0));
    }

    result.overlayImage = output;
    result.ok = !output.empty();
    result.score = result.ok ? 1.0 : 0.0;
    result.data["transformType"] = m_transformType.c_str();
    result.data["outputWidth"] = output.cols;
    result.data["outputHeight"] = output.rows;

    return result.ok;
}

QJsonObject ImageTransformTool::serialize() const {
    QJsonObject obj;
    obj["id"] = m_id;
    obj["type"] = type();
    obj["transformType"] = m_transformType;
    obj["angle"] = m_angle;
    obj["scaleX"] = m_scaleX;
    obj["scaleY"] = m_scaleY;
    obj["flipCode"] = m_flipCode;
    obj["targetWidth"] = m_targetWidth;
    obj["targetHeight"] = m_targetHeight;
    return obj;
}

void ImageTransformTool::deserialize(const QJsonObject& data) {
    m_id = data["id"].toString();
    m_transformType = data["transformType"].toString("resize");
    m_angle = data["angle"].toDouble(0.0);
    m_scaleX = data["scaleX"].toDouble(1.0);
    m_scaleY = data["scaleY"].toDouble(1.0);
    m_flipCode = data["flipCode"].toInt(0);
    m_targetWidth = data["targetWidth"].toInt(100);
    m_targetHeight = data["targetHeight"].toInt(100);
}