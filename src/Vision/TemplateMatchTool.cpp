#include "TemplateMatchTool.h"
#include "Core/Logger.h"
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>

using namespace QDV;

TemplateMatchTool::TemplateMatchTool() {
    m_name = "模板匹配";
}

bool TemplateMatchTool::validateTemplatePath(const QString& path) {
    if (path.isEmpty()) {
        Logger::error("Template path is empty");
        return false;
    }
    
    QFileInfo fileInfo(path);
    if (!fileInfo.exists()) {
        Logger::error("Template file does not exist: " + path);
        return false;
    }
    
    if (!fileInfo.isFile()) {
        Logger::error("Template path is not a file: " + path);
        return false;
    }
    
    QStringList allowedExtensions = {"png", "jpg", "jpeg", "bmp", "tiff", "tif"};
    QString ext = fileInfo.suffix().toLower();
    if (!allowedExtensions.contains(ext)) {
        Logger::error("Unsupported template file format: " + ext);
        return false;
    }
    
    qint64 maxSize = 50 * 1024 * 1024;
    if (fileInfo.size() > maxSize) {
        Logger::error("Template file too large: " + QString::number(fileInfo.size()));
        return false;
    }
    
    return true;
}

bool TemplateMatchTool::loadTemplate() {
    if (!validateTemplatePath(m_templatePath)) {
        m_template.release();
        return false;
    }

    // P 优化：用 QFile 读取字节流 + cv::imdecode 解码，
    // 绕过 cv::imread 对 Windows GBK 中文路径的兼容性问题
    // （FindShapeModelTool 等已统一使用此模式）
    QFile f(m_templatePath);
    if (!f.open(QIODevice::ReadOnly)) {
        Logger::error("TemplateMatchTool: cannot open template: " + m_templatePath);
        m_template.release();
        return false;
    }
    QByteArray bytes = f.readAll();
    f.close();
    if (bytes.isEmpty()) {
        Logger::error("TemplateMatchTool: template file is empty: " + m_templatePath);
        m_template.release();
        return false;
    }
    m_template = cv::imdecode(
        cv::Mat(1, bytes.size(), CV_8UC1, const_cast<char*>(bytes.constData())),
        cv::IMREAD_GRAYSCALE);
    if (m_template.empty()) {
        Logger::error("Failed to load template image: " + m_templatePath);
        return false;
    }
    
    if (m_template.cols < 10 || m_template.rows < 10) {
        Logger::error("Template image too small");
        m_template.release();
        return false;
    }
    
    if (m_template.cols > 5000 || m_template.rows > 5000) {
        Logger::error("Template image too large");
        m_template.release();
        return false;
    }
    
    Logger::info("Template loaded successfully: " + m_templatePath + 
                 QString(" (%1x%2)").arg(m_template.cols).arg(m_template.rows));
    return true;
}

bool TemplateMatchTool::configure(const QJsonObject& params) {
    if (params.contains("template")) {
        m_templatePath = params["template"].toString();
        if (!loadTemplate()) {
            return false;
        }
    }
    
    if (params.contains("threshold")) {
        double threshold = params["threshold"].toDouble();
        if (threshold < 0.0 || threshold > 1.0) {
            Logger::warn("Invalid threshold value, using default");
            threshold = 0.8;
        }
        m_threshold = threshold;
    }
    
    if (params.contains("method")) {
        QString method = params["method"].toString();
        QStringList validMethods = {"SQDIFF", "CCORR", "CCORR_NORMED", 
                                    "SQDIFF_NORMED", "CCOEFF", "CCOEFF_NORMED"};
        if (!validMethods.contains(method)) {
            Logger::warn("Invalid match method, using default");
            method = "SQDIFF";
        }
        m_matchMethod = method;
    }
    
    return true;
}

bool TemplateMatchTool::execute(const cv::Mat& input, ToolResult& result) {
    if (m_template.empty()) {
        Logger::error("Template not loaded");
        result.ok = false;
        return false;
    }
    
    if (input.empty()) {
        Logger::error("Input image is empty");
        result.ok = false;
        return false;
    }
    
    if (input.cols < m_template.cols || input.rows < m_template.rows) {
        Logger::error("Input image smaller than template");
        result.ok = false;
        return false;
    }
    
    cv::Mat grayInput;
    if (input.channels() == 3) {
        cv::cvtColor(input, grayInput, cv::COLOR_BGR2GRAY);
    } else {
        grayInput = input.clone();
    }
    
    int method = cv::TM_SQDIFF;
    if (m_matchMethod == "CCORR") {
        method = cv::TM_CCORR;
    } else if (m_matchMethod == "CCORR_NORMED") {
        method = cv::TM_CCORR_NORMED;
    } else if (m_matchMethod == "SQDIFF_NORMED") {
        method = cv::TM_SQDIFF_NORMED;
    } else if (m_matchMethod == "CCOEFF") {
        method = cv::TM_CCOEFF;
    } else if (m_matchMethod == "CCOEFF_NORMED") {
        method = cv::TM_CCOEFF_NORMED;
    }
    
    cv::Mat resultMat;
    cv::matchTemplate(grayInput, m_template, resultMat, method);
    
    double minVal, maxVal;
    cv::Point minLoc, maxLoc;
    cv::minMaxLoc(resultMat, &minVal, &maxVal, &minLoc, &maxLoc);
    
    cv::Point matchLoc = (method == cv::TM_SQDIFF || method == cv::TM_SQDIFF_NORMED) ? minLoc : maxLoc;
    double score = (method == cv::TM_SQDIFF || method == cv::TM_SQDIFF_NORMED) ? (1.0 - minVal) : maxVal;
    
    result.ok = score >= m_threshold;
    result.score = score;
    
    result.overlayImage = input.clone();
    cv::rectangle(result.overlayImage, matchLoc, 
                  cv::Point(matchLoc.x + m_template.cols, matchLoc.y + m_template.rows), 
                  result.ok ? cv::Scalar(0, 255, 0) : cv::Scalar(0, 0, 255), 2);
    
    result.data["x"] = matchLoc.x;
    result.data["y"] = matchLoc.y;
    result.data["width"] = m_template.cols;
    result.data["height"] = m_template.rows;
    result.data["score"] = score;
    result.data["template"] = m_templatePath;
    
    return true;
}

QJsonObject TemplateMatchTool::serialize() const {
    QJsonObject obj;
    obj["id"] = m_id;
    obj["type"] = type();
    obj["name"] = m_name;
    obj["template"] = m_templatePath;
    obj["threshold"] = m_threshold;
    obj["method"] = m_matchMethod;
    return obj;
}

bool TemplateMatchTool::deserialize(const QJsonObject& data) {
    m_id = data["id"].toString();
    m_name = data["name"].toString();
    m_templatePath = data["template"].toString();
    m_threshold = data["threshold"].toDouble();
    m_matchMethod = data["method"].toString();
    
    if (!m_templatePath.isEmpty()) {
        loadTemplate();
    }
    return true;
}