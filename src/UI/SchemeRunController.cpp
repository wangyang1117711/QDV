// ============================================================================
// SchemeRunController —— 方案运行控制器实现（从 EditViewBridge 拆分，Task 6）
// ----------------------------------------------------------------------------
// 逻辑与原 EditViewBridge 完全等价，仅将成员访问改为通过 EditViewBridge*
// 的 public 接口（currentNodes/connections/cameraFramePath/variableManager/
// imageVariableManager），并将信号改为本类自有信号（由 EditViewBridge 转发）。
// ============================================================================

#include "UI/SchemeRunController.h"
#include "UI/EditViewBridge.h"

#include "Vision/ToolFactory.h"
#include "Vision/ToolChainExecutor.h"
#include "Core/VisionTool.h"
#include "Core/Scheme.h"
#include "Core/SchemeManager.h"
#include "Core/Logger.h"
#include "Core/VariableManager.h"
#include "Core/ImageVariableManager.h"

#include <QFileInfo>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QUuid>
#include <QElapsedTimer>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QVariantMap>
#include <QMap>
#include <QSet>
#include <QRegularExpression>
#include <QtConcurrent>
#include <mutex>      // P0-4：std::once_flag 启动清理
#include <opencv2/opencv.hpp>
#include <opencv2/imgcodecs.hpp>

// v2.5.0 修复：健壮的图像加载（QFile 读取字节流 + cv::imdecode 解码）
// 替代 cv::imread，解决 Windows 上中文/特殊字符路径加载失败问题
namespace {
cv::Mat loadImageRobust(const QString& filePath, int flags = cv::IMREAD_COLOR) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return cv::Mat();
    }
    const QByteArray data = file.readAll();
    file.close();
    if (data.isEmpty()) {
        return cv::Mat();
    }
    return cv::imdecode(cv::Mat(1, data.size(), CV_8UC1,
                                const_cast<char*>(data.constData())), flags);
}
}  // namespace

SchemeRunController::SchemeRunController(EditViewBridge* bridge, QObject* parent)
    : QObject(parent), m_bridge(bridge)
{
    // P0-4（0906 优化）：一次性清理历史版本的 UUID 临时 PNG（qdv_single_XXXXXXXX /
    // qdv_deploy_XXXXXXXX / qdv_snapshot_*_before|after.png），以及本次会话之外
    // 不再被引用的快照文件。启动时执行一次，量级为 %TEMP% 中匹配文件数。
    static std::once_flag s_cleanupOnce;
    std::call_once(s_cleanupOnce, []() {
        const QString tempDir = QDir::tempPath();
        QDir dir(tempDir);
        const QStringList patterns = {
            QStringLiteral("qdv_single_*.png"),
            QStringLiteral("qdv_deploy_*.png"),
            QStringLiteral("qdv_snapshot_*.png"),
        };
        int removed = 0;
        for (const QString& pattern : patterns) {
            for (const QFileInfo& fi : dir.entryInfoList({pattern}, QDir::Files)) {
                if (QFile::remove(fi.absoluteFilePath())) ++removed;
            }
        }
        if (removed > 0) {
            QDV::Logger::info(QString("SchemeRunController: cleaned %1 legacy temp preview PNGs").arg(removed));
        }
    });
}

SchemeRunController::~SchemeRunController() = default;

