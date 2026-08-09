#include "Vision/AiClassifyTool.h"
#include "Core/Logger.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <opencv2/imgproc.hpp>

using namespace QDV;

// 与训练推理模块 (ModelManager) 一致的标签文件查找逻辑：
// 依次查找 <basename>_labels.json / labels.json，解析 {"labels": [...]}。
// 返回空列表表示未找到或解析失败。
static QStringList loadLabelsFromModelDir(const QString& modelPath) {
    QFileInfo modelInfo(modelPath);
    if (!modelInfo.exists()) {
        return QStringList();
    }
    QDir modelDir = modelInfo.absoluteDir();
    QString baseName = modelInfo.completeBaseName();
    QStringList candidates;
    candidates << baseName + "_labels.json" << "labels.json";

    for (const QString& labelsFileName : candidates) {
        QString labelsPath = modelDir.absoluteFilePath(labelsFileName);
        if (!QFile::exists(labelsPath)) {
            continue;
        }
        QFile labelsFile(labelsPath);
        if (!labelsFile.open(QIODevice::ReadOnly)) {
            Logger::warn("AiClassifyTool: 无法打开 Labels 文件: " + labelsPath);
            continue;
        }
        QJsonParseError parseErr;
        QJsonDocument doc = QJsonDocument::fromJson(labelsFile.readAll(), &parseErr);
        labelsFile.close();

        if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
            Logger::warn("AiClassifyTool: Labels 文件解析失败: " + labelsPath
                         + ", error: " + parseErr.errorString());
            continue;
        }

        QJsonArray labelsArray = doc.object().value("labels").toArray();
        QStringList labels;
        labels.reserve(labelsArray.size());
        for (const QJsonValue& labelValue : labelsArray) {
            labels.append(labelValue.toString());
        }
        if (labels.isEmpty()) {
            Logger::warn("AiClassifyTool: Labels 文件为空数组: " + labelsPath);
            continue;
        }
        Logger::info("AiClassifyTool: 已从模型目录加载类别标签: " + labelsPath
                     + " (" + QString::number(labels.size()) + " 类)");
        return labels;
    }
    return QStringList();
}

AiClassifyTool::AiClassifyTool() {
    m_name = "AI分类";
    // Phase 1: m_engine 由 setInferenceEngine 外部注入，不再 new InferenceEngine
}

AiClassifyTool::~AiClassifyTool() {
    // Phase 1: m_engine 所有权归外部（注入方），不 delete
}

bool AiClassifyTool::configure(const QJsonObject& params) {
    if (params.contains("modelPath")) {
        setModelPath(params["modelPath"].toString());
    }
    if (params.contains("confidenceThreshold")) {
        setConfidenceThreshold(params["confidenceThreshold"].toDouble(0.5));
    }
    if (params.contains("topK")) {
        setTopK(params["topK"].toInt(3));
    }
    if (params.contains("inputWidth") && params.contains("inputHeight")) {
        setInputSize(params["inputWidth"].toInt(224), params["inputHeight"].toInt(224));
    }
    if (params.contains("categoryLabels")) {
        QJsonArray arr = params["categoryLabels"].toArray();
        QStringList labels;
        for (const auto& v : arr) {
            labels.append(v.toString());
        }
        setCategoryLabels(labels);
    }

    // 解析输出开关配置（v5.4 升级：每个输出可独立启用/禁用）
    // 格式：params["outputConfig"] = { {"className", {{"enabled", true}}},
    //                                  {"confidence", {{"enabled", false}}}, ... }
    // 默认值 true 是为了向后兼容（旧节点没有 outputConfig 时，所有输出视为启用）
    if (params.contains("outputConfig") && params["outputConfig"].isObject()) {
        QJsonObject oc = params["outputConfig"].toObject();
        m_outputConfig.clear();
        for (const QString& key : oc.keys()) {
            QJsonObject item = oc[key].toObject();
            m_outputConfig[key] = item["enabled"].toBool(true);
        }
    }

    m_params = params;

    // Phase 1: 增加 m_engine nullptr 检查（RT-006）
    if (!m_modelPath.isEmpty() && !m_warmedUp && m_engine) {
        Logger::info("AiClassifyTool loading model: " + m_modelPath);
        bool loaded = m_engine->loadModel(m_modelPath,
            QSize(m_inputWidth, m_inputHeight),
            cv::Scalar(0.485, 0.456, 0.406),       // ImageNet mean
            1.0 / 255.0,                            // scale = 1/255
            true,                                    // swapRB = BGR→RGB
            cv::Scalar(0.229, 0.224, 0.225)         // ImageNet std
        );
        if (loaded) {
            m_engine->warmUp(3);
            m_warmedUp = true;
            Logger::info("AiClassifyTool model warmed up successfully");

            // 一致性修复：自动从模型同目录读取 labels.json（与训练推理模块 ModelManager 一致），
            // 作为类别标签的权威来源。用户手动配置的 categoryLabels 优先级更高：
            // 仅当用户未配置非空标签时，才以 labels.json 为准，并同步注入 engine。
            ensureCategoryLabels();
        } else {
            Logger::error("AiClassifyTool failed to load model: " + m_modelPath);
            return false;
        }
    }

    return true;
}

