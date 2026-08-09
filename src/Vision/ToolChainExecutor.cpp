#include "ToolChainExecutor.h"
#include "Core/Logger.h"
#include "ToolFactory.h"               // v2.7.0：子链/分支独立 Tool 实例创建
#include <QtConcurrent/QtConcurrent>
#include <QtConcurrent/QtConcurrentMap> // v2.7.0：blockingMap 并行执行分支
#include <QThread>
#include <QCoreApplication>
#include <QJsonArray>
#include <QVariantMap>
#include <opencv2/core.hpp>    // P0 修复：cv::Exception
#include <opencv2/imgproc.hpp> // spec Task 2：cv::fillPoly/cv::bitwise_and/cv::resize
#include <algorithm>           // v2.7.0：std::max/std::min 边界保护

using namespace QDV;

// =====================================================================
// spec 阶段一 Task 2：算子级通用 ROI 辅助函数（匿名命名空间）
// 设计要点：
// - 矩形 ROI：裁剪输入图像，执行后坐标 += roiRect.tl()，overlayImage 贴回原图
// - 多边形 ROI：掩蔽 ROI 外像素（不改坐标系），执行后过滤完全在 ROI 外的检测框
// - 无 ROI：透传原图（零回归）
// =====================================================================
namespace {

// 解析矩形 ROI 为 cv::Rect（带边界保护）
// 返回 true 表示有效矩形 ROI
bool parseRectRoi(const QVariantMap& roi, const cv::Mat& input, cv::Rect& outRect) {
    if (roi.value("type").toString() != "rect") return false;
    int x = roi.value("x").toInt();
    int y = roi.value("y").toInt();
    int w = roi.value("w").toInt();
    int h = roi.value("h").toInt();
    // 边界保护：ROI 必须落在输入图像范围内
    x = std::max(0, std::min(x, input.cols - 1));
    y = std::max(0, std::min(y, input.rows - 1));
    w = std::max(1, std::min(w, input.cols - x));
    h = std::max(1, std::min(h, input.rows - y));
    outRect = cv::Rect(x, y, w, h);
    return true;
}

// 生成多边形掩码（ROI 内 255，ROI 外 0）
cv::Mat makePolygonMask(const QVariantMap& roi, const cv::Size& size) {
    cv::Mat mask = cv::Mat::zeros(size, CV_8UC1);
    const QVariantList pts = roi.value("points").toList();
    if (pts.size() < 3) return mask;  // 多边形至少需要 3 个点
    std::vector<cv::Point> cvpts;
    cvpts.reserve(pts.size());
    for (const QVariant& v : pts) {
        const QVariantMap pm = v.toMap();
        cvpts.emplace_back(pm.value("x").toInt(), pm.value("y").toInt());
    }
    cv::fillPoly(mask, std::vector<std::vector<cv::Point>>{cvpts}, cv::Scalar(255));
    return mask;
}

// 对 ToolResult.data["detections"] 中的检测框坐标按 (dx, dy) 偏移
// 兼容多种格式：bbox[x,y,w,h] / cx,cy / x1,y1,x2,y2 / x,y,w,h
void offsetDetections(ToolResult& result, int dx, int dy) {
    if (dx == 0 && dy == 0) return;
    if (!result.data.contains("detections")) return;

    QJsonArray dets = result.data.value("detections").toArray();
    QJsonArray newDets;
    for (const QJsonValue& v : dets) {
        QJsonObject d = v.toObject();
        // bbox: [x, y, w, h]（YoloDetectTool 实际格式，左上角 + 宽高）
        if (d.contains("bbox") && d.value("bbox").isArray()) {
            QJsonArray bbox = d.value("bbox").toArray();
            if (bbox.size() >= 2) {
                QJsonArray newBbox;
                newBbox.append(bbox[0].toDouble() + dx);
                newBbox.append(bbox[1].toDouble() + dy);
                for (int i = 2; i < bbox.size(); ++i) newBbox.append(bbox[i]);
                d["bbox"] = newBbox;
            }
        }
        // cx/cy 格式（中心坐标）
        if (d.contains("cx")) d["cx"] = d.value("cx").toDouble() + dx;
        if (d.contains("cy")) d["cy"] = d.value("cy").toDouble() + dy;
        // x1/y1/x2/y2 格式（对角坐标）
        if (d.contains("x1")) d["x1"] = d.value("x1").toDouble() + dx;
        if (d.contains("y1")) d["y1"] = d.value("y1").toDouble() + dy;
        if (d.contains("x2")) d["x2"] = d.value("x2").toDouble() + dx;
        if (d.contains("y2")) d["y2"] = d.value("y2").toDouble() + dy;
        // x/y/w/h 格式（左上角 + 宽高，只偏移 x/y）
        if (d.contains("x") && d.contains("y") && d.contains("w")) {
            d["x"] = d.value("x").toDouble() + dx;
            d["y"] = d.value("y").toDouble() + dy;
        }
        newDets.append(d);
    }
    result.data["detections"] = newDets;
}

// 对 ToolResult.ports 中的点类型数据按 (dx, dy) 偏移
// 仅处理含 x/y 键的 QVariantMap（点/位姿），不处理数值/字符串/布尔端口
void offsetPorts(ToolResult& result, int dx, int dy) {
    if (dx == 0 && dy == 0) return;
    QVariantMap newPorts;
    for (auto it = result.ports.begin(); it != result.ports.end(); ++it) {
        QVariant v = it.value();
        if (v.type() == QVariant::Map) {
            QVariantMap m = v.toMap();
            // 含 x/y 键的视为点/位姿，做偏移
            if (m.contains("x") && m.contains("y")) {
                m["x"] = m.value("x").toDouble() + dx;
                m["y"] = m.value("y").toDouble() + dy;
                v = m;
            }
        }
        newPorts[it.key()] = v;
    }
    result.ports = newPorts;
}

// 过滤完全在多边形 ROI 外的检测框（按中心点判断）
void filterDetectionsByPolygon(ToolResult& result, const cv::Mat& mask) {
    if (!result.data.contains("detections")) return;
    QJsonArray dets = result.data.value("detections").toArray();
    QJsonArray newDets;
    for (const QJsonValue& v : dets) {
        QJsonObject d = v.toObject();
        double cx = 0, cy = 0;
        bool hasCenter = false;
        // bbox: [x, y, w, h]
        if (d.contains("bbox") && d.value("bbox").isArray()) {
            QJsonArray bbox = d.value("bbox").toArray();
            if (bbox.size() >= 4) {
                cx = bbox[0].toDouble() + bbox[2].toDouble() / 2.0;
                cy = bbox[1].toDouble() + bbox[3].toDouble() / 2.0;
                hasCenter = true;
            }
        }
        if (!hasCenter && d.contains("cx") && d.contains("cy")) {
            cx = d.value("cx").toDouble();
            cy = d.value("cy").toDouble();
            hasCenter = true;
        }
        if (!hasCenter && d.contains("x") && d.contains("y") && d.contains("w") && d.contains("h")) {
            cx = d.value("x").toDouble() + d.value("w").toDouble() / 2.0;
            cy = d.value("y").toDouble() + d.value("h").toDouble() / 2.0;
            hasCenter = true;
        }
        if (!hasCenter && d.contains("x1") && d.contains("y1") && d.contains("x2") && d.contains("y2")) {
            cx = (d.value("x1").toDouble() + d.value("x2").toDouble()) / 2.0;
            cy = (d.value("y1").toDouble() + d.value("y2").toDouble()) / 2.0;
            hasCenter = true;
        }
        if (!hasCenter) {
            // 无法判定中心，保守保留
            newDets.append(d);
            continue;
        }
        int ix = static_cast<int>(cx);
        int iy = static_cast<int>(cy);
        if (ix >= 0 && ix < mask.cols && iy >= 0 && iy < mask.rows && mask.at<uchar>(iy, ix) > 0) {
            newDets.append(d);
        }
    }
    result.data["detections"] = newDets;
}

} // anonymous namespace