// =====================================================================
// 内部辅助：构建工具链 / 计算上游链 / 保存临时 PNG
// =====================================================================
// P0-4（0906 优化）：临时 PNG 从 UUID 命名改为「前缀_toolId」确定性命名 ——
// 同一节点的每次执行覆盖写同一个文件，%TEMP% 不再无限累积（原实现每算子每
// 次执行新建 UUID 文件永不清理，长期使用积累数 GB）。toolId 为节点 UUID（
// hex 字符），直接可用作文件名；长度截断防爆路径。
// 另：QML 端 Image 已恢复 cache:true，同 URL 重复显示不再重新解码。
QString SchemeRunController::saveMatToTempPng(const cv::Mat& image, const QString& prefix) {
    if (image.empty()) return QString();
    const QString tempDir = QDir::tempPath();
    // P0-4：调用方把 toolId 编入 prefix（"qdv_single_<id>"）以获得确定性文件名。
    // 截断超长段并替换文件名非法字符，防路径越界。
    QString safePrefix = prefix;
    safePrefix.replace(QRegularExpression("[^A-Za-z0-9_\\-]"), "_");
    if (safePrefix.size() > 80) safePrefix = safePrefix.left(80);
    const QString fileName = QString("%1.png").arg(safePrefix);
    const QString filePath = QDir(tempDir).absoluteFilePath(fileName);

    // v5.2 修复：cv::imwrite 内部用 C 标准 fopen，在 Windows GBK 系统下对 UTF-8 中文路径
    // 会失败。改用 QFile + cv::imencode，与 ReadImageTool 的加载逻辑对称。
    std::vector<uchar> buf;
    if (!cv::imencode(".png", image, buf)) {
        QDV::Logger::error("SchemeRunController::saveMatToTempPng: cv::imencode failed");
        return QString();
    }
    QFile f(filePath);
    if (!f.open(QIODevice::WriteOnly)) {
        QDV::Logger::error(QString("SchemeRunController::saveMatToTempPng: cannot open %1 for writing").arg(filePath));
        return QString();
    }
    const qint64 written = f.write(reinterpret_cast<const char*>(buf.data()), static_cast<qint64>(buf.size()));
    f.close();
    if (written != static_cast<qint64>(buf.size())) {
        QDV::Logger::error(QString("SchemeRunController::saveMatToTempPng: short write to %1").arg(filePath));
        return QString();
    }
    return filePath;
}

