#include "LoopTool.h"
#include "Core/Logger.h"
#include <opencv2/imgproc.hpp>
#include <QRegularExpression>
#include <QStringList>

using namespace QDV;

// =====================================================
// 构造
// =====================================================
LoopTool::LoopTool() {
    m_name = "循环遍历";
}

// =====================================================
// 参数配置
// =====================================================
bool LoopTool::configure(const QJsonObject& params) {
    if (params.contains("mode")) {
        const QString v = params["mode"].toString();
        // v2.7.0：白名单扩展 count / while
        if (v == "grid" || v == "list" || v == "detection" ||
            v == "count" || v == "while") {
            m_mode = v;
        } else {
            Logger::warn(QString("LoopTool: mode '%1' 非法，回退为 'grid'").arg(v));
            m_mode = "grid";
        }
    }
    if (params.contains("gridCols")) {
        const int c = params["gridCols"].toInt();
        m_gridCols = (c > 0) ? c : 3;            // 列数必须 > 0
    }
    if (params.contains("gridRows")) {
        const int r = params["gridRows"].toInt();
        m_gridRows = (r > 0) ? r : 3;            // 行数必须 > 0
    }
    if (params.contains("gridOverlap")) {
        const int o = params["gridOverlap"].toInt();
        m_gridOverlap = (o >= 0) ? o : 0;        // 重叠像素非负
    }
    if (params.contains("roiList")) {
        m_roiList = params["roiList"].toString();
    }
    if (params.contains("startIndex")) {
        m_startIndex = params["startIndex"].toInt();
        if (m_startIndex < 0) m_startIndex = 0;
    }
    if (params.contains("maxIterations")) {
        const int m = params["maxIterations"].toInt();
        // 钳制到 [1, 10000]，防止过大导致内存爆炸
        m_maxIterations = (m > 0) ? qMin(m, 10000) : 100;
    }

    // v2.7.0：count 模式参数
    if (params.contains("count")) {
        int c = params["count"].toInt();
        if (c > 0) m_count = c;
    }
    // v2.7.0：while 模式参数
    if (params.contains("whileCondition")) {
        m_whileCondition = params["whileCondition"].toString();
    }
    if (params.contains("maxWhileIterations")) {
        int m = params["maxWhileIterations"].toInt();
        if (m > 0 && m <= 100000) m_maxWhileIterations = m;
    }

    m_params = params;
    return true;
}

// =====================================================
// 网格切分：按 gridCols×gridRows + overlap 生成 ROI 列表
// 设单元格基础尺寸 stepW = imgW / cols，overlap 使相邻单元格向彼此延伸 overlap/2
// =====================================================
QVariantList LoopTool::buildGridRois(int imgW, int imgH) const {
    QVariantList rois;
    if (imgW <= 0 || imgH <= 0) return rois;

    const int cols = qMax(1, m_gridCols);
    const int rows = qMax(1, m_gridRows);
    // 每格基础宽高（向下取整，保证不越界）
    const int baseW = imgW / cols;
    const int baseH = imgH / rows;
    if (baseW <= 0 || baseH <= 0) return rois;

    const int halfOverlap = m_gridOverlap / 2;

    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            // 基础左上角
            int x0 = c * baseW;
            int y0 = r * baseH;
            int w0 = baseW;
            int h0 = baseH;
            // 向相邻方向扩展 overlap/2，再裁剪到图像边界
            x0 = qMax(0, x0 - halfOverlap);
            y0 = qMax(0, y0 - halfOverlap);
            int x1 = qMin(imgW, (c + 1) * baseW + halfOverlap);
            int y1 = qMin(imgH, (r + 1) * baseH + halfOverlap);
            w0 = x1 - x0;
            h0 = y1 - y0;
            if (w0 <= 0 || h0 <= 0) continue;

            QVariantMap roi;
            roi["x"] = x0;
            roi["y"] = y0;
            roi["w"] = w0;
            roi["h"] = h0;
            rois.append(roi);

            // 达到 maxIterations 上限即停止，防止意外爆炸
            if (rois.size() >= m_maxIterations) return rois;
        }
    }
    return rois;
}

// =====================================================
// 解析 "x,y,w,h;x,y,w,h;..." 为 ROI 列表
// 容错：跳过空段与非法数字
// =====================================================
QVariantList LoopTool::parseRoiListString(const QString& text) {
    QVariantList rois;
    if (text.trimmed().isEmpty()) return rois;

    // 段分隔符：分号（中英文均可）
    const QStringList segments = text.split(
        QRegularExpression(QStringLiteral("[;；]")), Qt::SkipEmptyParts);

    for (const QString& seg : segments) {
        const QStringList nums = seg.split(
            QRegularExpression(QStringLiteral("[,，]")), Qt::SkipEmptyParts);
        if (nums.size() < 4) continue;

        bool ok1 = false, ok2 = false, ok3 = false, ok4 = false;
        const int x = nums[0].trimmed().toInt(&ok1);
        const int y = nums[1].trimmed().toInt(&ok2);
        const int w = nums[2].trimmed().toInt(&ok3);
        const int h = nums[3].trimmed().toInt(&ok4);
        if (!ok1 || !ok2 || !ok3 || !ok4) continue;
        if (w <= 0 || h <= 0) continue;          // 宽高必须为正

        QVariantMap roi;
        roi["x"] = x;
        roi["y"] = y;
        roi["w"] = w;
        roi["h"] = h;
        rois.append(roi);

        if (rois.size() >= 10000) break;         // 兜底保护
    }
    return rois;
}

