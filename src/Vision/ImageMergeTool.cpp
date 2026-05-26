#include "ImageMergeTool.h"

bool ImageMergeTool::configure(const QJsonObject& params) {
    if (!params.contains("mergeType")) {
        return false;
    }

    m_mergeType = params["mergeType"].toString();
    QStringList validTypes = {"horizontal", "vertical", "overlay", "alphaBlend"};
    if (!validTypes.contains(m_mergeType)) {
        return false;
    }

    if (params.contains("alpha")) {
        m_mergeType = "alphaBlend";
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
                cv::addWeighted(input, 0.5, m_storedImage, 0.5, 0.0, output);
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
    result.data["mergeType"] = m_mergeType.c_str();

    return result.ok;
}

QJsonObject ImageMergeTool::serialize() const {
    QJsonObject obj;
    obj["id"] = m_id;
    obj["type"] = type();
    obj["mergeType"] = m_mergeType;
    return obj;
}

void ImageMergeTool::deserialize(const QJsonObject& data) {
    m_id = data["id"].toString();
    m_mergeType = data["mergeType"].toString("horizontal");
}