QList<QDV::VisionTool*> SchemeRunController::buildToolChainFromNodes(const QStringList& nodeIds) const {
    QList<QDV::VisionTool*> tools;
    tools.reserve(nodeIds.size());

    const QVariantList nodes = m_bridge->currentNodes();
    QObject* vmObj = m_bridge->variableManager();
    QDV::VariableManager* vm = vmObj ? qobject_cast<QDV::VariableManager*>(vmObj) : nullptr;

    for (const QString& nodeId : nodeIds) {
        QVariantMap nodeData;
        bool found = false;
        for (const QVariant& v : nodes) {
            const QVariantMap n = v.toMap();
            if (n.value("id").toString() == nodeId) {
                nodeData = n;
                found = true;
                break;
            }
        }
        if (!found) {
            QDV::Logger::warn(QString("buildToolChainFromNodes: 节点 %1 未找到，跳过").arg(nodeId));
            continue;
        }

        const QString type = nodeData.value("type").toString();
        QDV::VisionTool* tool = ToolFactory::instance()->createTool(type);
        if (!tool) {
            QDV::Logger::error(QString("buildToolChainFromNodes: 无法创建算子 %1").arg(type));
            qDeleteAll(tools);
            return {};
        }

        // 设置参数（QVariantMap → QJsonObject → configure）
        const QVariantMap params = nodeData.value("params").toMap();
        // v2.6.0：解析参数中的 ${varName} 变量绑定
        QVariantMap resolvedParams = params;
        if (vm) {
            resolvedParams.clear();
            for (auto it = params.constBegin(); it != params.constEnd(); ++it) {
                resolvedParams[it.key()] = vm->resolveVariant(it.value());
            }
        }
        QJsonObject jsonParams;
        for (auto it = resolvedParams.constBegin(); it != resolvedParams.constEnd(); ++it) {
            jsonParams[it.key()] = QJsonValue::fromVariant(it.value());
        }

        // spec 阶段一 Task 4：通用 ROI 参数处理
        // UI 端 ParamForm 通过 "roi" 参数传递 ROI 字符串，此处解析为 QVariantMap 并调用 setRoi
        // 格式：
        //   矩形："x,y,w,h"（如 "100,50,200,150"）
        //   多边形："poly:x1,y1,x2,y2,..."（至少 3 个点）
        //   空字符串：无 ROI（全图，向后兼容）
        if (jsonParams.contains("roi")) {
            const QString roiStr = jsonParams.value("roi").toString().trimmed();
            QVariantMap roiMap;
            if (roiStr.startsWith(QStringLiteral("poly:"), Qt::CaseInsensitive)) {
                // 多边形 ROI 解析
                const QString body = roiStr.mid(5).trimmed();
                const QStringList parts = body.split(QRegularExpression(QStringLiteral("[,;\\s]+")),
                                                     Qt::SkipEmptyParts);
                QVariantList pts;
                for (int i = 0; i + 1 < parts.size(); i += 2) {
                    bool ok1 = false, ok2 = false;
                    int px = parts[i].toInt(&ok1);
                    int py = parts[i + 1].toInt(&ok2);
                    if (ok1 && ok2) {
                        QVariantMap pm;
                        pm["x"] = px;
                        pm["y"] = py;
                        pts.append(pm);
                    }
                }
                if (pts.size() >= 3) {
                    roiMap["type"] = QStringLiteral("polygon");
                    roiMap["points"] = pts;
                }
            } else if (!roiStr.isEmpty()) {
                // 矩形 ROI 解析 "x,y,w,h"
                const QStringList parts = roiStr.split(QRegularExpression(QStringLiteral("[,;\\s]+")),
                                                       Qt::SkipEmptyParts);
                if (parts.size() >= 4) {
                    bool ok1 = false, ok2 = false, ok3 = false, ok4 = false;
                    int rx = parts[0].toInt(&ok1);
                    int ry = parts[1].toInt(&ok2);
                    int rw = parts[2].toInt(&ok3);
                    int rh = parts[3].toInt(&ok4);
                    if (ok1 && ok2 && ok3 && ok4) {
                        roiMap["type"] = QStringLiteral("rect");
                        roiMap["x"] = rx;
                        roiMap["y"] = ry;
                        roiMap["w"] = rw;
                        roiMap["h"] = rh;
                    }
                }
            }
            if (roiMap.contains("type")) {
                tool->setRoi(roiMap);
                QDV::Logger::info(QString("buildToolChainFromNodes: 算子 %1 设置 ROI type=%2")
                    .arg(type).arg(roiMap.value("type").toString()));
            }
        }

        if (!tool->configure(jsonParams)) {
            QDV::Logger::error(QString("buildToolChainFromNodes: 算子 %1 参数配置失败，已跳过").arg(type));
            delete tool;
            qDeleteAll(tools);
            return {};
        }

        tool->setId(nodeId);
        tool->setName(type);
        tools.append(tool);
    }
    return tools;
}

void SchemeRunController::collectUpstreamNodes(const QString& nodeId, QStringList& result, QSet<QString>& visited) const {
    if (visited.contains(nodeId)) return;
    visited.insert(nodeId);

    const QVariantList conns = m_bridge->connections();
    for (const QVariant& v : conns) {
        const QVariantMap conn = v.toMap();
        if (conn.value("toId").toString() == nodeId) {
            const QString upstreamId = conn.value("fromId").toString();
            collectUpstreamNodes(upstreamId, result, visited);
        }
    }
    result.append(nodeId);
}

QStringList SchemeRunController::computeUpstreamChain(const QString& targetNodeId) const {
    QStringList result;
    QSet<QString> visited;
    collectUpstreamNodes(targetNodeId, result, visited);
    return result;
}

// =====================================================================
// 输入源解析
// =====================================================================
QString SchemeRunController::resolveInputImageForNode(const QString& nodeId) const {
    const QStringList chain = computeUpstreamChain(nodeId);
    if (chain.isEmpty()) return QString();

    const QVariantList nodes = m_bridge->currentNodes();
    for (const QString& id : chain) {
        for (const QVariant& v : nodes) {
            const QVariantMap n = v.toMap();
            if (n.value("id").toString() != id) continue;
            if (n.value("type").toString() != QStringLiteral("ReadImage")) continue;

            const QVariantMap params = n.value("params").toMap();
            const QString filePath = params.value("filePath").toString();
            if (!filePath.isEmpty() && QFile::exists(filePath)) {
                QDV::Logger::info(QString("resolveInputImageForNode: 命中 ReadImage 节点 %1，filePath=%2")
                                  .arg(id).arg(filePath));
                return filePath;
            }
        }
    }
    return QString();
}