// =====================================================
// 在 overlay 上绘制 ROI 框（黄色）+ 索引文字
// =====================================================
void LoopTool::drawRois(cv::Mat& overlay, const QVariantList& rois) {
    if (overlay.empty() || rois.isEmpty()) return;

    // 转为 BGR 以便绘制彩色框与文字
    if (overlay.channels() == 1) {
        cv::Mat tmp;
        cv::cvtColor(overlay, tmp, cv::COLOR_GRAY2BGR);
        overlay = tmp;
    }

    const cv::Scalar yellow(0, 255, 255);        // BGR 黄色
    for (int i = 0; i < rois.size(); ++i) {
        const QVariantMap roi = rois[i].toMap();
        const int x = roi.value("x").toInt();
        const int y = roi.value("y").toInt();
        const int w = roi.value("w").toInt();
        const int h = roi.value("h").toInt();
        if (w <= 0 || h <= 0) continue;

        const cv::Rect rect(x, y, w, h);
        cv::rectangle(overlay, rect, yellow, 2);

        // 索引文字（左上角，带黑色描边便于阅读）
        const QString idxText = QString::number(i);
        const std::string txt = idxText.toStdString();
        const cv::Point org(x + 4, y + 18);
        cv::putText(overlay, txt, org, cv::FONT_HERSHEY_SIMPLEX, 0.6,
                    cv::Scalar(0, 0, 0), 3, cv::LINE_AA);   // 黑色描边
        cv::putText(overlay, txt, org, cv::FONT_HERSHEY_SIMPLEX, 0.6,
                    yellow, 1, cv::LINE_AA);
    }
}

// =====================================================
// execute：生成 ROI 列表 + overlay 可视化
// =====================================================
bool LoopTool::execute(const cv::Mat& input, ToolResult& result) {
    try {
        // 空输入图像直接失败
        if (input.empty()) {
            result.ok = false;
            result.data["error"] = "输入图像为空";
            Logger::warn("LoopTool: 输入图像为空");
            return false;
        }

        QVariantList rois;
        QString effectiveMode = m_mode;

        if (m_mode == "list") {
            rois = parseRoiListString(m_roiList);
            if (rois.isEmpty()) {
                // list 模式但解析为空：降级 grid 并提示
                Logger::warn("LoopTool: list 模式 ROI 解析为空，降级为 grid");
                effectiveMode = "grid";
            }
        }

        if (m_mode == "detection") {
            // 当前无检测结果输入通道，降级 grid
            Logger::info("LoopTool: detection 模式暂无检测输入，降级为 grid");
            effectiveMode = "grid";
        }

        // v2.7.0：count 模式 - 计数循环，生成 N 个全图 ROI 占位
        // 实际子链执行由 ToolChainExecutor::executeSubChain 编排
        if (m_mode == "count") {
            const int n = qMax(1, qMin(m_count, 10000));
            for (int i = 0; i < n; ++i) {
                QVariantMap roi;
                roi["x"] = 0;
                roi["y"] = 0;
                roi["w"] = input.cols;
                roi["h"] = input.rows;
                roi["index"] = i;  // 迭代索引
                rois.append(roi);
            }
            Logger::info(QString("LoopTool count 模式: 生成 %1 个迭代").arg(n));
        }

        // v2.7.0：while 模式 - 条件循环，生成 maxWhileIterations 个占位 ROI
        // 实际条件判断由 ToolChainExecutor 在每次迭代后评估 whileCondition
        // 当前简化实现：生成最大迭代数的占位 ROI，由执行器决定何时停止
        if (m_mode == "while") {
            const int n = qMax(1, qMin(m_maxWhileIterations, 10000));
            for (int i = 0; i < n; ++i) {
                QVariantMap roi;
                roi["x"] = 0;
                roi["y"] = 0;
                roi["w"] = input.cols;
                roi["h"] = input.rows;
                roi["index"] = i;
                roi["isWhile"] = true;  // 标记 while 模式
                rois.append(roi);
            }
            Logger::info(QString("LoopTool while 模式: 最大 %1 次迭代，条件='%2'")
                         .arg(n).arg(m_whileCondition));
        }

        if (effectiveMode == "grid" || rois.isEmpty()) {
            rois = buildGridRois(input.cols, input.rows);
        }

        // 钳制起始索引到合法范围
        int curIdx = m_startIndex;
        if (curIdx >= rois.size()) curIdx = 0;
        if (curIdx < 0) curIdx = 0;

        // 写出结果（ports + data 双通道）
        result.ok = true;
        result.ports["roiList"] = QVariant(rois);
        result.ports["count"] = QVariant(rois.size());

        result.data["mode"] = effectiveMode;
        result.data["count"] = rois.size();
        result.data["currentIndex"] = curIdx;
        result.data["startIndex"] = m_startIndex;
        result.data["maxIterations"] = m_maxIterations;
        result.data["gridCols"] = m_gridCols;
        result.data["gridRows"] = m_gridRows;
        result.data["gridOverlap"] = m_gridOverlap;

        // overlay：在输入上绘制所有 ROI 框 + 索引
        result.overlayImage = input.clone();
        drawRois(result.overlayImage, rois);

        Logger::info(QString("LoopTool: mode=%1, count=%2, currentIndex=%3")
                         .arg(effectiveMode).arg(rois.size()).arg(curIdx));
        return true;

    } catch (const cv::Exception& e) {
        Logger::error(QString("LoopTool: OpenCV 异常: %1").arg(QString::fromStdString(e.what())));
        result.ok = false;
        result.data["error"] = QString::fromStdString(e.what());
        return false;
    } catch (const std::exception& e) {
        Logger::error(QString("LoopTool: 标准异常: %1").arg(QString::fromStdString(e.what())));
        result.ok = false;
        result.data["error"] = QString::fromStdString(e.what());
        return false;
    }
}

