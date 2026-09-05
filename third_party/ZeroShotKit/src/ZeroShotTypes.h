#ifndef ZEROSHOTTYPES_H
#define ZEROSHOTTYPES_H

// ============================================================================
// 0样本升级版 - 公共数据类型定义
// 供 ORTInferenceEngine / ZeroShotEngine / 测试代码共用
// ============================================================================

#include <QString>
#include <QStringList>
#include <QJsonObject>
#include <QJsonArray>
#include <QSize>
#include <vector>
#include <opencv2/opencv.hpp>

namespace zsu {

// --- 推理后端枚举 ---
enum class Backend {
    OpenCVDNN = 0,     // OpenCV DNN（主项目现有后端，用于对比基准）
    ONNXRuntime = 1    // ONNX Runtime（本子项目新增）
};

// --- 推理性能指标 ---
struct InferenceMetrics {
    qint64 preprocessMs = 0;   // 预处理耗时
    qint64 inferenceMs = 0;    // 推理耗时
    qint64 postprocessMs = 0;  // 后处理耗时
    qint64 totalMs = 0;        // 总耗时
    QString backend;           // 使用的后端名称
};

// --- 错误状态 ---
enum class ErrorState {
    NoError = 0,
    ModelNotFound,
    ModelLoadFailed,
    ModelForwardTestFailed,
    PreprocessFailed,
    InferenceFailed,
    PostprocessFailed,
    UnexpectedError,
    FatalError,
    Cancelled,
    Timeout
};

// --- 模型输入规格 ---
struct ModelInputSpec {
    QStringList supportedFormats = {"JPG", "PNG", "BMP", "TIFF", "WEBP"};
    int minWidth = 32;
    int minHeight = 32;
    int maxWidth = 4096;
    int maxHeight = 4096;
    QSize recommendedSize = QSize(224, 224);
    qint64 maxFileSizeBytes = 50 * 1024 * 1024;
};

// --- 零样本模型类型 ---
enum class ZeroShotModelType {
    AnomalyCLIP,       // 零样本异常检测
    GroundingDINO,     // 开集目标检测
    MobileSAM,         // 轻量分割
    OpenCLIP,          // 零样本分类
    PatchCore,         // 正常样本异常检测
    Unknown
};

// --- 零样本推理结果 ---
struct ZeroShotResult {
    // 通用字段
    bool success = false;
    QString errorMessage;
    InferenceMetrics metrics;

    // 分类结果（AnomalyCLIP / OpenCLIP）
    QString category;
    double confidence = 0.0;
    double anomalyScore = 0.0;  // 异常分数 [0,1]

    // 检测结果（Grounding DINO / YOLO）
    struct Detection {
        float cx, cy, w, h;     // 边界框（归一化坐标）
        float confidence;
        int classId;
        QString className;
    };
    std::vector<Detection> detections;

    // 分割结果（MobileSAM）
    cv::Mat mask;               // 二值掩码

    // 异常热力图（AnomalyCLIP / PatchCore）
    cv::Mat anomalyMap;         // 像素级异常分数图

    // --- 效果图展示（原图 + 检测框/掩码叠加） ---
    cv::Mat sourceImage;        // 输入原图（供"检测效果图"叠加绘制）
    QString imageName;          // 原图像文件名（批量/单张推理时记录，供列表显示）

    // 转为 JSON（与主项目 InferenceEngine 结果格式兼容）
    QJsonObject toJson() const {
        QJsonObject obj;
        obj["status"] = success ? "success" : "failed";
        obj["category"] = category;
        obj["confidence"] = confidence;
        obj["anomaly_score"] = anomalyScore;
        obj["latency_ms"] = metrics.totalMs;
        obj["preprocess_ms"] = metrics.preprocessMs;
        obj["inference_ms"] = metrics.inferenceMs;
        obj["postprocess_ms"] = metrics.postprocessMs;
        obj["backend"] = metrics.backend;

        QJsonArray detArray;
        for (const auto& d : detections) {
            QJsonObject det;
            det["class_id"] = d.classId;
            det["class_name"] = d.className;
            det["confidence"] = d.confidence;
            det["cx"] = d.cx;
            det["cy"] = d.cy;
            det["w"] = d.w;
            det["h"] = d.h;
            detArray.append(det);
        }
        obj["detections"] = detArray;
        obj["num_detections"] = (int)detections.size();

        return obj;
    }
};

// --- ONNX Runtime 会话配置 ---
struct ORTSessionConfig {
    int intraOpNumThreads = 4;              // 线程内并行数
    int interOpNumThreads = 2;              // 线程间并行数
    QString executionMode = "SEQUENTIAL";   // SEQUENTIAL | PARALLEL
    QString optimizationLevel = "ALL";      // DISABLE | BASIC | EXTENDED | ALL
    bool enableMemPattern = true;           // 内存模式优化
    bool enableCpuMemArena = true;          // CPU 内存竞技场

    // 量化配置（M5 阶段使用）
    bool enableInt8Quantization = false;
    QString quantizationModelPath;          // 量化后的模型路径
};

// --- 推理稳定性配置 ---
struct StabilityConfig {
    int numRuns = 1;                // 多次推理次数（1=单次，>1=取稳定值）
    float nmsIouThreshold = 0.45f;  // NMS 的 IoU 阈值
    float confidenceThreshold = 0.3f; // 置信度过滤阈值
    bool enableMultiRunStability = false; // 是否启用多次推理取稳定值
};

// --- 模型注意事项 ---
struct ModelNote {
    QString modelType;      // 模型类型名称
    QString displayName;    // 用户可见名称
    QString description;    // 模型简述
    QStringList notes;      // 注意事项列表
    QStringList inputFormat; // 输入格式要求
    QStringList limitations; // 限制条件
    QStringList tips;        // 使用建议
};

// --- 人工复核状态 ---
enum class ReviewStatus {
    Pending = 0,    // 待复核
    Confirmed = 1,  // 已确认
    Rejected = 2    // 已拒绝
};

// --- Bad Case 记录 ---
struct BadCaseRecord {
    QString imagePath;
    QString modelType;
    ZeroShotResult originalResult;   // 原始推理结果
    ZeroShotResult correctedResult;  // 用户修正后的结果
    QString userComment;             // 用户备注
    qint64 timestamp = 0;            // 记录时间
};

} // namespace zsu

#endif // ZEROSHOTTYPES_H