bool SchemeRunController::isInputImageRequired(const QString& nodeId) const {
    QString nodeType;
    for (const QVariant& v : m_bridge->currentNodes()) {
        const QVariantMap n = v.toMap();
        if (n.value("id").toString() == nodeId) {
            nodeType = n.value("type").toString();
            break;
        }
    }
    if (nodeType.isEmpty()) {
        return true;  // 节点不存在时保守返回 true
    }
    static const QSet<QString> kNoImageRequiredTypes = {
        QStringLiteral("OpenFramegrabber"),
        QStringLiteral("GrabImage"),
        QStringLiteral("RobotPose"),
        QStringLiteral("HandEyeCalib"),
        QStringLiteral("ReadImage")
    };
    return !kNoImageRequiredTypes.contains(nodeType);
}

QVariantMap SchemeRunController::resolveRunInput(const QString& nodeId) const {
    QVariantMap result;
    const bool required = isInputImageRequired(nodeId);
    result["required"] = required;
    if (required) {
        const QString path = resolveInputImageForNode(nodeId);
        result["path"] = path;
        result["hint"] = path.isEmpty()
            ? QStringLiteral("上游无 ReadImage 节点或未配置图像，请手动选择输入源")
            : QStringLiteral("已自动解析上游 ReadImage 图像");
    } else {
        result["path"] = QString();
        result["hint"] = QStringLiteral("当前算子为数据源型算子，无需输入图像");
    }
    return result;
}

QVariantMap SchemeRunController::resolveSchemeRunInput() const {
    QVariantMap result;
    result["path"] = QString();

    const QVariantList nodes = m_bridge->currentNodes();
    QStringList nodeTypes;
    for (const QVariant& v : nodes) {
        nodeTypes.append(v.toMap().value("type").toString());
    }
    QDV::Logger::info(QStringLiteral("[resolveSchemeRunInput] nodes=%1").arg(nodeTypes.join(", ")));

    // 1) 优先查找 ReadImage 节点并返回其 filePath
    for (const QVariant& v : nodes) {
        const QVariantMap n = v.toMap();
        if (n.value("type").toString() != QStringLiteral("ReadImage")) continue;
        const QVariantMap params = n.value("params").toMap();
        const QString fp = params.value("filePath").toString();
        QDV::Logger::info(QStringLiteral("[resolveSchemeRunInput] ReadImage filePath=%1 exists=%2").arg(fp).arg(QFile::exists(fp)));
        if (!fp.isEmpty() && QFile::exists(fp)) {
            result["required"] = true;
            result["path"] = fp;
            result["hint"] = QStringLiteral("已自动解析 ReadImage 节点图像");
            QDV::Logger::info(QStringLiteral("[resolveSchemeRunInput] result: required=true path=%1").arg(fp));
            return result;
        }
    }

    // 2) 若方案包含相机类算子，则无需输入图像
    static const QSet<QString> kCameraSourceTypes = {
        QStringLiteral("OpenFramegrabber"),
        QStringLiteral("GrabImage")
    };
    for (const QVariant& v : nodes) {
        const QString type = v.toMap().value("type").toString();
        QDV::Logger::debug(QStringLiteral("[resolveSchemeRunInput] checking type=%1").arg(type));
        if (kCameraSourceTypes.contains(type)) {
            result["required"] = false;
            result["hint"] = QStringLiteral("方案包含相机数据源，无需选择输入图像");
            QDV::Logger::info(QStringLiteral("[resolveSchemeRunInput] result: required=false (camera source)"));
            return result;
        }
    }

    // 3) 其余情况需要用户选择
    result["required"] = true;
    result["hint"] = QStringLiteral("当前方案无 ReadImage/相机数据源，请手动选择输入图像");
    QDV::Logger::info(QStringLiteral("[resolveSchemeRunInput] result: required=true (manual)"));
    return result;
}

