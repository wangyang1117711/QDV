// ============================================================================
// ZeroShotDetectTool — 零样本检测算子实现（spec v2 阶段三 Task 8）
// 封装 zsu::Kit::infer() 为 VisionTool::execute()，结果走 Points 端口。
// ============================================================================

#include "Vision/ZeroShotDetectTool.h"
#include "Core/Logger.h"
// v2.0 阶段六 Task 15.2：推理结果缓存
#include "Core/InferenceCache.h"

// ZeroShotKit 完整实现头（含 zsu::Kit / ZeroShotResult / ZeroShotModelType）
#include "ZeroShotKit/ZeroShotKit.h"
#include "ZeroShotKit/ZeroShotTypes.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QByteArray>
#include <QVariantList>
#include <QVariantMap>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>

#include <algorithm>

// ----------------------------------------------------------------------------
// Mat → QJsonObject 编码（PNG 字节流 + 尺寸/通道/类型元数据）
// 用途：掩码/热力图等 Mat 数据无法直接放入 ToolResult.ports（QVariantMap），
//       统一编码为 JSON 数据字段，供变量管理/属性面板展示与后续算子复用。
// ----------------------------------------------------------------------------
namespace {

QJsonObject encodeMatToJson(const cv::Mat& img) {
    QJsonObject obj;
    if (img.empty()) {
        obj["valid"] = false;
        return obj;
    }
    std::vector<uchar> buf;
    // 先转 8UC3/8UC1 便于 imencode（兼容 16U 等非常规类型）
    cv::Mat enc = img;
    if (enc.depth() != CV_8U) {
        double mn, mx;
        cv::minMaxLoc(enc, &mn, &mx);
        if (enc.channels() == 1) {
            cv::Mat tmp;
            enc.convertTo(tmp, CV_8U, 255.0 / std::max(1.0, mx - mn),
                          -mn * 255.0 / std::max(1.0, mx - mn));
            enc = tmp;
        } else {
            cv::Mat tmp;
            enc.convertTo(tmp, CV_8UC3, 255.0 / std::max(1.0, mx - mn),
                          -mn * 255.0 / std::max(1.0, mx - mn));
            enc = tmp;
        }
    }
    if (enc.channels() == 1) {
        cv::imencode(".png", enc, buf);
    } else if (enc.channels() == 3) {
        cv::imencode(".png", enc, buf);
    } else if (enc.channels() == 4) {
        cv::imencode(".png", enc, buf);
    } else {
        obj["valid"] = false;
        return obj;
    }
    if (buf.empty()) {
        obj["valid"] = false;
        return obj;
    }
    obj["valid"] = true;
    obj["rows"] = img.rows;
    obj["cols"] = img.cols;
    obj["channels"] = img.channels();
    obj["type"] = img.type();
    obj["encoding"] = QStringLiteral("png");
    // 二进制字节流 base64 编码为字符串存储（QJsonValue 不支持 QByteArray）
    QByteArray raw(reinterpret_cast<const char*>(buf.data()), static_cast<int>(buf.size()));
    obj["data"] = QString::fromLatin1(raw.toBase64());
    return obj;
}

}  // namespace

using namespace QDV;

// ----------------------------------------------------------------------------
// 构造 / 析构
// ----------------------------------------------------------------------------
ZeroShotDetectTool::ZeroShotDetectTool() {
    m_name = QStringLiteral("零样本检测");
    // zsu::Kit 是 QObject 但不强制 parent；这里作为成员持有，析构时手动 delete
    // 模型加载后复用，避免每次 execute 重建（重建会丢失已加载模型，性能极差）
    m_kit = new zsu::Kit();
}

ZeroShotDetectTool::~ZeroShotDetectTool() {
    delete m_kit;
    m_kit = nullptr;
}