bool AiClassifyTool::ensureCategoryLabels() {
    // 一致性修复：类别标签以模型目录 labels.json 为权威来源（与训练推理模块 ModelManager 一致），
    // 彻底消除因手动配置顺序与模型训练顺序不一致导致的类别映射错误。
    QStringList fileLabels = loadLabelsFromModelDir(m_modelPath);
    if (!fileLabels.isEmpty()) {
        m_categoryLabels = fileLabels;
        if (m_engine) {
            m_engine->setCategoryLabels(fileLabels);
        }
        Logger::info("AiClassifyTool: 应用模型 labels.json 权威类别标签 ("
                     + fileLabels.join(", ") + ")");
        return true;
    }
    // 模型目录无 labels.json 时，回退到用户手动配置的 categoryLabels（同步注入 engine）
    if (!m_categoryLabels.isEmpty()) {
        if (m_engine) {
            m_engine->setCategoryLabels(m_categoryLabels);
        }
        Logger::info("AiClassifyTool: 使用用户配置的类别标签 ("
                     + QString::number(m_categoryLabels.size()) + " 类)");
        return true;
    }
    Logger::warn("AiClassifyTool: 未找到类别标签（模型目录无 labels.json 且用户未配置）");
    return false;
}

bool AiClassifyTool::execute(const cv::Mat& input, ToolResult& result) {
    // RT-010 修复：对整个 execute() 加锁，保证并发调用的线程安全。
    // 之前存在两个竞态：(1) m_warmedUp 检查-设置非原子，多线程重复 loadModel/warmUp；
    // (2) m_results（QJsonObject）并发写入触发 detach 竞争，导致段错误 0xC0000005。
    // AI 推理工具的真正并发应通过多实例或批处理实现，单实例串行化是可接受的。
    QMutexLocker locker(&m_execMutex);

    if (input.empty()) {
        Logger::warn("AiClassifyTool: empty input");
        result.ok = false;
        return false;
    }

    // Phase 1 RT-006: 引擎未注入保护
    if (!m_engine) {
        Logger::warn("AiClassifyTool: no inference engine injected");
        result.ok = false;
        result.data["error"] = "No inference engine injected";
        result.data["modelLoaded"] = false;  // v5.3：供 ToolChainVerifier 判断预期跳过
        return false;
    }

    if (m_modelPath.isEmpty()) {
        Logger::warn("AiClassifyTool: no model configured");
        result.ok = false;
        result.data["error"] = "No model configured";
        result.data["modelLoaded"] = false;
        return false;
    }

    if (!m_warmedUp && m_engine) {
        Logger::info("AiClassifyTool: loading model before first inference");
        bool loaded = m_engine->loadModel(m_modelPath,
            QSize(m_inputWidth, m_inputHeight),
            cv::Scalar(0.485, 0.456, 0.406),
            1.0 / 255.0,
            true);
        if (loaded) {
            m_engine->warmUp(3);
            m_warmedUp = true;
        } else {
            result.ok = false;
            result.data["error"] = "Failed to load model";
            result.data["modelLoaded"] = false;
            return false;
        }
    }
    result.data["modelLoaded"] = true;  // v5.3：模型已加载标记

    QJsonObject inferResult;
    bool ok = m_engine->infer(input, inferResult);

    if (!ok) {
        Logger::error("AiClassifyTool: inference failed");
        result.ok = false;
        // inferResult 中可能已包含引擎返回的具体错误信息（如 OpenCV DNN forward error）
        if (inferResult.contains("error") && !inferResult["error"].toString().isEmpty()) {
            result.data["error"] = inferResult["error"].toString();
            result.data["errorDetail"] = inferResult["error"].toString();
        } else {
            result.data["error"] = "Inference failed";
        }
        return false;
    }

    double confidence = inferResult["confidence"].toDouble();
    result.score = confidence;
    result.ok = (confidence >= m_confidenceThreshold);

    result.data = inferResult;

    // 写入 classId / className / confidence（兼容 class_index / category 等多种字段名）
    int classId = inferResult["classId"].toInt(inferResult["class_index"].toInt(-1));
    QString className;
    if (classId >= 0 && classId < m_categoryLabels.size()) {
        className = m_categoryLabels[classId];
    } else {
        className = inferResult["className"].toString(inferResult["category"].toString());
    }
    // v5.4：按 outputConfig 过滤写入（topK 始终保留用于叠加图绘制，不受开关控制）
    // score 字段（line 129 附近）保留用于分支判断，不受开关影响
    // 注意：line 146 的 result.data = inferResult 会全量拷贝引擎返回字段，
    //       因此禁用分支需主动 remove()，确保开关语义对引擎原生字段同样生效
    if (m_outputConfig.value("classId", true)) {
        result.data["classId"] = classId;
    } else {
        result.data.remove("classId");
    }
    if (m_outputConfig.value("className", true)) {
        result.data["className"] = className;
        result.data["category_name"] = className;  // 向后兼容别名
    } else {
        result.data.remove("className");
        result.data.remove("category_name");
    }
    if (m_outputConfig.value("confidence", true)) {
        result.data["confidence"] = confidence;
    } else {
        result.data.remove("confidence");
    }

    // 解析 topK 数组：优先使用引擎返回的 topK / predictions，否则构造单元素 topK
    QJsonArray topKArray;
    if (inferResult.contains("topK") && inferResult["topK"].isArray()) {
        topKArray = inferResult["topK"].toArray();
    } else if (inferResult.contains("predictions") && inferResult["predictions"].isArray()) {
        topKArray = inferResult["predictions"].toArray();
    } else {
        // 兜底：仅含 Top-1 结果
        QJsonObject top1;
        top1["classId"] = classId;
        top1["className"] = className;
        top1["confidence"] = confidence;
        topKArray.append(top1);
    }
    result.data["topK"] = topKArray;

    // v5.4：多目标分类支持
    // 当 params 包含 detectionBoxes 字段（QJsonArray of {x,y,w,h}）时，按顺序对每个 ROI 分类
    // 输出 classArray / confidenceArray（仅当对应开关启用时写入 result.data）
    // 注意：此分支不替换单目标的 classId/className/confidence（整图分类结果），
    //       多目标数组是额外输出；m_engine->infer() 已由 ToolChainExecutor 注入
    if (m_params.contains("detectionBoxes") && m_params["detectionBoxes"].isArray()) {
        QJsonArray boxes = m_params["detectionBoxes"].toArray();
        if (boxes.size() > 0) {
            QJsonArray classArray;
            QJsonArray confArray;

            for (int i = 0; i < boxes.size(); ++i) {
                QJsonObject box = boxes[i].toObject();
                int x = box["x"].toInt();
                int y = box["y"].toInt();
                int w = box["w"].toInt();
                int h = box["h"].toInt();

                // 边界裁剪
                cv::Rect roi(x, y, w, h);
                roi &= cv::Rect(0, 0, input.cols, input.rows);
                if (roi.width <= 0 || roi.height <= 0) {
                    Logger::warn(QString("AiClassifyTool: invalid ROI at index %1").arg(i));
                    classArray.append(QString(""));
                    confArray.append(0.0);
                    continue;
                }

                cv::Mat roiMat = input(roi).clone();
                QJsonObject roiResult;
                if (!m_engine->infer(roiMat, roiResult)) {
                    Logger::warn(QString("AiClassifyTool: ROI %1 inference failed").arg(i));
                    classArray.append(QString(""));
                    confArray.append(0.0);
                    continue;
                }

                int roiClassId = roiResult["classId"].toInt(roiResult["class_index"].toInt(-1));
                QString roiClassName;
                if (roiClassId >= 0 && roiClassId < m_categoryLabels.size()) {
                    roiClassName = m_categoryLabels[roiClassId];
                } else {
                    roiClassName = roiResult["className"].toString(roiResult["category"].toString());
                }
                double roiConf = roiResult["confidence"].toDouble();

                classArray.append(roiClassName);
                confArray.append(roiConf);
            }

            // 仅当对应开关启用时写入（多目标输出默认不启用，向后兼容）
            if (m_outputConfig.value("classArray", false)) {
                result.data["classArray"] = classArray;
            }
            if (m_outputConfig.value("confidenceArray", false)) {
                result.data["confidenceArray"] = confArray;
            }
            // 同时更新内部缓存（不受开关影响，供调试/状态查询使用）
            m_results["lastClassArray"] = QVariant(classArray.toVariantList());
            m_results["lastConfidenceArray"] = QVariant(confArray.toVariantList());
        }
    }

    // 在 overlayImage 上绘制 Top-5 分类结果文字（绿色）
    if (!input.empty()) {
        if (input.channels() == 1) {
            cv::cvtColor(input, result.overlayImage, cv::COLOR_GRAY2BGR);
        } else {
            result.overlayImage = input.clone();
        }

        int maxDisplay = topKArray.size() < 5 ? topKArray.size() : 5;
        int y = 25;
        int lineHeight = 22;
        double fontScale = 0.6;
        int thickness = 1;

        // 标题
        cv::putText(result.overlayImage, "Top-5 Classification:",
                    cv::Point(10, y), cv::FONT_HERSHEY_SIMPLEX, fontScale,
                    cv::Scalar(0, 255, 0), thickness);
        y += lineHeight;

        // 逐行绘制 Top-K
        for (int i = 0; i < maxDisplay; ++i) {
            QJsonObject item = topKArray[i].toObject();
            int cid = item["classId"].toInt(item["class_index"].toInt(-1));
            double conf = item["confidence"].toDouble();
            QString cname = item["className"].toString();
            if (cname.isEmpty()) {
                if (cid >= 0 && cid < m_categoryLabels.size()) {
                    cname = m_categoryLabels[cid];
                } else {
                    cname = QString("Class_%1").arg(cid);
                }
            }

            QString line = QString("%1. %2: %3%").arg(i + 1).arg(cname)
                .arg(QString::number(conf * 100, 'f', 2));
            std::string lineStr = line.toStdString();

            cv::putText(result.overlayImage, lineStr, cv::Point(10, y),
                        cv::FONT_HERSHEY_SIMPLEX, fontScale,
                        cv::Scalar(0, 255, 0), thickness);
            y += lineHeight;
        }
    }

    // Phase 1: InferenceMetrics 改用 IInferenceEngine 的 InferenceMetricsLite
    InferenceMetricsLite metrics = m_engine->lastMetrics();
    result.elapsedMs = metrics.totalMs;
    result.data["preprocess_ms"] = metrics.preprocessMs;
    result.data["inference_ms"] = metrics.inferenceMs;
    result.data["postprocess_ms"] = metrics.postprocessMs;
    result.data["backend"] = metrics.backend;
    result.data["confidence_threshold"] = m_confidenceThreshold;
    result.data["pass"] = result.ok;

    m_results["lastClassification"] = className;
    m_results["lastConfidence"] = confidence;
    m_results["lastPass"] = result.ok;

    return true;
}