ToolChainExecutor::ToolChainExecutor(QObject* parent) : QObject(parent) {
}

void ToolChainExecutor::setTools(const QList<VisionTool*>& tools) {
    QMutexLocker locker(&m_mutex);
    m_tools = tools;
}

void ToolChainExecutor::setBranches(const QMap<QString, BranchNode*>& branches) {
    QMutexLocker locker(&m_mutex);
    m_branches = branches;
}

// v2.7.0：设置子链映射（LoopTool id → 子链算子列表）
void ToolChainExecutor::setSubChains(const QMap<QString, QList<QDV::VisionTool*>>& subChains) {
    QMutexLocker locker(&m_mutex);
    m_subChains = subChains;
}

// v2.7.0：设置并行分支映射（分支起始 toolId → 分支算子列表）
void ToolChainExecutor::setParallelBranches(const QMap<QString, QList<QDV::VisionTool*>>& branches) {
    QMutexLocker locker(&m_mutex);
    m_parallelBranches = branches;
}

// v2.7.0：获取子链迭代结果（按迭代索引顺序）
QList<ToolResult> ToolChainExecutor::getIterationResults(const QString& loopToolId) const {
    QMutexLocker locker(&m_mutex);
    return m_iterationResults.value(loopToolId);
}

// ============================================================================
// 端口级多输入数据流执行（spec：editor-output-connection-optimization）
// 设计：与 execute() 并存（向后兼容）。executeWithFlow 按端口绑定构建依赖图，
// 以拓扑序执行，保证每个算子仅在其全部上游执行后运行（真正的多上游数据流），
// 并将各上游已勾选输出的 typed 数据按下游端口名合并到 m_flowInputs 供算子消费。
// ============================================================================
void ToolChainExecutor::setFlowBindings(const QList<FlowBinding>& bindings) {
    QMutexLocker locker(&m_mutex);
    m_flowBindings.clear();
    for (const FlowBinding& b : bindings) {
        if (b.downstreamToolId.isEmpty() || b.upstreamToolId.isEmpty()) continue;
        m_flowBindings[b.downstreamToolId].append(b);
    }
}

