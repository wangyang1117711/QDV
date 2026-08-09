#include "ContourCompareTool.h"
#include "Core/Logger.h"
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>
#include <vector>

using namespace QDV;

ContourCompareTool::ContourCompareTool() {
    m_name = "轮廓比对";
}

// 加载模板图：使用 QFile 读取字节流后用 cv::imdecode 解码，
// 绕过 cv::imread 对中文路径在 Windows GBK 系统下的失败问题
bool ContourCompareTool::loadTemplate() {
    m_template.release();

    if (m_templatePath.isEmpty()) {
        return false;
    }

    QFileInfo info(m_templatePath);
    if (!info.exists() || !info.isFile()) {
        Logger::error("ContourCompareTool: template file not found: " + m_templatePath);
        return false;
    }

    QFile f(m_templatePath);
    if (!f.open(QIODevice::ReadOnly)) {
        Logger::error("ContourCompareTool: cannot open template: " + m_templatePath);
        return false;
    }
    QByteArray bytes = f.readAll();
    f.close();
    if (bytes.isEmpty()) {
        Logger::error("ContourCompareTool: template file is empty: " + m_templatePath);
        return false;
    }

    cv::Mat raw = cv::imdecode(
        cv::Mat(1, bytes.size(), CV_8UC1, const_cast<char*>(bytes.constData())),
        cv::IMREAD_GRAYSCALE);
    if (raw.empty()) {
        Logger::error("ContourCompareTool: failed to decode template: " + m_templatePath);
        return false;
    }

    m_template = raw;
    Logger::info(QString("ContourCompareTool: template loaded %1 (%2x%3)")
        .arg(info.fileName()).arg(m_template.cols).arg(m_template.rows));
    return true;
}