QJsonObject AiClassifyTool::serialize() const {
    QJsonObject obj = VisionTool::serialize();
    obj["modelPath"] = m_modelPath;
    obj["confidenceThreshold"] = m_confidenceThreshold;
    obj["topK"] = m_topK;
    obj["inputWidth"] = m_inputWidth;
    obj["inputHeight"] = m_inputHeight;
    QJsonArray labels;
    for (const auto& label : m_categoryLabels) {
        labels.append(label);
    }
    obj["categoryLabels"] = labels;

    // v5.4：持久化输出开关配置
    QJsonObject ocObj;
    for (auto it = m_outputConfig.constBegin(); it != m_outputConfig.constEnd(); ++it) {
        QJsonObject item;
        item["enabled"] = it.value();
        ocObj[it.key()] = item;
    }
    obj["outputConfig"] = ocObj;

    return obj;
}

bool AiClassifyTool::deserialize(const QJsonObject& data) {
    if (!VisionTool::deserialize(data)) return false;

    if (data.contains("modelPath")) m_modelPath = data["modelPath"].toString();
    if (data.contains("confidenceThreshold")) m_confidenceThreshold = data["confidenceThreshold"].toDouble(0.5);
    if (data.contains("topK")) m_topK = data["topK"].toInt(3);
    if (data.contains("inputWidth")) m_inputWidth = data["inputWidth"].toInt(224);
    if (data.contains("inputHeight")) m_inputHeight = data["inputHeight"].toInt(224);
    if (data.contains("categoryLabels")) {
        QJsonArray arr = data["categoryLabels"].toArray();
        m_categoryLabels.clear();
        for (const auto& v : arr) {
            m_categoryLabels.append(v.toString());
        }
    }

    // v5.4：加载输出开关配置
    // 注意：后续 configure(data) 也会从 data 解析 outputConfig，此处显式加载
    //       保证语义清晰且与 serialize() 对称（configure 行为变更时仍可恢复）
    if (data.contains("outputConfig")) {
        QJsonObject oc = data["outputConfig"].toObject();
        m_outputConfig.clear();
        for (const QString& key : oc.keys()) {
            QJsonObject item = oc[key].toObject();
            m_outputConfig[key] = item["enabled"].toBool(true);
        }
    }

    return configure(data);
}