QVariantMap ToolChainExecutor::flowInputsFor(const QString& toolId) const {
    QMutexLocker locker(&m_mutex);
    return m_flowInputs.value(toolId);
}

QList<QString> ToolChainExecutor::topoSortFlow() const {
    // 依赖：下游 toolId 依赖其全部上游 toolId
    QMap<QString, int> indegree;
    QMap<QString, QList<QString>> adj;   // 上游 → 下游
    for (const QDV::VisionTool* t : m_tools) {
        const QString id = t->id();
        if (!indegree.contains(id)) indegree[id] = 0;
    }
    for (auto it = m_flowBindings.constBegin(); it != m_flowBindings.constEnd(); ++it) {
        const QString downstream = it.key();
        for (const FlowBinding& b : it.value()) {
            const QString up = b.upstreamToolId;
            if (!indegree.contains(up)) continue;   // 上游不在当前链中，忽略
            if (!adj[up].contains(downstream)) adj[up].append(downstream);
            indegree[downstream] += 1;
        }
    }
    // Kahn 拓扑排序
    QList<QString> ready;
    QMap<QString, int> deg = indegree;
    for (auto it = deg.constBegin(); it != deg.constEnd(); ++it) {
        if (it.value() == 0) ready.append(it.key());
    }
    QList<QString> order;
    while (!ready.isEmpty()) {
        const QString n = ready.takeFirst();
        order.append(n);
        for (const QString& m : adj.value(n)) {
            deg[m] -= 1;
            if (deg[m] == 0) ready.append(m);
        }
    }
    if (order.size() != indegree.size()) {
        // 存在环
        Logger::error(QStringLiteral("executeWithFlow: 检测到端口绑定环，无法拓扑排序"));
        return {};
    }
    return order;
}

bool ToolChainExecutor::executeFlowNode(QDV::VisionTool* tool, const cv::Mat& primaryInput,
                                        ToolResult& result) {
    if (!tool) return false;
    // 1) 组装主输入图像：优先用"首选上游"的 overlayImage（线性流语义），否则用 primaryInput
    cv::Mat mainInput = primaryInput;
    const QList<FlowBinding> binds = m_flowBindings.value(tool->id());
    for (const FlowBinding& b : binds) {
        const ToolResult& up = m_results.value(b.upstreamToolId);
        if (!up.overlayImage.empty()) {
            mainInput = up.overlayImage.clone();
            break;  // 取首个有图像的绑定作为主输入
        }
    }
    // 2) 合并各上游已勾选输出的 typed 数据 → m_flowInputs[toolId][下游端口名]
    QVariantMap merged;
    for (const FlowBinding& b : binds) {
        const ToolResult& up = m_results.value(b.upstreamToolId);
        // 从上游 ports（typed 数据）取值；找不到端口再从 data 取该端口名键
        if (up.ports.contains(b.upstreamPort)) {
            merged[b.downstreamPort] = up.ports.value(b.upstreamPort);
        } else if (up.data.contains(b.upstreamPort)) {
            merged[b.downstreamPort] = up.data.value(b.upstreamPort).toVariant();
        }
    }
    {
        QMutexLocker locker(&m_mutex);
        m_flowInputs[tool->id()] = merged;
    }
    // 3) 执行算子（复用 executeTool：含 ROI 处理与 OpenCV 异常捕获）
    return executeTool(tool, mainInput, result);
}