// 从灰度图提取最大轮廓：Canny 边缘 + findContours，按面积降序取第一个
std::vector<cv::Point> ContourCompareTool::extractLargestContour(const cv::Mat& gray) {
    std::vector<cv::Point> empty;
    if (gray.empty()) return empty;

    // Canny 边缘检测（与 EdgesSubPix 一致的默认阈值）
    cv::Mat edges;
    cv::Canny(gray, edges, 50.0, 150.0, 3);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(edges, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    if (contours.empty()) return empty;

    // 按面积降序取最大轮廓
    std::sort(contours.begin(), contours.end(),
              [](const std::vector<cv::Point>& a, const std::vector<cv::Point>& b) {
                  return cv::contourArea(a) > cv::contourArea(b);
              });
    return contours[0];
}

// 将字符串匹配方法映射为 cv::matchShapes 的方法枚举
int ContourCompareTool::matchMethodFromString(const QString& m) {
    if (m == "I2") return cv::CONTOURS_MATCH_I2;
    if (m == "I3") return cv::CONTOURS_MATCH_I3;
    // 默认 I1（含非法值回退）
    return cv::CONTOURS_MATCH_I1;
}

bool ContourCompareTool::configure(const QJsonObject& params) {
    if (params.contains("templatePath")) {
        QString newPath = params["templatePath"].toString();
        if (newPath != m_templatePath) {
            m_templatePath = newPath;
            loadTemplate();
        }
    }
    if (params.contains("maxDeviation")) {
        const double v = params["maxDeviation"].toDouble();
        // 偏差阈值非负
        m_maxDeviation = (v < 0.0) ? 0.0 : v;
    }
    if (params.contains("matchMethod")) {
        const QString m = params["matchMethod"].toString();
        if (m == "I1" || m == "I2" || m == "I3") {
            m_matchMethod = m;
        } else {
            Logger::warn(QString("ContourCompareTool: matchMethod '%1' 非法，已回退到 'I1'").arg(m));
            m_matchMethod = "I1";
        }
    }
    if (params.contains("minScore")) {
        double s = params["minScore"].toDouble();
        // 钳制到 [0,1]
        s = std::clamp(s, 0.0, 1.0);
        m_minScore = s;
    }

    m_params = params;
    return true;
}

bool ContourCompareTool::execute(const cv::Mat& input, ToolResult& result) {
    try {
        if (input.empty()) {
            result.ok = false;
            result.data["error"] = "Input image is empty";
            return false;
        }

        // 模板加载失败时 ok=false 并设置 error（按任务要求）
        if (m_template.empty()) {
            if (m_templatePath.isEmpty()) {
                result.ok = false;
                result.data["error"] = "Template path is empty";
            } else {
                // 尝试重新加载一次（可能 configure 时路径已设但加载失败）
                if (!loadTemplate()) {
                    result.ok = false;
                    result.data["error"] = "Failed to load template: " + m_templatePath;
                }
            }
            if (!result.ok) {
                result.overlayImage = input.clone();
                return false;
            }
        }

        // 转灰度
        cv::Mat grayInput;
        if (input.channels() == 3) {
            cv::cvtColor(input, grayInput, cv::COLOR_BGR2GRAY);
        } else if (input.channels() == 4) {
            cv::cvtColor(input, grayInput, cv::COLOR_BGRA2GRAY);
        } else {
            grayInput = input.clone();
        }
        if (grayInput.type() != CV_8UC1) {
            grayInput.convertTo(grayInput, CV_8U);
        }

        // 提取实测轮廓与标准轮廓
        std::vector<cv::Point> measuredContour = extractLargestContour(grayInput);
        std::vector<cv::Point> templateContour = extractLargestContour(m_template);

        if (measuredContour.empty()) {
            result.ok = false;
            result.data["error"] = "No contour found in input image";
            result.overlayImage = input.clone();
            return false;
        }
        if (templateContour.empty()) {
            result.ok = false;
            result.data["error"] = "No contour found in template image";
            result.overlayImage = input.clone();
            return false;
        }

        // cv::matchShapes 计算 Hu 矩距离（deviation，越小越相似）
        const int method = matchMethodFromString(m_matchMethod);
        const double deviation = cv::matchShapes(measuredContour, templateContour, method, 0.0);

        // 将距离映射为相似度分数（越大越相似）
        // score = 1/(1+deviation)，deviation=0 时 score=1（完全匹配）
        const double score = 1.0 / (1.0 + std::fabs(deviation));

        // 判定：偏差与分数双条件
        const bool isMatch = (deviation <= m_maxDeviation) && (score >= m_minScore);

        // 输出 typed ports
        result.ports["deviation"] = deviation;
        result.ports["score"] = score;
        result.ports["isMatch"] = isMatch;

        // JSON 输出同步
        result.data["deviation"] = deviation;
        result.data["score"] = score;
        result.data["isMatch"] = isMatch;
        result.data["matchMethod"] = m_matchMethod;
        result.data["maxDeviation"] = m_maxDeviation;
        result.data["minScore"] = m_minScore;
        result.data["templatePath"] = m_templatePath;

        result.score = score;
        result.ok = true; // 算子执行成功（匹配与否由 isMatch 表达）

        // overlayImage：绘制实测轮廓（绿）+ 标准轮廓（红）
        if (input.channels() == 1) {
            cv::cvtColor(input, result.overlayImage, cv::COLOR_GRAY2BGR);
        } else if (input.channels() == 4) {
            cv::cvtColor(input, result.overlayImage, cv::COLOR_BGRA2BGR);
        } else {
            result.overlayImage = input.clone();
        }

        // 实测轮廓：直接在 input overlay 上绘制（绿色）
        std::vector<std::vector<cv::Point>> measuredSet = {measuredContour};
        cv::drawContours(result.overlayImage, measuredSet, -1,
                         cv::Scalar(0, 255, 0), 2, cv::LINE_AA);

        // 标准轮廓：按其原始坐标绘制（红色），仅绘制落在 input 范围内的点
        // 注意：模板坐标系可能与 input 不同，此处仅作可视化对照
        // 截取标准轮廓到 input 范围内绘制（避免越界）
        std::vector<cv::Point> visibleTemplate;
        visibleTemplate.reserve(templateContour.size());
        const cv::Rect imgRect(0, 0, result.overlayImage.cols, result.overlayImage.rows);
        for (const cv::Point& pt : templateContour) {
            if (pt.x >= imgRect.x && pt.x < imgRect.x + imgRect.width &&
                pt.y >= imgRect.y && pt.y < imgRect.y + imgRect.height) {
                visibleTemplate.push_back(pt);
            }
        }
        if (!visibleTemplate.empty()) {
            std::vector<std::vector<cv::Point>> visSet = {visibleTemplate};
            cv::drawContours(result.overlayImage, visSet, -1,
                             cv::Scalar(0, 0, 255), 2, cv::LINE_AA);
        }

        // 结果标签
        const QString label = QString("match=%1 score=%2 dev=%3")
            .arg(isMatch ? "YES" : "NO")
            .arg(score, 0, 'f', 3)
            .arg(deviation, 0, 'f', 3);
        cv::putText(result.overlayImage, label.toStdString(),
                    cv::Point(10, 20),
                    cv::FONT_HERSHEY_SIMPLEX, 0.5,
                    isMatch ? cv::Scalar(0, 255, 0) : cv::Scalar(0, 0, 255),
                    1, cv::LINE_AA);

        return true;
    } catch (const cv::Exception& e) {
        Logger::error(QString("ContourCompareTool: OpenCV 异常: %1").arg(e.what()));
        result.ok = false;
        result.data["error"] = QString("OpenCV exception: %1").arg(e.what());
        return false;
    } catch (const std::exception& e) {
        Logger::error(QString("ContourCompareTool: 异常: %1").arg(e.what()));
        result.ok = false;
        result.data["error"] = QString("Exception: %1").arg(e.what());
        return false;
    }
}

QList<PortDescriptor> ContourCompareTool::outputPorts() const {
    return {
        {"deviation", QStringLiteral("偏差"), PortType::Number, PortDirection::Out,
         QStringLiteral("轮廓偏差（Hu 矩距离，越小越相似）")},
        {"score", QStringLiteral("分数"), PortType::Number, PortDirection::Out,
         QStringLiteral("匹配分数 [0,1]（越大越相似）")},
        {"isMatch", QStringLiteral("是否匹配"), PortType::Bool, PortDirection::Out,
         QStringLiteral("是否匹配（偏差≤阈值 且 分数≥阈值）")},
    };
}

QList<PortDescriptor> ContourCompareTool::inputPorts() const {
    return {
        {"image", QStringLiteral("图像"), PortType::Image, PortDirection::In,
         QStringLiteral("输入图像（提取实测轮廓）")},
    };
}

QJsonObject ContourCompareTool::serialize() const {
    QJsonObject obj = VisionTool::serialize();
    obj["templatePath"] = m_templatePath;
    obj["maxDeviation"] = m_maxDeviation;
    obj["matchMethod"] = m_matchMethod;
    obj["minScore"] = m_minScore;
    return obj;
}

bool ContourCompareTool::deserialize(const QJsonObject& data) {
    if (!VisionTool::deserialize(data)) return false;

    if (data.contains("templatePath")) {
        m_templatePath = data["templatePath"].toString();
        loadTemplate();
    }
    if (data.contains("maxDeviation")) {
        const double v = data["maxDeviation"].toDouble();
        m_maxDeviation = (v < 0.0) ? 0.0 : v;
    }
    if (data.contains("matchMethod")) {
        const QString m = data["matchMethod"].toString();
        if (m == "I1" || m == "I2" || m == "I3") {
            m_matchMethod = m;
        }
    }
    if (data.contains("minScore")) {
        double s = data["minScore"].toDouble();
        m_minScore = std::clamp(s, 0.0, 1.0);
    }
    return true;
}
