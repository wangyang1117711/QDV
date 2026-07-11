#include "ImageMergeTool.h"
#include <opencv2/imgproc.hpp>

using namespace QDV;

bool ImageMergeTool::configure(const QJsonObject& params) {
    if (!params.contains("mergeType")) {
        return false;
    }

    m_mergeType = params["mergeType"].toString();
    QStringList validTypes = {"horizontal", "vertical", "overlay", "alphaBlend"};
    if (!validTypes.contains(m_mergeType)) {
        return false;
    }

    // P1-A13 修复：存储 alpha 参数
    // P1-B5 修复：之前只要 params 包含 alpha 就强制 m_mergeType="alphaBlend"
    // 但 OperatorDescriptors 总会带 alpha 默认值 0.5，导致用户选 horizontal/vertical/overlay 时被静默改为 alphaBlend
    // 现在仅在 m_mergeType=="alphaBlend" 时读取 alpha，不强制覆盖 mergeType
    if (m_mergeType == "alphaBlend" && params.contains("alpha")) {
        m_alpha = params["alpha"].toDouble(0.5);
        // 限制范围 [0.0, 1.0]
        m_alpha = std::max(0.0, std::min(1.0, m_alpha));
    }

    return true;
}

bool ImageMergeTool::execute(const cv::Mat& input, ToolResult& result) {
    result.elapsedMs = 0;
    cv::Mat output;

    if (!m_storedImage.empty()) {
        if (m_mergeType == "horizontal") {
            int maxRows = std::max(input.rows, m_storedImage.rows);
            int totalCols = input.cols + m_storedImage.cols;
            output = cv::Mat(maxRows, totalCols, input.type(), cv::Scalar(0));

            cv::Mat roi1 = output(cv::Rect(0, 0, input.cols, input.rows));
            input.copyTo(roi1);
            cv::Mat roi2 = output(cv::Rect(input.cols, 0, m_storedImage.cols, m_storedImage.rows));
            m_storedImage.copyTo(roi2);
        } else if (m_mergeType == "vertical") {
            int totalRows = input.rows + m_storedImage.rows;
            int maxCols = std::max(input.cols, m_storedImage.cols);
            output = cv::Mat(totalRows, maxCols, input.type(), cv::Scalar(0));

            cv::Mat roi1 = output(cv::Rect(0, 0, input.cols, input.rows));
            input.copyTo(roi1);
            cv::Mat roi2 = output(cv::Rect(0, input.rows, m_storedImage.cols, m_storedImage.rows));
            m_storedImage.copyTo(roi2);
        } else if (m_mergeType == "overlay") {
            output = input.clone();
            cv::Rect roi(0, 0, std::min(m_storedImage.cols, output.cols),
                         std::min(m_storedImage.rows, output.rows));
            m_storedImage(roi).copyTo(output(roi));
        } else if (m_mergeType == "alphaBlend") {
            if (m_storedImage.size() == input.size()) {
                // P1-A13 修复：使用用户设置的 alpha（之前硬编码 0.5）
                cv::addWeighted(input, m_alpha, m_storedImage, 1.0 - m_alpha, 0.0, output);
            } else {
                output = input.clone();
            }
        }
    } else {
        output = input.clone();
    }

    m_storedImage = input.clone();
    result.overlayImage = output;
    result.ok = !output.empty();
    result.score = result.ok ? 1.0 : 0.0;
    result.data["mergeType"] = m_mergeType;

    return result.ok;
}

QJsonObject ImageMergeTool::serialize() const {
    QJsonObject obj;
    obj["id"] = m_id;
    obj["type"] = type();
    obj["mergeType"] = m_mergeType;
    // P1-A13 修复：序列化 alpha 字段
    if (m_mergeType == "alphaBlend") {
        obj["alpha"] = m_alpha;
    }
    return obj;
}

bool ImageMergeTool::deserialize(const QJsonObject& data) {
    m_id = data["id"].toString();
    m_mergeType = data["mergeType"].toString("horizontal");
    // P1-A13 修复：反序列化 alpha 字段
    m_alpha = data["alpha"].toDouble(0.5);
    m_alpha = std::max(0.0, std::min(1.0, m_alpha));
    return true;
}