// ----------------------------------------------------------------------------
// 模型类型字符串 → zsu::ZeroShotModelType 枚举映射
// 返回 int（强转枚举），-1 表示不支持（如 LocateAnything）
// ----------------------------------------------------------------------------
int ZeroShotDetectTool::modelTypeFromString(const QString& str) {
    const QString s = str.trimmed().toLower();
    if (s == "anomalyclip")  return static_cast<int>(zsu::ZeroShotModelType::AnomalyCLIP);
    if (s == "groundingdino") return static_cast<int>(zsu::ZeroShotModelType::GroundingDINO);
    if (s == "mobilesam")    return static_cast<int>(zsu::ZeroShotModelType::MobileSAM);
    if (s == "patchcore")    return static_cast<int>(zsu::ZeroShotModelType::PatchCore);
    if (s == "openclip")     return static_cast<int>(zsu::ZeroShotModelType::OpenCLIP);
    // LocateAnything 当前 zsu::Kit 不支持，返回 -1 触发"未实现"错误路径
    if (s == "locateanything") return -1;
    return -1;
}

// ----------------------------------------------------------------------------
// 提示词串拆分：支持 "scratch . dent . rust" / "scratch.dent" / "scratch dent"
// ----------------------------------------------------------------------------
QStringList ZeroShotDetectTool::splitPrompts(const QString& prompts) {
    if (prompts.trimmed().isEmpty()) return {};
    // 用 "." 分隔后再用空白拆分，过滤空串
    QStringList parts = prompts.split('.', Qt::SkipEmptyParts);
    QStringList result;
    for (const QString& p : parts) {
        const QString trimmed = p.trimmed();
        if (!trimmed.isEmpty()) result << trimmed;
    }
    return result;
}

// ----------------------------------------------------------------------------
// configure：解析参数 + 加载模型（若路径/类型变化）
// ----------------------------------------------------------------------------
bool ZeroShotDetectTool::configure(const QJsonObject& params) {
    if (params.contains("modelType")) {
        setModelType(params["modelType"].toString());
    }
    if (params.contains("textPrompts")) {
        setTextPrompts(params["textPrompts"].toString());
    }
    if (params.contains("boxThreshold")) {
        setBoxThreshold(params["boxThreshold"].toDouble(0.25));
    }
    if (params.contains("detectionMode")) {
        setDetectionMode(params["detectionMode"].toString());
    }
    if (params.contains("modelPath")) {
        setModelPath(params["modelPath"].toString());
    }
    m_params = params;

    // LocateAnything 在 configure 阶段不报错（允许配置），实际 execute 时返回"未实现"
    // 这样用户可以保存方案，等后续 zsu::Kit 支持后立即可用
    const int mtInt = modelTypeFromString(m_modelTypeStr);
    if (mtInt < 0 && m_modelTypeStr.toLower() == "locateanything") {
        Logger::info("ZeroShotDetectTool: modelType=LocateAnything 配置已保存，"
                     "但 zsu::Kit 当前不支持，execute 将返回未实现错误");
        return true;  // 配置成功，运行时返回错误
    }
    if (mtInt < 0) {
        Logger::warn("ZeroShotDetectTool: 未知 modelType=" + m_modelTypeStr);
        return false;
    }

    // 配置提示词与阈值到 Kit（即使模型未加载也先设置，加载后立即生效）
    if (m_kit) {
        m_kit->setTextPrompts(splitPrompts(m_textPrompts));
        m_kit->setAnomalyThreshold(static_cast<float>(m_boxThreshold));
        m_kit->setDetectionThreshold(static_cast<float>(m_boxThreshold));
    }

    // 模型路径或类型变化时重新加载
    const bool pathChanged  = (m_loadedModelPath != m_modelPath);
    const bool typeChanged  = (m_loadedModelTypeStr != m_modelTypeStr);
    if (!m_modelPath.isEmpty() && (pathChanged || typeChanged) && m_kit) {
        const auto mt = static_cast<zsu::ZeroShotModelType>(mtInt);
        Logger::info("ZeroShotDetectTool: 加载模型 type=" + m_modelTypeStr +
                     " path=" + m_modelPath);
        if (m_kit->loadModel(mt, m_modelPath)) {
            m_loadedModelPath = m_modelPath;
            m_loadedModelTypeStr = m_modelTypeStr;
            Logger::info("ZeroShotDetectTool: 模型加载成功");
        } else {
            Logger::error("ZeroShotDetectTool: 模型加载失败 path=" + m_modelPath);
            // 加载失败不阻塞 configure 返回值，execute 时再次检查
        }
    }
    return true;
}