bool ToolChainExecutor::executeWithFlow(const cv::Mat& primaryInput) {
    if (primaryInput.empty()) {
        Logger::error("executeWithFlow: 初始输入图像为空");
        return false;
    }
    if (m_running.exchange(true)) {
        Logger::warn("executeWithFlow: 已在运行，拒绝重入");
        return false;
    }
    QList<QDV::VisionTool*> toolsCopy;
    {
        QMutexLocker locker(&m_mutex);
        m_results.clear();
        m_flowInputs.clear();
        toolsCopy = m_tools;
    }

    const QList<QString> order = topoSortFlow();
    if (order.isEmpty() && !m_flowBindings.isEmpty()) {
        m_running = false;
        emit chainCompleted(false);
        return false;
    }

    // 若无绑定（纯线性场景），退化为 execute() 语义
    if (m_flowBindings.isEmpty()) {
        m_running = false;
        return execute(primaryInput);
    }

    // 按拓扑序执行
    int failCount = 0;
    int executed = 0;
    const int total = order.size();
    QMap<QString, QDV::VisionTool*> byId;
    for (QDV::VisionTool* t : toolsCopy) byId[t->id()] = t;

    for (const QString& id : order) {
        if (!m_running) break;
        QDV::VisionTool* tool = byId.value(id);
        if (!tool) continue;
        ToolResult result;
        if (!executeFlowNode(tool, primaryInput, result)) {
            failCount++;
            QString detail = result.data.contains("error")
                ? result.data["error"].toString() : QString();
            QString msg = QStringLiteral("工具执行失败: %1").arg(tool->name());
            if (!detail.isEmpty()) msg += QStringLiteral(" - %1").arg(detail);
            emit toolFailed(tool->id(), msg);
            continue;
        }
        {
            QMutexLocker locker(&m_mutex);
            m_results[tool->id()] = result;
        }
        emit toolExecuted(tool->id(), result);
        executed++;
        emit executionProgress(executed, total);
    }

    m_running = false;
    const bool ok = (failCount == 0) && (executed > 0);
    emit chainCompleted(ok);
    return ok;
}