// =====================================================================
// 整链运行
// =====================================================================
QVariantMap SchemeRunController::runScheme(const QString& inputImagePath) {
    QVariantMap result;
    result["success"] = false;

    const QVariantList nodes = m_bridge->currentNodes();

    QObject* ivmObj = m_bridge->imageVariableManager();
    QDV::ImageVariableManager* ivm = ivmObj ? qobject_cast<QDV::ImageVariableManager*>(ivmObj) : nullptr;
    QObject* vmObj = m_bridge->variableManager();
    QDV::VariableManager* vm = vmObj ? qobject_cast<QDV::VariableManager*>(vmObj) : nullptr;

    // 确定输入图像路径（优先级 入参 > 链中 ReadImage > 相机帧）
    QString imagePath = inputImagePath;
    if (imagePath.isEmpty()) {
        for (const QVariant& v : nodes) {
            const QVariantMap n = v.toMap();
            if (n.value("type").toString() != QStringLiteral("ReadImage")) continue;
            const QVariantMap params = n.value("params").toMap();
            const QString fp = params.value("filePath").toString();
            if (!fp.isEmpty() && QFile::exists(fp)) {
                imagePath = fp;
                break;
            }
        }
    }
    if (imagePath.isEmpty()) {
        imagePath = m_bridge->cameraFramePath();
    }

    cv::Mat inputImage;
    if (imagePath.isEmpty()) {
        // v5.3.9 修复：若方案包含相机类算子，使用 1x1 占位图像跳过空输入校验
        bool hasCameraSource = false;
        for (const QVariant& v : nodes) {
            const QString type = v.toMap().value("type").toString();
            if (type == QStringLiteral("OpenFramegrabber") ||
                type == QStringLiteral("GrabImage")) {
                hasCameraSource = true;
                break;
            }
        }
        if (hasCameraSource) {
            inputImage = cv::Mat(1, 1, CV_8UC3, cv::Scalar(0, 0, 0));
        } else {
            result["error"] = QStringLiteral("未指定输入图像路径");
            emit errorRaised(QStringLiteral("runScheme"), QStringLiteral("未指定输入图像路径"));
            return result;
        }
    } else {
        QFileInfo fi(imagePath);
        if (!fi.exists()) {
            result["error"] = QStringLiteral("输入图像文件不存在：%1").arg(imagePath);
            emit errorRaised(QStringLiteral("runScheme"), result["error"].toString());
            return result;
        }
        inputImage = loadImageRobust(imagePath, cv::IMREAD_COLOR);
        if (inputImage.empty()) {
            result["error"] = QStringLiteral("无法加载图像：%1").arg(imagePath);
            emit errorRaised(QStringLiteral("runScheme"), result["error"].toString());
            return result;
        }
    }

    // 构建工具链（按节点顺序）
    QStringList nodeIds;
    for (const QVariant& v : nodes) {
        const QString id = v.toMap().value("id").toString();
        if (!id.isEmpty()) nodeIds.append(id);
    }
    if (nodeIds.isEmpty()) {
        result["error"] = QStringLiteral("当前方案无算子节点");
        emit errorRaised(QStringLiteral("runScheme"), result["error"].toString());
        return result;
    }

    QList<QDV::VisionTool*> tools = buildToolChainFromNodes(nodeIds);
    if (tools.isEmpty()) {
        result["error"] = QStringLiteral("构建工具链失败");
        emit errorRaised(QStringLiteral("runScheme"), result["error"].toString());
        return result;
    }

    QElapsedTimer timer;
    timer.start();

    ::ToolChainExecutor executor;
    executor.setTools(tools);

    // v2.7.0：注入分支节点
    if (Scheme* scheme = SchemeManager::instance()->currentScheme()) {
        auto branches = scheme->branches();
        if (!branches.isEmpty()) {
            executor.setBranches(branches);
        }
        auto subChains = scheme->subChainPtrs();
        if (!subChains.isEmpty()) {
            executor.setSubChains(subChains);
        }
        auto parallelBranches = scheme->parallelBranchPtrs();
        if (!parallelBranches.isEmpty()) {
            executor.setParallelBranches(parallelBranches);
        }
    }

    QVariantList toolResults;
    int successCount = 0;
    int failCount = 0;
    QString lastOutputPath;

    QObject::connect(&executor, &::ToolChainExecutor::toolExecuted,
        [this, &nodes, &ivm, &toolResults, &successCount, &lastOutputPath](const QString& toolId, const ::ToolResult& tr) {
            QVariantMap trMap;
            trMap["toolId"] = toolId;
            trMap["ok"] = tr.ok;
            trMap["elapsedMs"] = tr.elapsedMs;
            // v6.x：把运行时输出值（ports）同步到桥接层缓存，供变量管理面板展示
            m_bridge->setNodeOutputValues(toolId, tr.ports);
            if (!tr.overlayImage.empty()) {
                // P0-4：确定性文件名（同节点覆盖写），toolId 编入 prefix
                const QString path = saveMatToTempPng(tr.overlayImage, QString("qdv_deploy_%1").arg(toolId));
                trMap["outputImagePath"] = path;
                if (!path.isEmpty()) lastOutputPath = path;
                if (ivm && !path.isEmpty()) {
                    QString toolName = toolId;
                    for (const QVariant& v : nodes) {
                        const QVariantMap n = v.toMap();
                        if (n.value("id").toString() == toolId) {
                            toolName = n.value("type").toString();
                            break;
                        }
                    }
                    ivm->updateImageVariable(
                        toolId, toolName, path,
                        tr.overlayImage.cols, tr.overlayImage.rows, tr.overlayImage.channels());
                }
            }
            toolResults.append(trMap);
            if (tr.ok) successCount++;
        });
    QObject::connect(&executor, &::ToolChainExecutor::toolFailed,
        [&toolResults, &failCount](const QString& toolId, const QString& errMsg) {
            QVariantMap trMap;
            trMap["toolId"] = toolId;
            trMap["ok"] = false;
            trMap["errorMessage"] = errMsg;
            toolResults.append(trMap);
            failCount++;
        });

    const bool ok = executor.execute(inputImage);
    const qint64 elapsedMs = timer.elapsed();

    // v5.4：ToolChain 执行后更新已注册算子输出变量的值
    if (vm) {
        const QMap<QString, ToolResult> results = executor.getResults();
        for (auto it = results.constBegin(); it != results.constEnd(); ++it) {
            vm->updateOperatorOutputValues(it.key(), it.value().data);
        }
    }

    qDeleteAll(tools);

    result["success"] = ok;
    result["totalTools"] = nodeIds.size();
    result["successCount"] = successCount;
    result["failCount"] = failCount;
    result["elapsedMs"] = elapsedMs;
    result["outputImagePath"] = lastOutputPath;
    result["toolResults"] = toolResults;

    QDV::Logger::info(QString("runScheme: %1/%2 success, %3ms")
                          .arg(successCount).arg(nodeIds.size()).arg(elapsedMs));
    return result;
}

