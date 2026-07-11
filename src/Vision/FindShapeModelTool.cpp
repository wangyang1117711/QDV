#include "FindShapeModelTool.h"
#include "Core/Logger.h"
#include <QJsonArray>
#include <opencv2/imgproc.hpp>
#include <cmath>

using namespace QDV;

FindShapeModelTool::FindShapeModelTool() {
    m_name = "形状匹配";
}

// 加载模板图: 使用 QFile 读取字节流后用 cv::imdecode 解码,
// 绕过 cv::imread 对中文路径在 Windows GBK 系统下的失败问题
bool FindShapeModelTool::loadTemplate() {
    m_template.release();

    if (m_templatePath.isEmpty()) {
        return false;
    }

    QFileInfo info(m_templatePath);
    if (!info.exists() || !info.isFile()) {
        Logger::error("FindShapeModelTool: template file not found: " + m_templatePath);
        return false;
    }

    QFile f(m_templatePath);
    if (!f.open(QIODevice::ReadOnly)) {
        Logger::error("FindShapeModelTool: cannot open template: " + m_templatePath);
        return false;
    }
    QByteArray bytes = f.readAll();
    f.close();
    if (bytes.isEmpty()) {
        Logger::error("FindShapeModelTool: template file is empty: " + m_templatePath);
        return false;
    }

    cv::Mat raw = cv::imdecode(
        cv::Mat(1, bytes.size(), CV_8UC1, const_cast<char*>(bytes.constData())),
        cv::IMREAD_GRAYSCALE);
    if (raw.empty()) {
        Logger::error("FindShapeModelTool: failed to decode template: " + m_templatePath);
        return false;
    }

    m_template = raw;
    Logger::info(QString("FindShapeModelTool: template loaded %1 (%2x%3)")
        .arg(info.fileName()).arg(m_template.cols).arg(m_template.rows));
    return true;
}

// 绕模板中心旋转, 使用外扩画布保证旋转后内容不被裁剪
cv::Mat FindShapeModelTool::rotateTemplate(const cv::Mat& tpl, double angleDeg) const {
    if (angleDeg == 0.0) return tpl.clone();

    // 外接正方形边长 = 对角线长度, 确保任意角度旋转都不裁剪
    int len = static_cast<int>(std::sqrt(double(tpl.cols * tpl.cols + tpl.rows * tpl.rows)) + 0.5);
    len = std::max(len, std::max(tpl.cols, tpl.rows));

    // 将原模板居中贴到正方形画布上, 背景填充中性灰(128)
    // 用中性灰可避免旋转后边缘产生强梯度影响 NCC
    cv::Mat canvas = cv::Mat::zeros(len, len, tpl.type());
    canvas.setTo(cv::Scalar(128));
    int dx = (len - tpl.cols) / 2;
    int dy = (len - tpl.rows) / 2;
    cv::Rect roi(dx, dy, tpl.cols, tpl.rows);
    tpl.copyTo(canvas(roi));

    cv::Point2f center(static_cast<float>(len) / 2.0f, static_cast<float>(len) / 2.0f);
    cv::Mat rot = cv::getRotationMatrix2D(center, angleDeg, 1.0);
    cv::Mat dst;
    cv::warpAffine(canvas, dst, rot, cv::Size(len, len),
                   cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar(128));
    return dst;
}

bool FindShapeModelTool::configure(const QJsonObject& params) {
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

    if (params.contains("angleStart")) {
        m_angleStart = params["angleStart"].toDouble();
    }

    if (params.contains("angleExtent")) {
        // 钳制到 [0, 360]
        double ext = params["angleExtent"].toDouble();
        if (ext < 0.0) ext = 0.0;
        if (ext > 360.0) ext = 360.0;
        m_angleExtent = ext;
    }

    if (params.contains("maxMatches")) {
        int m = params["maxMatches"].toInt();
        if (m < 1) m = 1;
        if (m > 1000) m = 1000;
        m_maxMatches = m;
    }

    m_params = params;
    return true;
}