bool ToolChainExecutor::execute(const cv::Mat& input) {
    // 先验证输入，再获取执行权（避免提前返回导致 m_running 残留）
    if (input.empty()) {
        Logger::error("Input image is empty");
        return false;
    }

    // 原子检查：防止多个线程同时执行工具链
    if (m_running.exchange(true)) {
        Logger::warn("ToolChainExecutor::execute() rejected — already running");
        return false;
    }

    if (QThread::currentThread() == QCoreApplication::instance()->thread()) {
        // v5.3.2：降级为 debug，避免 PreviewManager 预览时刷屏
        // （PreviewManager.doPreview 已通过 QtConcurrent::run 在子线程执行，
        //  但某些同步调用路径如 runScheme 仍可能在主线程触发，属已知情况）
        Logger::info("ToolChainExecutor::execute() running on main thread (consider using async API)");
    }
    
    m_running = true;

    cv::Mat currentInput = input.clone();
    int totalTools;
    QList<VisionTool*> toolsCopy;
    QMap<QString, BranchNode*> branchesCopy;
    {
        QMutexLocker locker(&m_mutex);
        // P1-B13 修复：m_results.clear() 移到锁内，避免与 getResult() 数据竞争
        // 之前 line 39 锁外 clear + line 48 锁内 clear 重复，删除锁外那次
        m_results.clear();
        totalTools = m_tools.size();
        toolsCopy = m_tools;
        branchesCopy = m_branches;
    }
    
    int currentIndex = 0;
    int failCount = 0;   // P1-A2 修复：统计失败工具数，链失败时返回 false

    for (VisionTool* tool : toolsCopy) {
        if (!m_running) {
            break;
        }

        ToolResult result;
        if (!executeTool(tool, currentInput, result)) {
            Logger::error("Tool execution failed: " + tool->name());
            // P1-A2 修复：之前仅 continue 且最后无条件 return true，调用方认为链成功
            failCount++;
            // 透传算子返回的具体错误信息，避免 UI 仅显示"未知错误"
            QString detail = result.data.contains("error")
                ? result.data["error"].toString()
                : QString();
            QString msg = QStringLiteral("工具执行失败: %1").arg(tool->name());
            if (!detail.isEmpty()) {
                msg += QStringLiteral(" - %1").arg(detail);
            }
            emit toolFailed(tool->id(), msg);
            continue;
        }

        // v2.7.0：LoopTool 子链编排
        // 说明：LoopTool 先用 executeTool 执行自身得到 roiList，
        // 再用 executeSubChain 对每个 ROI 切片执行子链，结果聚合到 result 中。
        // 必须在 m_results[tool->id()] = result 之前完成，以便后续路由看到聚合后的数据。
        if (tool->type() == QStringLiteral("Loop") && m_subChains.contains(tool->id())) {
            ExecutionContext ctx;
            ctx.upstreamResults = &m_results;
            if (!executeSubChain(tool, currentInput, result, ctx)) {
                failCount++;
                emit toolFailed(tool->id(), QStringLiteral("子链循环执行失败"));
                // 子链失败不 continue，继续主链（结果已在 result 中）
            }
            // 子链结果已写入 result，继续走下方的 m_results 记录逻辑
        }

        {
            QMutexLocker locker(&m_mutex);
            m_results[tool->id()] = result;
        }
        emit toolExecuted(tool->id(), result);

        if (!result.overlayImage.empty()) {
            currentInput = result.overlayImage.clone();
        }

        currentIndex++;
        emit executionProgress(currentIndex, totalTools);

        // v2.7.0：trueBranch/falseBranch 路由（替代原来的单一 break）
        // 原行为：分支条件不满足 → break
        // 新行为：根据条件选择 trueBranch 或 falseBranch 执行；
        //        条件不满足且无 falseBranch → 保留原 break 行为
        if (branchesCopy.contains(tool->id())) {
            BranchNode* branch = branchesCopy[tool->id()];
            if (branch && branch->isValid()) {
                const bool conditionMet = evaluateBranch(branch);
                // 获取分支工具列表
                const QList<QString>& targetBranch = conditionMet
                    ? branch->trueBranchToolIds
                    : branch->falseBranchToolIds;

                if (!targetBranch.isEmpty()) {
                    // 有分支工具：执行选中的分支
                    QList<VisionTool*> branchTools;
                    for (const QString& tid : targetBranch) {
                        for (VisionTool* t : toolsCopy) {
                            if (t->id() == tid) { branchTools.append(t); break; }
                        }
                    }
                    if (!branchTools.isEmpty()) {
                        ExecutionContext ctx;
                        ctx.upstreamResults = &m_results;
                        ToolResult branchResult = executeBranchSequence(branchTools, currentInput, ctx);
                        // 分支内每个算子的结果已在 executeBranchSequence 内记录到 m_results
                        // 分支最后结果覆盖 currentInput，以便主链后续算子接续
                        if (!branchResult.overlayImage.empty()) {
                            currentInput = branchResult.overlayImage.clone();
                        }
                    }
                    // 无论分支是否执行，主链继续（不 break）
                } else if (!conditionMet) {
                    // 条件不满足且无 falseBranch → break（保留原行为）
                    break;
                }
                // 条件满足但无 trueBranch → 主链继续（不 break）
            } else {
                // 分支无效，保留原行为：条件不满足则 break
                if (!evaluateBranch(branch)) {
                    break;
                }
            }
        }
    }

    m_running = false;
    // P1-A2 修复：根据失败数决定返回值，链失败时通知调用方
    const bool overallSuccess = (failCount == 0) && (currentIndex > 0 || totalTools == 0);
    if (!overallSuccess) {
        Logger::error(QStringLiteral("ToolChain execution completed with %1/%2 failures")
                          .arg(failCount).arg(totalTools));
    }
    emit chainCompleted(overallSuccess);
    return overallSuccess;
}

QFuture<bool> ToolChainExecutor::executeAsync(const cv::Mat& input) {
    return QtConcurrent::run([this, input]() {
        return execute(input);
    });
}

void ToolChainExecutor::stop() {
    m_running = false;
}