// ----------------------------------------------------------------------------
// execute：执行零样本推理，转换结果到 ToolResult
// ----------------------------------------------------------------------------
bool ZeroShotDetectTool::execute(const cv::Mat& input, ToolResult& result) {
    if (input.empty()) {
        Logger::warn("ZeroShotDetectTool: 输入图像为空");
        result.ok = false;
        result.data["error"] = "Input image is empty";
        return false;
    }

    if (!m_kit) {
        result.ok = false;
        result.data["error"] = "ZeroShot Kit not initialized";
        return false;
    }

    // --- LocateAnything 诚实处理：zsu::Kit 当前不支持，返回明确"未实现"错误 ---
    if (m_modelTypeStr.toLower() == "locateanything") {
        result.ok = false;
        result.data["error"] = QStringLiteral(
            "LocateAnything 模型类型尚未实现，请使用 AnomalyCLIP/GroundingDINO/MobileSAM/PatchCore");
        result.data["modelType"] = m_modelTypeStr;
        Logger::warn("ZeroShotDetectTool: LocateAnything 未实现，返回错误");
        return false;
    }

    // --- 模型类型校验 ---
    const int mtInt = modelTypeFromString(m_modelTypeStr);
    if (mtInt < 0) {
        result.ok = false;
        result.data["error"] = QStringLiteral("Unknown modelType: %1").arg(m_modelTypeStr);
        return false;
    }

    // --- 模型加载（若未加载或路径变化） ---
    const bool needReload = !m_kit->isModelLoaded() ||
                            (m_loadedModelPath != m_modelPath) ||
                            (m_loadedModelTypeStr != m_modelTypeStr);
    if (needReload) {
        if (m_modelPath.isEmpty()) {
            result.ok = false;
            result.data["error"] = "Model path is empty";
            return false;
        }
        const auto mt = static_cast<zsu::ZeroShotModelType>(mtInt);
        if (!m_kit->loadModel(mt, m_modelPath)) {
            result.ok = false;
            result.data["error"] = QStringLiteral("Failed to load model: %1").arg(m_modelPath);
            result.data["modelLoaded"] = false;
            return false;
        }
        m_loadedModelPath = m_modelPath;
        m_loadedModelTypeStr = m_modelTypeStr;
    }
    result.data["modelLoaded"] = true;

    // --- 同步提示词与阈值（防止 configure 后用户修改参数未触发 configure） ---
    m_kit->setTextPrompts(splitPrompts(m_textPrompts));
    m_kit->setAnomalyThreshold(static_cast<float>(m_boxThreshold));
    m_kit->setDetectionThreshold(static_cast<float>(m_boxThreshold));

    // --- v2.0 阶段六 Task 15.2：查询推理结果缓存 ---
    // 缓存键：图像内容哈希 + 提示词 + ROI 哈希 + 模型类型 + 模型路径
    // 命中时跳过推理，直接返回缓存结果（同图同参数重复执行场景显著加速）
    QDV::InferenceCache* cache = QDV::InferenceCache::instance();
    if (cache->lookup(input, m_textPrompts, m_roi, m_modelTypeStr, m_modelPath, result)) {
        Logger::info(QStringLiteral("ZeroShotDetectTool: 缓存命中，跳过推理 (key 含模型=%1)")
                         .arg(m_modelTypeStr));
        // 缓存命中：result 已由 lookup 写入，记录最近结果后直接返回
        m_results["lastDetectionCount"] = result.ports.value("numDetections").toInt();
        m_results["lastMaxConfidence"]  = result.score;
        m_results["lastCacheHit"] = true;
        return true;
    }

    // --- 调用 zsu::Kit::infer ---
    const zsu::ZeroShotResult zsuResult = m_kit->infer(input);

    // --- 转换 ZeroShotResult → ToolResult ---
    result.ok = zsuResult.success;
    result.data = zsuResult.toJson();
    // 总耗时（preprocess+inference+postprocess）作为 elapsedMs
    result.elapsedMs = zsuResult.metrics.totalMs;
    result.data["modelType"] = m_modelTypeStr;
    result.data["detectionMode"] = m_detectionMode;
    result.data["textPrompts"] = m_textPrompts;
    result.data["boxThreshold"] = m_boxThreshold;

    if (!zsuResult.success) {
        result.data["error"] = zsuResult.errorMessage;
        Logger::warn("ZeroShotDetectTool: 推理失败 - " + zsuResult.errorMessage);
        return false;
    }

    // --- detections[] 转 Points 端口（归一化坐标 → 像素坐标） ---
    const int imgW = input.cols;
    const int imgH = input.rows;
    QVariantList detList;
    double maxConf = 0.0;
    for (const auto& d : zsuResult.detections) {
        QVariantMap det;
        // 归一化坐标 cx/cy/w/h → 像素坐标
        det["cx"]         = static_cast<double>(d.cx) * imgW;
        det["cy"]         = static_cast<double>(d.cy) * imgH;
        det["w"]          = static_cast<double>(d.w)  * imgW;
        det["h"]          = static_cast<double>(d.h)  * imgH;
        det["confidence"] = static_cast<double>(d.confidence);
        det["classId"]    = d.classId;
        det["className"]  = d.className;
        detList.append(det);
        if (d.confidence > maxConf) maxConf = d.confidence;
    }
    result.ports["detections"] = detList;
    result.ports["numDetections"] = static_cast<int>(zsuResult.detections.size());
    result.score = maxConf;
    result.data["num_detections"] = static_cast<int>(zsuResult.detections.size());
    result.data["max_confidence"] = maxConf;
    result.data["pass"] = !zsuResult.detections.empty();

    // --- 完整输出参数：补充分类/异常/掩码/元数据等实际输出 ---
    // 检测模型取最高检测置信度，分类模型取 zsuResult.confidence
    const double confValue = (maxConf > 0.0) ? maxConf : zsuResult.confidence;
    result.ports["category"]      = zsuResult.category;
    result.ports["confidence"]    = confValue;
    result.ports["anomalyScore"]  = zsuResult.anomalyScore;
    result.ports["imageName"]     = zsuResult.imageName;
    result.ports["latencyMs"]     = static_cast<qint64>(zsuResult.metrics.totalMs);
    result.data["category"]       = zsuResult.category;
    result.data["anomaly_score"]  = zsuResult.anomalyScore;
    result.data["latency_ms"]     = zsuResult.metrics.totalMs;

    // --- Mat 输出编码（掩码/异常热力图） ---
    // cv::Mat 无法直接放入 ToolResult.ports（QVariantMap 类型约束），统一用
    // imencode 编码为 JSON 数据字段，供变量管理/属性面板展示与下游算子复用。
    // 同时写入 result.ports，保证变量管理面板（读取 ports）能展示掩码/热力图输出。
    result.data["mask"]       = encodeMatToJson(zsuResult.mask);
    result.data["anomalyMap"] = encodeMatToJson(zsuResult.anomalyMap);
    result.ports["mask"]       = encodeMatToJson(zsuResult.mask);
    result.ports["anomalyMap"] = encodeMatToJson(zsuResult.anomalyMap);

    // --- 绘制 overlayImage（检测框 + 标签 + 置信度） ---
    if (!zsuResult.detections.empty()) {
        cv::Mat overlay = input.clone();
        // 转 QList<QVariantMap> 给 drawDetections
        QList<QVariantMap> dets;
        for (const auto& v : detList) dets << v.toMap();
        drawDetections(overlay, dets);
        result.overlayImage = overlay;
    } else {
        // 无检测：透传原图副本，便于上层显示
        result.overlayImage = input.clone();
    }

    // 记录最近检测结果（供外部查询）
    m_results["lastDetectionCount"] = static_cast<int>(zsuResult.detections.size());
    m_results["lastMaxConfidence"] = maxConf;
    m_results["lastCacheHit"] = false;

    // --- v2.0 阶段六 Task 15.2：写入推理结果缓存 ---
    // 仅缓存成功结果（insert 内部已检查 result.ok）
    // 缓存键与 lookup 一致：图像哈希 + 提示词 + ROI 哈希 + 模型类型 + 模型路径
    cache->insert(input, m_textPrompts, m_roi, m_modelTypeStr, m_modelPath, result);
    return true;
}

