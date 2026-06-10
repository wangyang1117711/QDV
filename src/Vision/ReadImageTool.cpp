#include "Vision/ReadImageTool.h"
#include "Core/Logger.h"
#include <QFileInfo>
#include <QJsonArray>
#include <opencv2/imgcodecs.hpp>

using namespace QDV;

ReadImageTool::ReadImageTool() {
    m_name = "读图";
}

ReadImageTool::~ReadImageTool() = default;

bool ReadImageTool::configure(const QJsonObject& params) {
    if (params.contains("filePath")) {
        setFilePath(params["filePath"].toString());
    }
    if (params.contains("colorMode")) {
        setColorMode(params["colorMode"].toInt(2));
    }
    m_params = params;
    return true;
}

bool ReadImageTool::execute(const cv::Mat& input, ToolResult& result) {
    Q_UNUSED(input);

    if (m_filePath.isEmpty()) {
        Logger::warn("ReadImageTool: no file path configured");
        result.ok = false;
        result.data["error"] = "No file path configured";
        return false;
    }

    QFileInfo fileInfo(m_filePath);
    if (!fileInfo.exists()) {
        Logger::error("ReadImageTool: file not found: " + m_filePath);
        result.ok = false;
        result.data["error"] = QString("File not found: %1").arg(m_filePath);
        return false;
    }

    if (!fileInfo.isReadable()) {
        Logger::error("ReadImageTool: file not readable: " + m_filePath);
        result.ok = false;
        result.data["error"] = QString("File not readable: %1").arg(m_filePath);
        return false;
    }

    QString suffix = fileInfo.suffix().toLower();
    QStringList supportedFormats = {"png", "jpg", "jpeg", "bmp", "tiff", "tif", "webp", "pbm", "pgm", "ppm", "sr", "ras"};
    if (!supportedFormats.contains(suffix) && !suffix.isEmpty()) {
        Logger::warn("ReadImageTool: potentially unsupported format: " + suffix +
                     ", attempting to read anyway");
    }

    int flags = cv::IMREAD_COLOR;
    switch (m_colorMode) {
    case 0: flags = cv::IMREAD_UNCHANGED; break;
    case 1: flags = cv::IMREAD_GRAYSCALE; break;
    case 2: flags = cv::IMREAD_COLOR; break;
    default:
        if (m_colorMode < 0) flags = cv::IMREAD_GRAYSCALE;
        else flags = cv::IMREAD_COLOR;
        break;
    }

    cv::Mat img = cv::imread(m_filePath.toStdString(), flags);

    if (img.empty()) {
        Logger::error("ReadImageTool: failed to decode image: " + m_filePath);
        result.ok = false;
        result.data["error"] = QString("Failed to decode image: %1").arg(m_filePath);
        result.data["reason"] = "The file may be corrupted or in an unsupported format";
        return false;
    }

    result.ok = true;
    result.score = 1.0;
    result.overlayImage = img.clone();

    result.data["filePath"] = m_filePath;
    result.data["fileName"] = fileInfo.fileName();
    result.data["fileSize"] = fileInfo.size();
    result.data["width"] = img.cols;
    result.data["height"] = img.rows;
    result.data["channels"] = img.channels();
    result.data["depth"] = img.depth();
    result.data["colorMode"] = m_colorMode;

    QString colorModeStr;
    switch (m_colorMode) {
    case 0: colorModeStr = "unchanged"; break;
    case 1: colorModeStr = "grayscale"; break;
    default: colorModeStr = "color"; break;
    }
    result.data["colorModeDesc"] = colorModeStr;

    m_results["lastFilePath"] = m_filePath;
    m_results["lastWidth"] = img.cols;
    m_results["lastHeight"] = img.rows;
    m_results["lastChannels"] = img.channels();

    Logger::info(QString("ReadImageTool: loaded %1 (%2x%3, %4 channels)")
        .arg(fileInfo.fileName())
        .arg(img.cols)
        .arg(img.rows)
        .arg(img.channels()));

    return true;
}

QJsonObject ReadImageTool::serialize() const {
    QJsonObject obj = VisionTool::serialize();
    obj["filePath"] = m_filePath;
    obj["colorMode"] = m_colorMode;
    return obj;
}

bool ReadImageTool::deserialize(const QJsonObject& data) {
    if (!VisionTool::deserialize(data)) return false;

    if (data.contains("filePath")) m_filePath = data["filePath"].toString();
    if (data.contains("colorMode")) m_colorMode = data["colorMode"].toInt(2);

    return configure(data);
}