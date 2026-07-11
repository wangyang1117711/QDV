#include "FindNccModelTool.h"
#include "Core/Logger.h"
#include <QJsonArray>
#include <opencv2/imgproc.hpp>

using namespace QDV;

FindNccModelTool::FindNccModelTool() {
    m_name = "NCC模板匹配";
}

// 加载模板图: 使用 QFile 读取字节流后用 cv::imdecode 解码,
// 绕过 cv::imread 对中文路径在 Windows GBK 系统下的失败问题
bool FindNccModelTool::loadTemplate() {
    m_template.release();

    if (m_templatePath.isEmpty()) {
        return false;
    }

    QFileInfo info(m_templatePath);
    if (!info.exists() || !info.isFile()) {
        Logger::error("FindNccModelTool: template file not found: " + m_templatePath);
        return false;
    }

    QFile f(m_templatePath);
    if (!f.open(QIODevice::ReadOnly)) {
        Logger::error("FindNccModelTool: cannot open template: " + m_templatePath);
        return false;
    }
    QByteArray bytes = f.readAll();
    f.close();
    if (bytes.isEmpty()) {
        Logger::error("FindNccModelTool: template file is empty: " + m_templatePath);
        return false;
    }

    // 从内存解码,统一转灰度便于后续 NCC 匹配
    cv::Mat raw = cv::imdecode(
        cv::Mat(1, bytes.size(), CV_8UC1, const_cast<char*>(bytes.constData())),
        cv::IMREAD_GRAYSCALE);
    if (raw.empty()) {
        Logger::error("FindNccModelTool: failed to decode template: " + m_templatePath);
        return false;
    }

    m_template = raw;
    Logger::info(QString("FindNccModelTool: template loaded %1 (%2x%3)")
        .arg(info.fileName()).arg(m_template.cols).arg(m_template.rows));
    return true;
}

// NMS: 在 scoreMap 中按得分降序挑选候选,丢弃与已选框重叠度过高的候选
std::vector<cv::Rect> FindNccModelTool::nonMaxSuppression(
    const cv::Mat& scoreMap,
    double threshold,
    int maxMatches,
    int templateW,
    int templateH,
    std::vector<double>& outScores) const {

    std::vector<cv::Rect> result;
    outScores.clear();
    if (scoreMap.empty()) return result;

    // 收集所有得分超过阈值的候选点
    std::vector<std::pair<cv::Point, double>> candidates;
    for (int y = 0; y < scoreMap.rows; ++y) {
        const float* row = scoreMap.ptr<float>(y);
        for (int x = 0; x < scoreMap.cols; ++x) {
            if (row[x] >= static_cast<float>(threshold)) {
                candidates.emplace_back(cv::Point(x, y), static_cast<double>(row[x]));
            }
        }
    }
    if (candidates.empty()) return result;

    // 按得分降序排列
    std::sort(candidates.begin(), candidates.end(),
        [](const std::pair<cv::Point, double>& a,
           const std::pair<cv::Point, double>& b) {
            return a.second > b.second;
        });

    // IoU 阈值: 超过该值视为重叠,丢弃
    const double iouThreshold = 0.3;
    const double areaTpl = static_cast<double>(templateW) * templateH;

    for (const auto& c : candidates) {
        if (static_cast<int>(result.size()) >= maxMatches) break;

        cv::Rect box(c.first.x, c.first.y, templateW, templateH);
        bool keep = true;
        for (const auto& kept : result) {
            // 计算 IoU
            int x1 = std::max(box.x, kept.x);
            int y1 = std::max(box.y, kept.y);
            int x2 = std::min(box.x + box.width, kept.x + kept.width);
            int y2 = std::min(box.y + box.height, kept.y + kept.height);
            int iw = std::max(0, x2 - x1);
            int ih = std::max(0, y2 - y1);
            double inter = static_cast<double>(iw) * ih;
            double uni = areaTpl + static_cast<double>(kept.width) * kept.height - inter;
            if (uni > 0.0 && (inter / uni) > iouThreshold) {
                keep = false;
                break;
            }
        }
        if (keep) {
            result.push_back(box);
            outScores.push_back(c.second);
        }
    }
    return result;
}