bool ToolChainExecutor::executeTool(VisionTool* tool, const cv::Mat& input, ToolResult& result) {
    // P1-C3 修复（PreReleaseReviewReport Minor 12）：空指针防御
    // 之前直接 tool->execute，若 setTools 传入含 nullptr 的列表会崩溃
    if (!tool) {
        Logger::error("ToolChainExecutor::executeTool: null tool pointer");
        result.ok = false;
        return false;
    }
    if (input.empty()) {
        Logger::error(QString("ToolChainExecutor::executeTool: empty input for tool %1")
            .arg(tool->name()));
        result.ok = false;
        return false;
    }

    qint64 startTime = QDateTime::currentMSecsSinceEpoch();

    // ===== spec 阶段一 Task 2：算子级通用 ROI 自动裁剪 =====
    // 设计：在调用 tool->execute 前，按 tool->roi() 自动裁剪/掩蔽输入图像
    //   - 矩形 ROI：用 cv::Mat::operator() 裁剪（产生尺寸更小的子图）
    //   - 多边形 ROI：用 cv::fillPoly 生成掩码，cv::bitwise_and 掩蔽 ROI 外像素（尺寸不变）
    //   - 无 ROI：透传原图（零回归，向后兼容）
    cv::Mat toolInput = input;                  // 默认透传原图
    cv::Mat polygonMask;                         // 多边形掩码（非空表示多边形 ROI）
    cv::Rect roiRect(0, 0, 0, 0);                // 矩形 ROI 区域（用于坐标反变换与 overlayImage 贴回）
    bool useRectRoi = false;
    bool usePolygonRoi = false;

    if (tool->hasRoi()) {
        const QVariantMap roiMap = tool->roi();
        const QString roiType = roiMap.value("type").toString();
        if (roiType == "rect") {
            if (parseRectRoi(roiMap, input, roiRect)) {
                toolInput = input(roiRect).clone();   // 裁剪为子图
                useRectRoi = true;
                Logger::info(QString("ToolChainExecutor: 矩形 ROI 裁剪 %1 -> %2x%3 @(%4,%5)")
                    .arg(tool->name()).arg(roiRect.width).arg(roiRect.height)
                    .arg(roiRect.x).arg(roiRect.y));
            }
        } else if (roiType == "polygon") {
            polygonMask = makePolygonMask(roiMap, input.size());
            if (!polygonMask.empty()) {
                cv::bitwise_and(input, input, toolInput, polygonMask);
                usePolygonRoi = true;
                Logger::info(QString("ToolChainExecutor: 多边形 ROI 掩蔽 %1 (%2 个顶点)")
                    .arg(tool->name())
                    .arg(roiMap.value("points").toList().size()));
            }
        }
    }

    // P0 修复：try-catch 捕获 OpenCV 异常，防止参数校验遗漏导致程序崩溃。
    // 之前 cv::Canny/cv::threshold 等函数在参数非法时抛出 cv::Exception，
    // 未被捕获直接导致应用闪退。
    bool success = false;
    try {
        success = tool->execute(toolInput, result);
    } catch (const cv::Exception& e) {
        Logger::error(QString("ToolChainExecutor::executeTool: OpenCV异常 in %1: %2")
            .arg(tool->name()).arg(QString::fromStdString(e.what())));
        result.ok = false;
        result.data["error"] = QString("OpenCV错误: %1").arg(QString::fromStdString(e.what()));
        success = false;
    } catch (const std::exception& e) {
        Logger::error(QString("ToolChainExecutor::executeTool: 标准异常 in %1: %2")
            .arg(tool->name()).arg(QString::fromStdString(e.what())));
        result.ok = false;
        result.data["error"] = QString("运行错误: %1").arg(QString::fromStdString(e.what()));
        success = false;
    }

    qint64 endTime = QDateTime::currentMSecsSinceEpoch();
    result.elapsedMs = endTime - startTime;

    // ===== spec 阶段一 Task 2：执行后坐标反变换 =====
    // 矩形 ROI：检测框/点坐标 += roiRect.tl()，overlayImage 贴回原图对应位置
    // 多边形 ROI：坐标不变（掩蔽不改坐标系），但过滤完全在 ROI 外的检测框
    if (useRectRoi) {
        const int dx = roiRect.x;
        const int dy = roiRect.y;
        offsetDetections(result, dx, dy);
        offsetPorts(result, dx, dy);
        // overlayImage 贴回原图对应位置（保证链式执行时下游算子收到完整原图）
        if (!result.overlayImage.empty()) {
            cv::Mat fullOverlay;
            // 确保贴回基准图与原图尺寸一致（BGR 三通道）
            if (input.channels() == 3) {
                fullOverlay = input.clone();
            } else if (input.channels() == 1) {
                cv::cvtColor(input, fullOverlay, cv::COLOR_GRAY2BGR);
            } else if (input.channels() == 4) {
                cv::cvtColor(input, fullOverlay, cv::COLOR_BGRA2BGR);
            } else {
                fullOverlay = input.clone();
            }
            cv::Mat roiOverlay = fullOverlay(roiRect);
            if (result.overlayImage.size() == roiRect.size()) {
                result.overlayImage.copyTo(roiOverlay);
            } else {
                // 算子可能 resize 了 overlayImage，缩放后贴回
                try {
                    cv::resize(result.overlayImage, roiOverlay, roiRect.size());
                } catch (const cv::Exception& e) {
                    Logger::warn(QString("ToolChainExecutor: overlayImage 贴回失败: %1").arg(e.what()));
                }
            }
            result.overlayImage = fullOverlay;
        }
    } else if (usePolygonRoi) {
        // 多边形 ROI：坐标不变，仅过滤完全在 ROI 外的检测框
        filterDetectionsByPolygon(result, polygonMask);
        // overlayImage 尺寸与原图一致（掩蔽不改尺寸），无需贴回
    }

    return success;
}