void SchemeRunController::runSchemeAsync(const QString& inputImagePath) {
    if (m_schemeRunning) {
        QDV::Logger::warn("runSchemeAsync: 上一次部署仍在执行，忽略新请求");
        return;
    }
    m_schemeRunning = true;

    if (!m_schemeWatcher) {
        m_schemeWatcher = new QFutureWatcher<QVariantMap>(this);
        connect(m_schemeWatcher, &QFutureWatcher<QVariantMap>::finished, this, [this]() {
            const QVariantMap result = m_schemeWatcher->result();
            m_schemeRunning = false;
            emit schemeDeployFinished(result);
        });
    }

    emit schemeDeployStarted();
    const QString path = inputImagePath;
    m_schemeWatcher->setFuture(QtConcurrent::run([this, path]() -> QVariantMap {
        return this->runScheme(path);
    }));
}

// =====================================================================
// 单算子运行
// =====================================================================
QVariantMap SchemeRunController::runSingleOperator(const QString& nodeId, const QString& inputImagePath) {
    QVariantMap result;
    result["success"] = false;

    const QVariantList nodes = m_bridge->currentNodes();

    QObject* ivmObj = m_bridge->imageVariableManager();
    QDV::ImageVariableManager* ivm = ivmObj ? qobject_cast<QDV::ImageVariableManager*>(ivmObj) : nullptr;
    QObject* vmObj = m_bridge->variableManager();
    QDV::VariableManager* vm = vmObj ? qobject_cast<QDV::VariableManager*>(vmObj) : nullptr;

    static const QSet<QString> kNoImageRequiredTypes = {
        QStringLiteral("OpenFramegrabber"),
        QStringLiteral("GrabImage"),
        QStringLiteral("RobotPose"),
        QStringLiteral("HandEyeCalib"),
        QStringLiteral("ReadImage")
    };
    QString targetNodeType;
    for (const QVariant& v : nodes) {
        const QVariantMap n = v.toMap();
        if (n.value("id").toString() == nodeId) {
            targetNodeType = n.value("type").toString();
            break;
        }
    }
    const bool isNoImageRequired = kNoImageRequiredTypes.contains(targetNodeType);

    // 确定输入图像路径
    QString imagePath = inputImagePath;
    if (imagePath.isEmpty()) {
        imagePath = resolveInputImageForNode(nodeId);
    }
    if (imagePath.isEmpty()) {
        imagePath = m_bridge->cameraFramePath();
    }

    cv::Mat inputImage;
    if (imagePath.isEmpty()) {
        if (isNoImageRequired) {
            inputImage = cv::Mat(1, 1, CV_8UC3, cv::Scalar(0, 0, 0));
        } else {
            result["error"] = QStringLiteral("未指定输入图像路径");
            emit errorRaised(QStringLiteral("runSingleOperator"), QStringLiteral("未指定输入图像路径"));
            return result;
        }
    } else {
        QFileInfo fi(imagePath);
        if (!fi.exists()) {
            result["error"] = QStringLiteral("输入图像文件不存在：%1").arg(imagePath);
            emit errorRaised(QStringLiteral("runSingleOperator"), result["error"].toString());
            return result;
        }
        inputImage = loadImageRobust(imagePath, cv::IMREAD_COLOR);
        if (inputImage.empty()) {
            result["error"] = QStringLiteral("无法加载图像：%1").arg(imagePath);
            emit errorRaised(QStringLiteral("runSingleOperator"), result["error"].toString());
            return result;
        }
    }

    const QStringList chain = computeUpstreamChain(nodeId);
    if (chain.isEmpty()) {
        result["error"] = QStringLiteral("未找到节点：%1").arg(nodeId);
        emit errorRaised(QStringLiteral("runSingleOperator"), result["error"].toString());
        return result;
    }

    const int upstreamCount = chain.size() - 1;

    QList<QDV::VisionTool*> tools = buildToolChainFromNodes(chain);
    if (tools.isEmpty()) {
        result["error"] = QStringLiteral("构建工具链失败");
        emit errorRaised(QStringLiteral("runSingleOperator"), result["error"].toString());
        return result;
    }

    QElapsedTimer timer;
    timer.start();

    ::ToolChainExecutor executor;
    executor.setTools(tools);

    QVariantList upstreamResults;
    QVariantMap targetResult;
    QString lastOutputPath;
    const QString targetToolId = chain.last();

    QObject::connect(&executor, &::ToolChainExecutor::toolExecuted,
        [this, &nodes, &ivm, &upstreamResults, &targetResult, &lastOutputPath, targetToolId]
        (const QString& toolId, const ::ToolResult& tr) {
            QVariantMap trMap;
            trMap["toolId"] = toolId;
            trMap["ok"] = tr.ok;
            trMap["elapsedMs"] = tr.elapsedMs;
            // v6.x：把运行时输出值（ports）同步到桥接层缓存，供变量管理面板展示
            m_bridge->setNodeOutputValues(toolId, tr.ports);
            if (!tr.overlayImage.empty()) {
                // P0-4：确定性文件名（同节点覆盖写），toolId 编入 prefix
                const QString path = saveMatToTempPng(tr.overlayImage, QString("qdv_single_%1").arg(toolId));
                trMap["outputImagePath"] = path;
                if (!path.isEmpty()) lastOutputPath = path;
                if (ivm && !path.isEmpty()) {
                    QString toolName = toolId;
                    for (const QVariant& v : nodes) {
                        const QVariantMap n = v.toMap();
                        if (n.value("id").toString() == toolId) {
                            toolName = n.value("type").toString();
                            break;
                        }
                    }
                    ivm->updateImageVariable(
                        toolId, toolName, path,
                        tr.overlayImage.cols, tr.overlayImage.rows, tr.overlayImage.channels());
                }
            }
            if (toolId == targetToolId) {
                targetResult = trMap;
            } else {
                upstreamResults.append(trMap);
            }
        });
    QObject::connect(&executor, &::ToolChainExecutor::toolFailed,
        [&upstreamResults, &targetResult, targetToolId](const QString& toolId, const QString& errMsg) {
            QVariantMap trMap;
            trMap["toolId"] = toolId;
            trMap["ok"] = false;
            trMap["errorMessage"] = errMsg;
            if (toolId == targetToolId) {
                targetResult = trMap;
            } else {
                upstreamResults.append(trMap);
            }
        });

    const bool ok = executor.execute(inputImage);
    const qint64 elapsedMs = timer.elapsed();

    if (vm) {
        const QMap<QString, ToolResult> results = executor.getResults();
        for (auto it = results.constBegin(); it != results.constEnd(); ++it) {
            vm->updateOperatorOutputValues(it.key(), it.value().data);
        }
    }

    qDeleteAll(tools);

    result["success"] = ok;
    result["upstreamCount"] = upstreamCount;
    result["elapsedMs"] = elapsedMs;
    result["outputImagePath"] = lastOutputPath;
    result["upstreamResults"] = upstreamResults;
    result["targetResult"] = targetResult;

    if (!ok && !result.contains("error")) {
        QString errMsg;
        if (targetResult.contains("errorMessage")) {
            errMsg = targetResult["errorMessage"].toString();
        } else if (targetResult.contains("error")) {
            errMsg = targetResult["error"].toString();
        }
        if (!errMsg.isEmpty()) {
            result["error"] = errMsg;
        }
    }

    QDV::Logger::info(QString("runSingleOperator: node=%1, upstream=%2, success=%3, %4ms")
                          .arg(nodeId).arg(upstreamCount).arg(ok).arg(elapsedMs));
    return result;
}

void SchemeRunController::runSingleOperatorAsync(const QString& nodeId, const QString& inputImagePath) {
    if (m_singleOpRunning) {
        return;
    }
    m_singleOpRunning = true;

    if (!m_singleOpWatcher) {
        m_singleOpWatcher = new QFutureWatcher<QVariantMap>(this);
        connect(m_singleOpWatcher, &QFutureWatcher<QVariantMap>::finished, this, [this]() {
            const QVariantMap result = m_singleOpWatcher->result();
            m_singleOpRunning = false;
            emit singleOperatorFinished(result);
        });
    }

    emit singleOperatorStarted();
    const QString nid = nodeId;
    const QString imgPath = inputImagePath;
    m_singleOpWatcher->setFuture(QtConcurrent::run([this, nid, imgPath]() -> QVariantMap {
        return this->runSingleOperator(nid, imgPath);
    }));
}