// ----------------------------------------------------------------------------
// 端口声明
// ----------------------------------------------------------------------------
QList<PortDescriptor> ZeroShotDetectTool::outputPorts() const {
    QList<PortDescriptor> ports;
    PortDescriptor det;
    det.name   = "detections";
    det.cnName = QStringLiteral("检测结果");
    det.type   = PortType::Points;
    det.dir    = PortDirection::Out;
    det.desc   = QStringLiteral("检测框数组（每项含 cx/cy/w/h/confidence/classId/className，像素坐标）");
    ports << det;

    PortDescriptor num;
    num.name   = "numDetections";
    num.cnName = QStringLiteral("检测数量");
    num.type   = PortType::Number;
    num.dir    = PortDirection::Out;
    num.desc   = QStringLiteral("检测到的目标数量");
    ports << num;

    // 补充输出端口（与 execute() 填充的 ports 保持一致；掩码/热力图走 overlayImage 通道）
    PortDescriptor cat;
    cat.name   = "category";
    cat.cnName = QStringLiteral("分类类别");
    cat.type   = PortType::String;
    cat.dir    = PortDirection::Out;
    cat.desc   = QStringLiteral("分类模型输出的类别名称（AnomalyCLIP/OpenCLIP）");
    ports << cat;

    PortDescriptor conf;
    conf.name   = "confidence";
    conf.cnName = QStringLiteral("置信度");
    conf.type   = PortType::Number;
    conf.dir    = PortDirection::Out;
    conf.desc   = QStringLiteral("分类置信度或最高检测置信度");
    ports << conf;

    PortDescriptor anomaly;
    anomaly.name   = "anomalyScore";
    anomaly.cnName = QStringLiteral("异常分数");
    anomaly.type   = PortType::Number;
    anomaly.dir    = PortDirection::Out;
    anomaly.desc   = QStringLiteral("异常检测分数（[0,1]，越高越异常）");
    ports << anomaly;

    PortDescriptor imgName;
    imgName.name   = "imageName";
    imgName.cnName = QStringLiteral("图像文件名");
    imgName.type   = PortType::String;
    imgName.dir    = PortDirection::Out;
    imgName.desc   = QStringLiteral("输入原图像文件名");
    ports << imgName;

    // 分割掩码（MobileSAM）与异常热力图（AnomalyCLIP/PatchCore）：
    // Mat 数据不落入 typed ports（设计约束见 ToolResult 注释），以 Image 端口声明供连线期类型校验。
    // 实际像素数据写入 result.data["mask"]/["anomalyMap"]（imencode 编码）。
    PortDescriptor mask;
    mask.name   = "mask";
    mask.cnName = QStringLiteral("分割掩码");
    mask.type   = PortType::Image;
    mask.dir    = PortDirection::Out;
    mask.desc   = QStringLiteral("分割模型输出的二值掩码图（MobileSAM），数据见 result.data.mask");
    ports << mask;

    PortDescriptor anomalyMap;
    anomalyMap.name   = "anomalyMap";
    anomalyMap.cnName = QStringLiteral("异常热力图");
    anomalyMap.type   = PortType::Image;
    anomalyMap.dir    = PortDirection::Out;
    anomalyMap.desc   = QStringLiteral("像素级异常热力图（AnomalyCLIP/PatchCore），数据见 result.data.anomalyMap");
    ports << anomalyMap;

    PortDescriptor latency;
    latency.name   = "latencyMs";
    latency.cnName = QStringLiteral("推理耗时");
    latency.type   = PortType::Number;
    latency.dir    = PortDirection::Out;
    latency.desc   = QStringLiteral("单次推理总耗时（毫秒）");
    ports << latency;
    return ports;
}