bool ToolChainExecutor::evaluateBranch(const BranchNode* branch) {
    if (!branch) {
        return true;
    }

    if (!m_results.contains(branch->sourceToolId)) {
        return true;
    }

    // P1-B3 修复：直接委托给 BranchNode::evaluate，支持 8 种操作符
    // (==, !=, >, >=, <, <=, ok, ng)，之前 executor 只实现 3 种导致 5 种永远 false
    const ToolResult& sourceResult = m_results[branch->sourceToolId];
    return branch->evaluate(sourceResult);
}

ToolResult ToolChainExecutor::getResult(const QString& toolId) const {
    QMutexLocker locker(&m_mutex);
    return m_results.value(toolId);
}

// v2.7.0：执行子链循环（对 ROI 列表逐个切片执行子链）
// 语义：LoopTool 自身已通过 executeTool 执行得到 roiList，
// 此处负责对每个 ROI 切片执行子链算子序列，聚合结果到 loopResult。
bool ToolChainExecutor::executeSubChain(VisionTool* loopTool, const cv::Mat& input,
                                         ToolResult& loopResult, const QDV::ExecutionContext& ctx) {
    if (!ctx.canRecurse()) {
        Logger::error(QStringLiteral("子链递归深度超限 (depth=%1)").arg(ctx.depth));
        return false;
    }

    // 从 loopResult 提取 roiList
    // roiList 格式：QJsonArray of {x, y, w, h}
    QVariantList rois;
    if (loopResult.data.contains(QStringLiteral("roiList"))) {
        // roiList 在 data 中是 QJsonArray，需转 QVariantList
        QJsonValue roiVal = loopResult.data.value(QStringLiteral("roiList"));
        if (roiVal.isArray()) {
            for (const QJsonValue& v : roiVal.toArray()) {
                rois.append(v.toVariant());
            }
        }
    }

    if (rois.isEmpty()) {
        Logger::warn(QStringLiteral("LoopTool %1 未产出 roiList，跳过子链执行").arg(loopTool->id()));
        return true;
    }

    // 查找子链
    auto it = m_subChains.find(loopTool->id());
    if (it == m_subChains.end() || it->isEmpty()) {
        Logger::warn(QStringLiteral("LoopTool %1 无子链配置").arg(loopTool->id()));
        return true;
    }
    QList<VisionTool*> subTools = it.value();

    QList<ToolResult> iterationResults;
    bool allSuccess = true;

    for (int i = 0; i < rois.size() && m_running; ++i) {
        emit subChainIterationStarted(loopTool->id(), i, rois.size());

        QVariantMap roiMap = rois[i].toMap();
        // 切片：从输入图像裁剪 ROI 区域
        cv::Mat roiInput;
        int rx = roiMap.value(QStringLiteral("x")).toInt();
        int ry = roiMap.value(QStringLiteral("y")).toInt();
        int rw = roiMap.value(QStringLiteral("w")).toInt();
        int rh = roiMap.value(QStringLiteral("h")).toInt();
        // 边界保护：ROI 必须落在输入图像范围内
        rx = std::max(0, std::min(rx, input.cols - 1));
        ry = std::max(0, std::min(ry, input.rows - 1));
        rw = std::max(1, std::min(rw, input.cols - rx));
        rh = std::max(1, std::min(rh, input.rows - ry));
        cv::Rect roiRect(rx, ry, rw, rh);
        roiInput = input(roiRect).clone();

        // 为每次迭代创建独立 Tool 实例（避免状态竞争，参考头文件 P1-B12 约束）
        QList<VisionTool*> iterTools;
        for (VisionTool* proto : subTools) {
            VisionTool* copy = ToolFactory::instance()->createTool(proto->type());
            if (copy) {
                copy->deserialize(proto->serialize());
                iterTools.append(copy);
            }
        }

        // 执行子链
        ExecutionContext childCtx = ctx.createChild(i, rois.size(), rois[i]);
        ToolResult iterResult = executeBranchSequence(iterTools, roiInput, childCtx);
        iterationResults.append(iterResult);
        if (!iterResult.ok) allSuccess = false;

        // 释放迭代工具实例
        qDeleteAll(iterTools);

        emit subChainIterationCompleted(loopTool->id(), i, iterResult);
    }

    // 聚合结果到 loopResult
    QJsonArray iterArray;
    for (const ToolResult& r : iterationResults) {
        iterArray.append(r.data);
    }
    loopResult.data[QStringLiteral("iterations")] = iterArray;
    loopResult.data[QStringLiteral("iterationCount")] = static_cast<int>(iterationResults.size());

    // 取最后一次迭代的 overlayImage 作为 loopResult 的 overlayImage
    // 简化处理：直接用最后迭代的 overlay，不贴回原图对应位置
    if (!iterationResults.isEmpty() && !iterationResults.last().overlayImage.empty()) {
        loopResult.overlayImage = iterationResults.last().overlayImage.clone();
    }

    // 存储迭代结果（供 getIterationResults 查询）
    {
        QMutexLocker locker(&m_mutex);
        m_iterationResults[loopTool->id()] = iterationResults;
    }

    return allSuccess;
}

