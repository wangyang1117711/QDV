#include "Vision/ReadImageTool.h"
#include "Core/Logger.h"
#include <QFileInfo>
#include <QJsonArray>
#include <QFile>
#include <QByteArray>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>    // v5.3：cvtColor/split 用于通道分离

using namespace QDV;

// v5.3：通道分离与色彩空间转换辅助函数
// 输入 img 为原图（可能是 1 通道或 3 通道），mode 指定输出模式
// 返回处理后的图像（单通道为灰度图，色彩空间转换为 3 通道）
static cv::Mat applyChannelModeImpl(const cv::Mat& img, const QString& mode) {
    if (img.empty()) return cv::Mat();

    // 灰度模式：直接返回灰度图
    if (mode == "Gray") {
        if (img.channels() == 1) return img.clone();
        cv::Mat gray;
        cv::cvtColor(img, gray, cv::COLOR_BGR2GRAY);
        return gray;
    }

    // 单通道提取：需要 3 通道输入
    if (img.channels() != 3) {
        return cv::Mat();  // 无法提取通道，返回空矩阵
    }

    std::vector<cv::Mat> channels;

    // RGB 通道分离（OpenCV 默认 BGR 顺序）
    if (mode == "R" || mode == "G" || mode == "B") {
        cv::split(img, channels);  // channels[0]=B, [1]=G, [2]=R
        if (mode == "R") return channels[2].clone();
        if (mode == "G") return channels[1].clone();
        if (mode == "B") return channels[0].clone();
    }

    // HSV 通道分离
    if (mode == "H" || mode == "S" || mode == "V") {
        cv::Mat hsv;
        cv::cvtColor(img, hsv, cv::COLOR_BGR2HSV);
        cv::split(hsv, channels);  // [0]=H, [1]=S, [2]=V
        if (mode == "H") return channels[0].clone();
        if (mode == "S") return channels[1].clone();
        if (mode == "V") return channels[2].clone();
    }

    // Lab 通道分离
    if (mode == "L" || mode == "a" || mode == "b") {
        cv::Mat lab;
        cv::cvtColor(img, lab, cv::COLOR_BGR2Lab);
        cv::split(lab, channels);  // [0]=L, [1]=a, [2]=b
        if (mode == "L") return channels[0].clone();
        if (mode == "a") return channels[1].clone();
        if (mode == "b") return channels[2].clone();
    }

    // 色彩空间转换（输出 3 通道）
    if (mode == "BGR_RGB") {
        cv::Mat rgb;
        cv::cvtColor(img, rgb, cv::COLOR_BGR2RGB);
        return rgb;
    }
    if (mode == "RGB_HSV") {
        cv::Mat hsv;
        cv::cvtColor(img, hsv, cv::COLOR_BGR2HSV);
        return hsv;
    }
    if (mode == "RGB_Lab") {
        cv::Mat lab;
        cv::cvtColor(img, lab, cv::COLOR_BGR2Lab);
        return lab;
    }
    if (mode == "RGB_YUV") {
        cv::Mat yuv;
        cv::cvtColor(img, yuv, cv::COLOR_BGR2YUV);
        return yuv;
    }

    return cv::Mat();  // 未知模式
}

ReadImageTool::ReadImageTool() {
    m_name = "读图";
}

ReadImageTool::~ReadImageTool() = default;

bool ReadImageTool::configure(const QJsonObject& params) {
    if (params.contains("filePath")) {
        setFilePath(params["filePath"].toString());
    }
    if (params.contains("colorMode")) {
        // 兼容三种格式：int(0/1/2) / 字符串 key("unchanged"/"grayscale"/"color") / 字符串数字("2")
        const QJsonValue& v = params["colorMode"];
        int mode = 2;  // 默认彩色
        if (v.isString()) {
            const QString s = v.toString();
            if (s == "unchanged") mode = 0;
            else if (s == "grayscale") mode = 1;
            else if (s == "color") mode = 2;
            else {
                bool ok = false;
                int parsed = s.toInt(&ok);
                if (ok && parsed >= 0 && parsed <= 2) mode = parsed;
            }
        } else if (v.isDouble()) {
            int parsed = v.toInt(2);
            if (parsed >= 0 && parsed <= 2) mode = parsed;
        }
        setColorMode(mode);
    }
    // v5.3：通道分离模式
    if (params.contains("channelMode")) {
        setChannelMode(params["channelMode"].toString());
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

    // v2.5.0 修复：改用 QFile + cv::imdecode 替代 cv::imread，兼容中文/特殊字符路径
    // cv::imread 内部用 C 标准 fopen，在 Windows GBK 系统下对 UTF-8 中文路径会失败
    QFile imgFile(m_filePath);
    if (!imgFile.open(QIODevice::ReadOnly)) {
        Logger::error("ReadImageTool: cannot open file: " + m_filePath);
        result.ok = false;
        result.data["error"] = QString("Cannot open file: %1").arg(m_filePath);
        return false;
    }
    QByteArray imgData = imgFile.readAll();
    imgFile.close();
    if (imgData.isEmpty()) {
        Logger::error("ReadImageTool: empty file: " + m_filePath);
        result.ok = false;
        result.data["error"] = QString("Empty file: %1").arg(m_filePath);
        return false;
    }
    // cv::imdecode 从内存字节流解码，绕过路径编码问题
    cv::Mat img = cv::imdecode(cv::Mat(1, imgData.size(), CV_8UC1,
                                       const_cast<char*>(imgData.constData())), flags);

    if (img.empty()) {
        Logger::error("ReadImageTool: failed to decode image: " + m_filePath);
        result.ok = false;
        result.data["error"] = QString("Failed to decode image: %1").arg(m_filePath);
        result.data["reason"] = "The file may be corrupted or in an unsupported format";
        return false;
    }

    // v5.3：通道分离与色彩空间转换
    // channelMode 为 none 时保持原图输出；否则按模式提取单通道或转换色彩空间
    cv::Mat processed = img;
    if (m_channelMode != "none" && m_channelMode != "") {
        processed = applyChannelModeImpl(img, m_channelMode);
        if (processed.empty()) {
            // 通道分离失败（如图像不是 3 通道），回退到原图
            Logger::warn("ReadImageTool: channelMode '" + m_channelMode +
                         "' not applicable, falling back to original image");
            processed = img;
        }
    }

    result.ok = true;
    result.score = 1.0;
    result.overlayImage = processed.clone();

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
    obj["channelMode"] = m_channelMode;  // v5.3
    return obj;
}

bool ReadImageTool::deserialize(const QJsonObject& data) {
    if (!VisionTool::deserialize(data)) return false;

    if (data.contains("filePath")) m_filePath = data["filePath"].toString();
    if (data.contains("colorMode")) m_colorMode = data["colorMode"].toInt(2);
    if (data.contains("channelMode")) m_channelMode = data["channelMode"].toString();  // v5.3

    return configure(data);
}