// =====================================================
// 端口声明（P1-3 typed ports）
// =====================================================
QList<PortDescriptor> LoopTool::outputPorts() const {
    return {
        PortDescriptor{ "roiList", "ROI列表", PortType::Points, PortDirection::Out, "循环遍历的 ROI 列表，每个元素 {x,y,w,h}" },
        PortDescriptor{ "count",   "总数",    PortType::Number, PortDirection::Out, "ROI 总数" },
    };
}

QList<PortDescriptor> LoopTool::inputPorts() const {
    return {
        PortDescriptor{ "image", "图像", PortType::Image, PortDirection::In, "输入图像（用于 grid 切分与 overlay 绘制）" },
    };
}

// =====================================================
// 序列化 / 反序列化
// =====================================================
QJsonObject LoopTool::serialize() const {
    QJsonObject obj = VisionTool::serialize();
    obj["mode"]          = m_mode;
    obj["gridCols"]      = m_gridCols;
    obj["gridRows"]      = m_gridRows;
    obj["gridOverlap"]   = m_gridOverlap;
    obj["roiList"]       = m_roiList;
    obj["startIndex"]    = m_startIndex;
    obj["maxIterations"] = m_maxIterations;
    // v2.7.0：count / while 模式参数
    obj["count"]                = m_count;
    obj["whileCondition"]       = m_whileCondition;
    obj["maxWhileIterations"]   = m_maxWhileIterations;
    return obj;
}

bool LoopTool::deserialize(const QJsonObject& data) {
    if (!VisionTool::deserialize(data)) return false;

    if (data.contains("mode")) {
        const QString v = data["mode"].toString();
        // v2.7.0：白名单扩展 count / while
        if (v == "grid" || v == "list" || v == "detection" ||
            v == "count" || v == "while") m_mode = v;
    }
    if (data.contains("gridCols")) {
        const int c = data["gridCols"].toInt();
        m_gridCols = (c > 0) ? c : 3;
    }
    if (data.contains("gridRows")) {
        const int r = data["gridRows"].toInt();
        m_gridRows = (r > 0) ? r : 3;
    }
    if (data.contains("gridOverlap")) {
        const int o = data["gridOverlap"].toInt();
        m_gridOverlap = (o >= 0) ? o : 0;
    }
    if (data.contains("roiList"))       m_roiList      = data["roiList"].toString();
    if (data.contains("startIndex")) {
        const int s = data["startIndex"].toInt();
        m_startIndex = (s >= 0) ? s : 0;
    }
    if (data.contains("maxIterations")) {
        const int m = data["maxIterations"].toInt();
        m_maxIterations = (m > 0) ? qMin(m, 10000) : 100;
    }
    // v2.7.0：count / while 模式参数
    if (data.contains("count")) {
        const int c = data["count"].toInt();
        if (c > 0) m_count = c;
    }
    if (data.contains("whileCondition")) {
        m_whileCondition = data["whileCondition"].toString();
    }
    if (data.contains("maxWhileIterations")) {
        const int m = data["maxWhileIterations"].toInt();
        if (m > 0 && m <= 100000) m_maxWhileIterations = m;
    }
    return true;
}