bool FindShapeModelTool::execute(const cv::Mat& input, ToolResult& result) {
    // 输入检查
    if (input.empty()) {
        Logger::error("FindShapeModelTool: input image is empty");
        result.ok = false;
        result.data["error"] = "Input image is empty";
        return false;
    }

    if (m_template.empty()) {
        Logger::error("FindShapeModelTool: template not loaded");
        result.ok = false;
        result.data["error"] = "Template not loaded";
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

    // 在角度范围内枚举旋转角度
    // 步长策略: 范围≤90°用 5° 步长, 否则用 10° 步长(平衡精度与耗时)
    double step = (m_angleExtent <= 90.0) ? 5.0 : 10.0;
    if (m_angleExtent <= 0.0) step = 360.0; // 退化为 0° 单次匹配

    // bestScoreMap: 每个位置在所有角度下的最高得分
    // bestAngleMap : 对应的最优角度(度)
    cv::Mat bestScoreMap;
    cv::Mat bestAngleMap;

    double a = m_angleStart;
    double aEnd = m_angleStart + m_angleExtent;
    bool first = true;
    for (double ang = a; ang < aEnd || (first && m_angleExtent == 0.0); ang += step) {
        first = false;

        cv::Mat rotated = rotateTemplate(m_template, ang);
        if (rotated.cols > grayInput.cols || rotated.rows > grayInput.rows) {
            // 旋转后模板可能比输入还大,跳过该角度
            continue;
        }

        cv::Mat score;
        cv::matchTemplate(grayInput, rotated, score, cv::TM_CCOEFF_NORMED);

        if (first || bestScoreMap.empty()) {
            bestScoreMap = score.clone();
            bestAngleMap = cv::Mat(score.size(), CV_32FC1, cv::Scalar(static_cast<float>(ang)));
        } else {
            // 逐像素取最大值, 同时记录对应角度
            for (int y = 0; y < score.rows; ++y) {
                const float* sRow = score.ptr<float>(y);
                float* bRow = bestScoreMap.ptr<float>(y);
                float* angRow = bestAngleMap.ptr<float>(y);
                for (int x = 0; x < score.cols; ++x) {
                    if (sRow[x] > bRow[x]) {
                        bRow[x] = sRow[x];
                        angRow[x] = static_cast<float>(ang);
                    }
                }
            }
        }
        if (m_angleExtent == 0.0) break;
    }

    if (bestScoreMap.empty()) {
        Logger::warn("FindShapeModelTool: no valid angle iteration");
        result.ok = false;
        result.data["error"] = "No valid angle iteration";
        return false;
    }

    // 收集候选并按得分降序, NMS 去重
    // 这里直接对 bestScoreMap 做候选采集, 用模板外接尺寸作为框大小
    struct Cand {
        cv::Point pt;
        double score;
        double angle;
    };
    std::vector<Cand> cands;
    for (int y = 0; y < bestScoreMap.rows; ++y) {
        const float* sRow = bestScoreMap.ptr<float>(y);
        const float* aRow = bestAngleMap.ptr<float>(y);
        for (int x = 0; x < bestScoreMap.cols; ++x) {
            if (sRow[x] >= static_cast<float>(m_threshold)) {
                cands.push_back({cv::Point(x, y),
                                 static_cast<double>(sRow[x]),
                                 static_cast<double>(aRow[x])});
            }
        }
    }
    std::sort(cands.begin(), cands.end(),
        [](const Cand& a, const Cand& b) { return a.score > b.score; });

    // NMS: 框尺寸取原模板外接正方形(旋转后基准尺寸),保证框可比性
    int boxLen = static_cast<int>(std::sqrt(
        double(m_template.cols * m_template.cols + m_template.rows * m_template.rows)) + 0.5);
    boxLen = std::max(boxLen, std::max(m_template.cols, m_template.rows));
    const double iouThr = 0.3;
    const double boxArea = static_cast<double>(boxLen) * boxLen;

    std::vector<Cand> kept;
    for (const auto& c : cands) {
        if (static_cast<int>(kept.size()) >= m_maxMatches) break;
        cv::Rect box(c.pt.x, c.pt.y, boxLen, boxLen);
        bool keep = true;
        for (const auto& k : kept) {
            cv::Rect kb(k.pt.x, k.pt.y, boxLen, boxLen);
            int x1 = std::max(box.x, kb.x);
            int y1 = std::max(box.y, kb.y);
            int x2 = std::min(box.x + box.width, kb.x + kb.width);
            int y2 = std::min(box.y + box.height, kb.y + kb.height);
            int iw = std::max(0, x2 - x1);
            int ih = std::max(0, y2 - y1);
            double inter = static_cast<double>(iw) * ih;
            double uni = boxArea * 2.0 - inter;
            if (uni > 0.0 && (inter / uni) > iouThr) {
                keep = false;
                break;
            }
        }
        if (keep) kept.push_back(c);
    }

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

    // 绘制: 绿色矩形 + "得分@角度" 标签
    QJsonArray matchesArr;
    double bestScore = 0.0;
    for (const auto& k : kept) {
        if (k.score > bestScore) bestScore = k.score;

        // 框的左上角即匹配点, 右下角加上外接尺寸
        // 注意: cv::matchTemplate 返回的 (x,y) 是模板左上角在原图中的坐标,
        // 但旋转模板尺寸为 boxLen x boxLen, 实际目标中心在该框中心附近
        cv::Rect box(k.pt.x, k.pt.y, boxLen, boxLen);
        // 钳制到图像范围内,避免框线越界
        box = box & cv::Rect(0, 0, overlay.cols, overlay.rows);
        cv::rectangle(overlay, box.tl(), box.br(), cv::Scalar(0, 255, 0), 2);

        QString label = QString("%1@%2°")
            .arg(k.score, 0, 'f', 3)
            .arg(k.angle, 0, 'f', 1);
        cv::putText(overlay, label.toStdString(),
                    cv::Point(box.x, std::max(box.y - 5, 12)),
                    cv::FONT_HERSHEY_SIMPLEX, 0.5,
                    cv::Scalar(0, 255, 0), 1, cv::LINE_AA);

        // 在框中心画十字标记匹配位置
        cv::Point center(box.x + box.width / 2, box.y + box.height / 2);
        cv::drawMarker(overlay, center, cv::Scalar(0, 255, 0),
                       cv::MARKER_CROSS, 12, 1, cv::LINE_AA);

        QJsonObject m;
        m["x"] = box.x;
        m["y"] = box.y;
        m["width"] = box.width;
        m["height"] = box.height;
        m["cx"] = center.x;
        m["cy"] = center.y;
        m["score"] = k.score;
        m["angle"] = k.angle;
        matchesArr.append(m);
    }

    result.ok = !kept.empty();
    result.score = bestScore;
    result.overlayImage = overlay;

    result.data["count"] = static_cast<int>(kept.size());
    result.data["matches"] = matchesArr;
    result.data["threshold"] = m_threshold;
    result.data["angleStart"] = m_angleStart;
    result.data["angleExtent"] = m_angleExtent;
    result.data["templatePath"] = m_templatePath;

    m_results["lastMatchCount"] = static_cast<int>(kept.size());
    m_results["lastBestScore"] = bestScore;

    Logger::info(QString("FindShapeModelTool: matched %1 (best=%2)")
        .arg(kept.size()).arg(bestScore, 0, 'f', 3));

    return true;
}

QJsonObject FindShapeModelTool::serialize() const {
    QJsonObject obj = VisionTool::serialize();
    obj["templatePath"] = m_templatePath;
    obj["threshold"] = m_threshold;
    obj["angleStart"] = m_angleStart;
    obj["angleExtent"] = m_angleExtent;
    obj["maxMatches"] = m_maxMatches;
    return obj;
}

bool FindShapeModelTool::deserialize(const QJsonObject& data) {
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
    if (data.contains("angleStart")) m_angleStart = data["angleStart"].toDouble();
    if (data.contains("angleExtent")) {
        double ext = data["angleExtent"].toDouble();
        if (ext < 0.0) ext = 0.0;
        if (ext > 360.0) ext = 360.0;
        m_angleExtent = ext;
    }
    if (data.contains("maxMatches")) {
        int m = data["maxMatches"].toInt();
        if (m < 1) m = 1;
        if (m > 1000) m = 1000;
        m_maxMatches = m;
    }
    return true;
}
