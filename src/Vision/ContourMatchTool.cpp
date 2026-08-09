#include "Vision/ContourMatchTool.h"
#include "Core/Logger.h"
#include <QJsonArray>
#include <QVariantList>
#include <QVariantMap>
#include <opencv2/imgproc.hpp>
#include <algorithm>

using namespace QDV;

ContourMatchTool::ContourMatchTool() {
    m_name = "轮廓匹配";
}

// 加载模板图：QFile 读取字节流 + cv::imdecode 解码，绕过 cv::imread 中文路径问题
// 解码后转灰度、OTSU 二值化、findContours 取面积最大者作为模板轮廓
bool ContourMatchTool::loadTemplate() {
    m_template.release();
    m_templateContour.clear();

    if (m_templatePath.isEmpty()) {
        return false;
    }

    QFileInfo info(m_templatePath);
    if (!info.exists() || !info.isFile()) {
        Logger::error("ContourMatchTool: template file not found: " + m_templatePath);
        return false;
    }

    QFile f(m_templatePath);
    if (!f.open(QIODevice::ReadOnly)) {
        Logger::error("ContourMatchTool: cannot open template: " + m_templatePath);
        return false;
    }
    QByteArray bytes = f.readAll();
    f.close();
    if (bytes.isEmpty()) {
        Logger::error("ContourMatchTool: template file is empty: " + m_templatePath);
        return false;
    }

    // 解码为灰度图（轮廓匹配只需单通道）
    cv::Mat raw = cv::imdecode(
        cv::Mat(1, bytes.size(), CV_8UC1, const_cast<char*>(bytes.constData())),
        cv::IMREAD_GRAYSCALE);
    if (raw.empty()) {
        Logger::error("ContourMatchTool: failed to decode template: " + m_templatePath);
        return false;
    }

    m_template = raw;

    // 二值化 + 提取轮廓，取面积最大者作为模板轮廓
    cv::Mat bin;
    cv::threshold(m_template, bin, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(bin, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    if (contours.empty()) {
        Logger::error("ContourMatchTool: no contour found in template: " + m_templatePath);
        m_template.release();
        return false;
    }

    // 选面积最大的轮廓作为模板（最稳定的形状特征）
    double maxArea = 0.0;
    size_t maxIdx = 0;
    for (size_t i = 0; i < contours.size(); ++i) {
        double a = cv::contourArea(contours[i]);
        if (a > maxArea) {
            maxArea = a;
            maxIdx = i;
        }
    }
    m_templateContour = contours[maxIdx];

    Logger::info(QString("ContourMatchTool: template loaded %1 (%2x%3, contourArea=%4)")
        .arg(info.fileName()).arg(m_template.cols).arg(m_template.rows).arg(maxArea, 0, 'f', 1));
    return true;
}

// 将方法字符串映射为 OpenCV 轮廓匹配方法枚举
int ContourMatchTool::matchMethodCode() const {
    if (m_matchMethod == "I2") return cv::CONTOURS_MATCH_I2;
    if (m_matchMethod == "I3") return cv::CONTOURS_MATCH_I3;
    return cv::CONTOURS_MATCH_I1;  // 默认 I1
}

bool ContourMatchTool::configure(const QJsonObject& params) {
    if (params.contains("templatePath")) {
        QString newPath = params["templatePath"].toString();
        if (newPath != m_templatePath) {
            m_templatePath = newPath;
            loadTemplate();
        }
    }

    if (params.contains("threshold")) {
        double t = params["threshold"].toDouble();
        if (t < 0.0) t = 0.0;
        if (t > 1.0) t = 1.0;
        m_threshold = t;
    }

    if (params.contains("maxMatches")) {
        int m = params["maxMatches"].toInt();
        if (m < 1) m = 1;
        if (m > 1000) m = 1000;
        m_maxMatches = m;
    }

    if (params.contains("matchMethod")) {
        QString m = params["matchMethod"].toString();
        if (m == "I1" || m == "I2" || m == "I3") {
            m_matchMethod = m;
        } else {
            Logger::warn("ContourMatchTool: invalid matchMethod, using default I1");
            m_matchMethod = "I1";
        }
    }

    if (params.contains("minArea")) {
        double a = params["minArea"].toDouble();
        if (a < 0.0) a = 0.0;
        m_minArea = a;
    }

    m_params = params;
    return true;
}

bool ContourMatchTool::execute(const cv::Mat& input, ToolResult& result) {
    try {
        // 输入校验
        if (input.empty()) {
            Logger::error("ContourMatchTool: input image is empty");
            result.ok = false;
            result.data["error"] = "Input image is empty";
            return false;
        }

        // 模板轮廓必须已加载
        if (m_templateContour.empty()) {
            Logger::error("ContourMatchTool: template contour not loaded");
            result.ok = false;
            result.data["error"] = "Template contour not loaded";
            return false;
        }

        // 输入转灰度
        cv::Mat grayInput;
        if (input.channels() == 1) {
            grayInput = input.clone();
        } else if (input.channels() == 3) {
            cv::cvtColor(input, grayInput, cv::COLOR_BGR2GRAY);
        } else if (input.channels() == 4) {
            cv::cvtColor(input, grayInput, cv::COLOR_BGRA2GRAY);
        } else {
            grayInput = input.clone();
        }

        // OTSU 二值化 + 提取所有外轮廓
        cv::Mat bin;
        cv::threshold(grayInput, bin, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);
        std::vector<std::vector<cv::Point>> contours;
        cv::findContours(bin, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

        // 候选匹配：对每个面积 >= minArea 的轮廓计算与模板轮廓的 matchShapes 距离
        struct Cand {
            cv::Rect rect;       // 外接矩形
            double   score;      // 转换后的相似分数 [0,1]
            double   distance;   // 原始 matchShapes 距离
            double   area;       // 轮廓面积
        };
        std::vector<Cand> cands;
        int method = matchMethodCode();

        for (const auto& c : contours) {
            double area = cv::contourArea(c);
            if (area < m_minArea) continue;  // 面积过滤

            // matchShapes：距离越小越相似，I1/I2/I3 距离均 >= 0
            double dist = cv::matchShapes(m_templateContour, c, method, 0.0);
            // 距离转分数：score = 1 / (1 + distance)，保证分数落在 [0,1]
            double score = 1.0 / (1.0 + dist);
            cands.push_back({cv::boundingRect(c), score, dist, area});
        }

        // 按分数降序排列
        std::sort(cands.begin(), cands.end(),
            [](const Cand& a, const Cand& b) { return a.score > b.score; });

        // 取分数 >= threshold 的前 maxMatches 个
        QVariantList matchesList;
        QJsonArray matchesArr;
        double bestScore = 0.0;
        int matchCount = 0;
        for (const auto& c : cands) {
            if (c.score < m_threshold) break;          // 已降序，后续只会更小
            if (matchCount >= m_maxMatches) break;
            if (c.score > bestScore) bestScore = c.score;

            // typed ports：QVariantMap 形式
            QVariantMap vm;
            vm["x"] = c.rect.x;
            vm["y"] = c.rect.y;
            vm["score"] = c.score;
            vm["area"] = c.area;
            matchesList.append(vm);

            // 同步 data（QJsonObject 形式）
            QJsonObject m;
            m["x"] = c.rect.x;
            m["y"] = c.rect.y;
            m["width"] = c.rect.width;
            m["height"] = c.rect.height;
            m["score"] = c.score;
            m["area"] = c.area;
            m["distance"] = c.distance;
            matchesArr.append(m);

            ++matchCount;
        }

        // 写入 typed ports
        result.ports["matches"] = matchesList;
        result.ports["matchCount"] = matchCount;
        result.ports["bestScore"] = bestScore;

        // 同步到 data（便于旧消费者读取）
        result.data["matches"] = matchesArr;
        result.data["matchCount"] = matchCount;
        result.data["bestScore"] = bestScore;
        result.data["threshold"] = m_threshold;
        result.data["matchMethod"] = m_matchMethod;
        result.data["templatePath"] = m_templatePath;

        result.ok = true;
        result.score = bestScore;

        // 准备 overlayImage（BGR）
        cv::Mat overlay;
        if (input.channels() == 3) {
            overlay = input.clone();
        } else if (input.channels() == 1) {
            cv::cvtColor(input, overlay, cv::COLOR_GRAY2BGR);
        } else if (input.channels() == 4) {
            cv::cvtColor(input, overlay, cv::COLOR_BGRA2BGR);
        } else {
            overlay = input.clone();
        }

        // 绘制匹配轮廓：绿色矩形框 + 分数文字
        for (int i = 0; i < matchCount; ++i) {
            const auto& c = cands[i];
            cv::Rect box = c.rect & cv::Rect(0, 0, overlay.cols, overlay.rows);
            cv::rectangle(overlay, box.tl(), box.br(), cv::Scalar(0, 255, 0), 2);

            QString label = QString("%1").arg(c.score, 0, 'f', 3);
            cv::putText(overlay, label.toStdString(),
                        cv::Point(box.x, std::max(box.y - 5, 12)),
                        cv::FONT_HERSHEY_SIMPLEX, 0.5,
                        cv::Scalar(0, 255, 0), 1, cv::LINE_AA);
        }

        result.overlayImage = overlay;

        m_results["lastMatchCount"] = matchCount;
        m_results["lastBestScore"] = bestScore;

        Logger::info(QString("ContourMatchTool: matched %1 (best=%2)")
            .arg(matchCount).arg(bestScore, 0, 'f', 3));

        return true;
    } catch (const cv::Exception& e) {
        Logger::error(QString("ContourMatchTool: OpenCV exception: %1").arg(e.what()));
        result.ok = false;
        result.data["error"] = QString("OpenCV exception: %1").arg(e.what());
        return false;
    } catch (const std::exception& e) {
        Logger::error(QString("ContourMatchTool: std exception: %1").arg(e.what()));
        result.ok = false;
        result.data["error"] = QString("std exception: %1").arg(e.what());
        return false;
    }
}

QJsonObject ContourMatchTool::serialize() const {
    QJsonObject obj = VisionTool::serialize();
    obj["templatePath"] = m_templatePath;
    obj["threshold"] = m_threshold;
    obj["maxMatches"] = m_maxMatches;
    obj["matchMethod"] = m_matchMethod;
    obj["minArea"] = m_minArea;
    return obj;
}

bool ContourMatchTool::deserialize(const QJsonObject& data) {
    if (!VisionTool::deserialize(data)) return false;

    if (data.contains("templatePath")) {
        m_templatePath = data["templatePath"].toString();
        loadTemplate();
    }
    if (data.contains("threshold")) {
        double t = data["threshold"].toDouble();
        if (t < 0.0) t = 0.0;
        if (t > 1.0) t = 1.0;
        m_threshold = t;
    }
    if (data.contains("maxMatches")) {
        int m = data["maxMatches"].toInt();
        if (m < 1) m = 1;
        if (m > 1000) m = 1000;
        m_maxMatches = m;
    }
    if (data.contains("matchMethod")) {
        QString m = data["matchMethod"].toString();
        if (m == "I1" || m == "I2" || m == "I3") {
            m_matchMethod = m;
        }
    }
    if (data.contains("minArea")) {
        double a = data["minArea"].toDouble();
        if (a < 0.0) a = 0.0;
        m_minArea = a;
    }
    return true;
}

// 声明输出端口：匹配结果（点集）、匹配数（数值）、最佳分数（数值）
QList<PortDescriptor> ContourMatchTool::outputPorts() const {
    return {
        {QStringLiteral("matches"),    QStringLiteral("匹配结果"), PortType::Points, PortDirection::Out, QStringLiteral("匹配到的轮廓列表")},
        {QStringLiteral("matchCount"), QStringLiteral("匹配数"),   PortType::Number, PortDirection::Out, QStringLiteral("匹配到的轮廓数量")},
        {QStringLiteral("bestScore"),  QStringLiteral("最佳分数"), PortType::Number, PortDirection::Out, QStringLiteral("最高匹配分数")},
    };
}

// 声明输入端口：图像
QList<PortDescriptor> ContourMatchTool::inputPorts() const {
    return {
        {QStringLiteral("image"), QStringLiteral("图像"), PortType::Image, PortDirection::In, QStringLiteral("待匹配的输入图像")},
    };
}