QList<PortDescriptor> ZeroShotDetectTool::inputPorts() const {
    QList<PortDescriptor> ports;
    PortDescriptor img;
    img.name   = "image";
    img.cnName = QStringLiteral("输入图像");
    img.type   = PortType::Image;
    img.dir    = PortDirection::In;
    img.desc   = QStringLiteral("待检测的输入图像（可由上游算子或 ROI 裁剪提供）");
    ports << img;
    return ports;
}

// ----------------------------------------------------------------------------
// 序列化 / 反序列化（含参数）
// ----------------------------------------------------------------------------
QJsonObject ZeroShotDetectTool::serialize() const {
    QJsonObject obj = VisionTool::serialize();
    obj["modelType"]     = m_modelTypeStr;
    obj["textPrompts"]   = m_textPrompts;
    obj["boxThreshold"]  = m_boxThreshold;
    obj["detectionMode"] = m_detectionMode;
    obj["modelPath"]     = m_modelPath;
    return obj;
}

bool ZeroShotDetectTool::deserialize(const QJsonObject& data) {
    if (!VisionTool::deserialize(data)) return false;
    if (data.contains("modelType"))     m_modelTypeStr  = data["modelType"].toString();
    if (data.contains("textPrompts"))   m_textPrompts   = data["textPrompts"].toString();
    if (data.contains("boxThreshold"))  m_boxThreshold  = data["boxThreshold"].toDouble(0.25);
    if (data.contains("detectionMode")) m_detectionMode = data["detectionMode"].toString();
    if (data.contains("modelPath"))     m_modelPath     = data["modelPath"].toString();
    // 用解析后的参数重新 configure（触发模型加载与提示词同步）
    return configure(data);
}