// v2.7.0：执行单条分支（线性顺序执行分支内的算子）
// 语义：对 branchTools 按顺序执行，前一个算子的 overlayImage 作为下一个的输入，
// 每个算子的结果记录到 m_results，返回最后一个算子的结果。
// 注意：此方法可能由并行分支的独立 subExecutor 调用，subExecutor.m_running 默认 false，
// 因此用 m_running.load() 检查不可靠。改用主 executor 的停止状态判断。
ToolResult ToolChainExecutor::executeBranchSequence(const QList<VisionTool*>& branchTools,
                                                     const cv::Mat& input,
                                                     const QDV::ExecutionContext& ctx) {
    Q_UNUSED(ctx);
    ToolResult lastResult;
    lastResult.ok = false;
    cv::Mat currentInput = input.clone();

    // 子执行器场景：临时设置 m_running 为 true，执行结束后恢复
    const bool wasRunning = m_running.exchange(true);
    struct RunningGuard {
        std::atomic<bool>& flag;
        bool prev;
        ~RunningGuard() { flag.store(prev); }
    } guard{m_running, wasRunning};

    for (VisionTool* tool : branchTools) {
        if (!m_running.load()) break;

        ToolResult result;
        if (!executeTool(tool, currentInput, result)) {
            Logger::error(QStringLiteral("分支内工具执行失败: %1").arg(tool->name()));
            emit toolFailed(tool->id(), QStringLiteral("分支内工具执行失败: %1").arg(tool->name()));
            lastResult = result;
            break;
        }

        {
            QMutexLocker locker(&m_mutex);
            m_results[tool->id()] = result;
        }
        emit toolExecuted(tool->id(), result);

        if (!result.overlayImage.empty()) {
            currentInput = result.overlayImage.clone();
        }
        lastResult = result;

        // 分支内的子分支递归（如果有）
        // 注意：这里简化处理，不递归检查 branchesCopy，避免复杂度
    }

    return lastResult;
}

// v2.7.0：并行执行多分支并等待全部完成（FlowJoin 语义）
// 语义：每条分支在独立线程执行，使用独立 Tool 实例（避免状态竞争），
// 各分支独立维护自己的 m_results，最后聚合判断是否全部成功。
bool ToolChainExecutor::executeParallelBranches(const QList<QString>& branchIds,
                                                 const cv::Mat& input,
                                                 const QDV::ExecutionContext& ctx) {
    if (branchIds.isEmpty()) return true;

    // 为每条分支创建独立 Tool 实例（并行执行必须状态隔离）
    QList<QList<VisionTool*>> branchToolCopies;
    for (const QString& branchId : branchIds) {
        auto it = m_parallelBranches.find(branchId);
        if (it == m_parallelBranches.end() || it->isEmpty()) continue;

        QList<VisionTool*> copies;
        for (VisionTool* proto : it.value()) {
            VisionTool* copy = ToolFactory::instance()->createTool(proto->type());
            if (copy) {
                copy->deserialize(proto->serialize());
                copies.append(copy);
            }
        }
        branchToolCopies.append(copies);
    }

    // 并行执行（每条分支在独立线程，使用独立 Tool 实例）
    QList<ToolResult> results;
    results.resize(branchToolCopies.size());

    // 使用 QtConcurrent::blockingMap 并行执行
    QList<int> indices;
    for (int i = 0; i < branchToolCopies.size(); ++i) indices.append(i);

    QtConcurrent::blockingMap(indices, [&](int i) {
        // 每条分支创建独立 executor（避免 m_results 竞争）
        ToolChainExecutor subExecutor;
        ToolResult r = subExecutor.executeBranchSequence(branchToolCopies[i], input, ctx);
        results[i] = r;
    });

    // 释放分支工具实例
    for (const QList<VisionTool*>& copies : branchToolCopies) {
        qDeleteAll(copies);
    }

    // 检查所有分支是否成功
    bool allSuccess = true;
    for (const ToolResult& r : results) {
        if (!r.ok) allSuccess = false;
    }
    return allSuccess;
}