bool FindNccModelTool::configure(const QJsonObject& params) {
    if (params.contains("templatePath")) {
        QString newPath = params["templatePath"].toString();
        if (newPath != m_templatePath) {
            m_templatePath = newPath;
            loadTemplate();
        }
    }

    if (params.contains("threshold")) {
        double t = params["threshold"].toDouble();
        // 钳制到 [0,1]
        if (t < 0.0) t = 0.0;
        if (t > 1.0) t = 1.0;
        m_threshold = t;
    }

    if (params.contains("maxMatches")) {
        int m = params["maxMatches"].toInt();
        // 钳制到 [1,1000]
        if (m < 1) m = 1;
        if (m > 1000) m = 1000;
        m_maxMatches = m;
    }

    m_params = params;
    return true;
}

bool FindNccModelTool::execute(const cv::Mat& input, ToolResult& result) {
    // 输入检查
    if (input.empty()) {
        Logger::error("FindNccModelTool: input image is empty");
        result.ok = false;
        result.data["error"] = "Input image is empty";
        return false;
    }

    if (m_template.empty()) {
        Logger::error("FindNccModelTool: template not loaded");
        result.ok = false;
        result.data["error"] = "Template not loaded";
        return false;
    }

    if (input.cols < m_template.cols || input.rows < m_template.rows) {
        Logger::error("FindNccModelTool: input smaller than template");
        result.ok = false;
        result.data["error"] = "Input image is smaller than template";
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

    // NCC 匹配: TM_CCOEFF_NORMED 输出 [-1,1] 的归一化得分
    cv::Mat scoreMap;
    cv::matchTemplate(grayInput, m_template, scoreMap, cv::TM_CCOEFF_NORMED);

    // NMS 多目标筛选
    std::vector<double> scores;
    std::vector<cv::Rect> boxes = nonMaxSuppression(
        scoreMap, m_threshold, m_maxMatches,
        m_template.cols, m_template.rows, scores);

    // 准备 overlayImage(BGR)
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

    // 绿色矩形框 + 得分文字
    QJsonArray matchesArr;
    double bestScore = 0.0;
    for (size_t i = 0; i < boxes.size(); ++i) {
        const cv::Rect& r = boxes[i];
        double s = scores[i];
        if (s > bestScore) bestScore = s;

        cv::rectangle(overlay, r.tl(), r.br(), cv::Scalar(0, 255, 0), 2);

        // 得分标签: 放在矩形左上角上方,避免遮挡目标
        QString label = QString::number(s, 'f', 3);
        cv::putText(overlay, label.toStdString(),
                    cv::Point(r.x, std::max(r.y - 5, 12)),
                    cv::FONT_HERSHEY_SIMPLEX, 0.5,
                    cv::Scalar(0, 255, 0), 1, cv::LINE_AA);

        QJsonObject m;
        m["x"] = r.x;
        m["y"] = r.y;
        m["width"] = r.width;
        m["height"] = r.height;
        m["score"] = s;
        matchesArr.append(m);
    }

    result.ok = !boxes.empty();
    result.score = bestScore;
    result.overlayImage = overlay;

    result.data["count"] = static_cast<int>(boxes.size());
    result.data["matches"] = matchesArr;
    result.data["threshold"] = m_threshold;
    result.data["templatePath"] = m_templatePath;

    m_results["lastMatchCount"] = static_cast<int>(boxes.size());
    m_results["lastBestScore"] = bestScore;

    Logger::info(QString("FindNccModelTool: matched %1 (best=%2)")
        .arg(boxes.size()).arg(bestScore, 0, 'f', 3));

    return true;
}

QJsonObject FindNccModelTool::serialize() const {
    QJsonObject obj = VisionTool::serialize();
    obj["templatePath"] = m_templatePath;
    obj["threshold"] = m_threshold;
    obj["maxMatches"] = m_maxMatches;
    return obj;
}

bool FindNccModelTool::deserialize(const QJsonObject& data) {
    if (!VisionTool::deserialize(data)) return false;

    // 使用 contains 检查,避免缺失字段被覆盖为默认值
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
    return true;
}