// ----------------------------------------------------------------------------
// 绘制检测框到 overlay（像素坐标，含标签 + 置信度）
// ----------------------------------------------------------------------------
void ZeroShotDetectTool::drawDetections(cv::Mat& overlay,
                                        const QList<QVariantMap>& detections) const {
    // 按类别分配颜色（与 YoloDetectTool 风格一致）
    static const cv::Scalar kColors[] = {
        cv::Scalar(0, 0, 255),     // 红
        cv::Scalar(0, 255, 0),     // 绿
        cv::Scalar(255, 0, 0),     // 蓝
        cv::Scalar(0, 255, 255),   // 黄
        cv::Scalar(255, 0, 255),   // 紫
        cv::Scalar(255, 255, 0),   // 青
        cv::Scalar(128, 0, 0),     // 深蓝
        cv::Scalar(0, 128, 0),     // 深绿
    };
    const int numColors = sizeof(kColors) / sizeof(kColors[0]);

    for (const auto& det : detections) {
        const int classId = det.value("classId").toInt();
        const double conf = det.value("confidence").toDouble();
        const double cx = det.value("cx").toDouble();
        const double cy = det.value("cy").toDouble();
        const double w  = det.value("w").toDouble();
        const double h  = det.value("h").toDouble();

        // 中心点 + 宽高 → 左上角 + 右下角
        int x1 = static_cast<int>(cx - w / 2.0);
        int y1 = static_cast<int>(cy - h / 2.0);
        int x2 = static_cast<int>(cx + w / 2.0);
        int y2 = static_cast<int>(cy + h / 2.0);
        // 钳制到图像边界
        x1 = std::max(0, std::min(x1, overlay.cols - 1));
        y1 = std::max(0, std::min(y1, overlay.rows - 1));
        x2 = std::max(0, std::min(x2, overlay.cols - 1));
        y2 = std::max(0, std::min(y2, overlay.rows - 1));

        const cv::Scalar color = kColors[std::abs(classId) % numColors];
        cv::rectangle(overlay, cv::Point(x1, y1), cv::Point(x2, y2), color, 2);

        // 标签：className + 置信度
        QString className = det.value("className").toString();
        if (className.isEmpty()) {
            className = QStringLiteral("Class_%1").arg(classId);
        }
        const QString label = QStringLiteral("%1: %2%")
            .arg(className).arg(QString::number(conf * 100, 'f', 1));
        const std::string labelStr = label.toStdString();

        int baseline = 0;
        const double fontScale = 0.5;
        const int thickness = 1;
        const cv::Size textSize = cv::getTextSize(labelStr, cv::FONT_HERSHEY_SIMPLEX,
                                                   fontScale, thickness, &baseline);
        const int textY = std::max(y1 - textSize.height - 4, 0);
        cv::rectangle(overlay, cv::Point(x1, textY),
                      cv::Point(x1 + textSize.width + 4, textY + textSize.height + 4),
                      color, cv::FILLED);
        cv::putText(overlay, labelStr, cv::Point(x1 + 2, textY + textSize.height),
                    cv::FONT_HERSHEY_SIMPLEX, fontScale, cv::Scalar(0, 0, 0), thickness);
    }
}
