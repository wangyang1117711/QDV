// ============================================================================
// 批量推理示例
// 演示如何使用 zsu::Kit 门面类对目录下多张图像进行异步批量推理，
// 并将结果导出为 JSON 报告。
//
// 用法: batch <model_path> <model_type> <input_dir> <output_json> [text_prompts]
//   model_type: anomalyclip / groundingdino / mobilesam / openclip / patchcore
// ============================================================================

#include <QCoreApplication>
#include <QEventLoop>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QElapsedTimer>
#include <iostream>

#include "ZeroShotKit/ZeroShotKit.h"
#include "ZeroShotKit/ZeroShotTypes.h"

// 将命令行参数（可能是本地编码的 char*）安全转换为 QString
static QString argToQString(const char* argv)
{
    return QString::fromLocal8Bit(argv);
}

// 将模型类型字符串解析为枚举
static zsu::ZeroShotModelType parseModelType(const QString& text)
{
    const QString lower = text.toLower();
    if (lower == "anomalyclip")  return zsu::ZeroShotModelType::AnomalyCLIP;
    if (lower == "groundingdino") return zsu::ZeroShotModelType::GroundingDINO;
    if (lower == "mobilesam")   return zsu::ZeroShotModelType::MobileSAM;
    if (lower == "openclip")    return zsu::ZeroShotModelType::OpenCLIP;
    if (lower == "patchcore")   return zsu::ZeroShotModelType::PatchCore;
    return zsu::ZeroShotModelType::Unknown;
}

// 收集目录下所有支持格式的图像文件（递归子目录）
static QStringList collectImageFiles(const QString& dirPath)
{
    const QStringList filters = { "*.jpg", "*.jpeg", "*.png", "*.bmp" };
    QStringList collected;
    QDir dir(dirPath);
    if (!dir.exists()) {
        return collected;
    }
    QDirIterator it(dirPath, filters, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        collected << it.next();
    }
    collected.sort();
    return collected;
}

// 在控制台打印简单进度条
static void printProgressBar(int current, int total)
{
    const int barWidth = 40;
    const float progress = (total > 0) ? static_cast<float>(current) / total : 0.0f;
    const int filled = static_cast<int>(progress * barWidth);
    std::cout << "\r[";
    for (int i = 0; i < barWidth; ++i) {
        std::cout << (i < filled ? '#' : '-');
    }
    std::cout << "] " << current << "/" << total << " ("
              << static_cast<int>(progress * 100.0f) << "%)" << std::flush;
    if (current >= total) {
        std::cout << std::endl;
    }
}

int main(int argc, char* argv[])
{
    // 异步推理依赖 Qt 事件循环，需要 QCoreApplication
    QCoreApplication app(argc, argv);

    // --- 1. 解析命令行参数 ---
    if (argc < 5) {
        std::cerr << "用法: batch <model_path> <model_type> <input_dir> <output_json> [text_prompts]"
                  << std::endl;
        std::cerr << "model_type: anomalyclip | groundingdino | mobilesam | openclip | patchcore"
                  << std::endl;
        std::cerr << "示例: batch D:/models/anomalyclip anomalyclip C:/images ./report.json"
                     " \"normal:product|anomaly:damaged product\"" << std::endl;
        return 1;
    }

    const QString modelPath    = argToQString(argv[1]);
    const QString modelTypeStr = argToQString(argv[2]);
    const QString inputDir     = argToQString(argv[3]);
    const QString outputJson   = argToQString(argv[4]);

    // 解析模型类型
    const zsu::ZeroShotModelType modelType = parseModelType(modelTypeStr);
    if (modelType == zsu::ZeroShotModelType::Unknown) {
        std::cerr << "[错误] 未知模型类型: " << modelTypeStr.toLocal8Bit().constData() << std::endl;
        std::cerr << "支持的类型: anomalyclip | groundingdino | mobilesam | openclip | patchcore"
                  << std::endl;
        return 2;
    }

    // 默认文本提示词
    QStringList prompts = {
        QString::fromUtf8("normal:product"),
        QString::fromUtf8("anomaly:damaged product")
    };
    // 若命令行提供了自定义提示词，则按 '|' 分割
    if (argc >= 6) {
        const QString raw = argToQString(argv[5]);
        const QStringList parts = raw.split('|', Qt::SkipEmptyParts);
        if (!parts.isEmpty()) {
            prompts = parts;
        }
    }

    // --- 2. 创建 Kit 实例 ---
    zsu::Kit kit;

    // --- 3. 加载模型 ---
    std::cout << "[信息] 正在加载模型 [" << modelTypeStr.toLocal8Bit().constData() << "]: "
              << modelPath.toLocal8Bit().constData() << std::endl;
    if (!kit.loadModel(modelType, modelPath)) {
        std::cerr << "[错误] 模型加载失败" << std::endl;
        return 3;
    }
    std::cout << "[信息] 模型加载成功" << std::endl;

    // 设置文本提示词
    kit.setTextPrompts(prompts);

    // --- 4. 遍历输入目录 ---
    QStringList imagePaths = collectImageFiles(inputDir);
    if (imagePaths.isEmpty()) {
        std::cerr << "[错误] 目录下未找到图像文件: "
                  << inputDir.toLocal8Bit().constData() << std::endl;
        return 4;
    }
    std::cout << "[信息] 共发现 " << imagePaths.size() << " 张图像" << std::endl;

    // --- 5. 准备事件循环，等待异步批量推理完成 ---
    QEventLoop loop;
    QList<zsu::ZeroShotResult> results;

    // 批量完成信号 → 退出事件循环
    QObject::connect(&kit, &zsu::Kit::batchCompleted, &loop,
                     [&](const QList<zsu::ZeroShotResult>& r) {
        results = r;
        std::cout << std::endl
                  << "[信息] 批量推理完成，共 " << r.size() << " 条结果" << std::endl;
        loop.quit();
    });

    // 进度信号 → 打印进度条
    QObject::connect(&kit, &zsu::Kit::progressUpdated, &loop,
                     [](int current, int total) {
        printProgressBar(current, total);
    });

    // 错误信号（单张图读取失败会触发，批量推理仍会继续）
    QObject::connect(&kit, &zsu::Kit::errorOccurred, &loop,
                     [](const QString& msg) {
        std::cerr << std::endl
                  << "[警告] " << msg.toLocal8Bit().constData() << std::endl;
    });

    // --- 6. 启动异步批量推理 ---
    QElapsedTimer timer;
    timer.start();
    kit.inferBatchAsync(imagePaths);

    // 阻塞等待 batchCompleted 信号
    loop.exec();
    const qint64 wallMs = timer.elapsed();

    // --- 7. 生成 JSON 报告 ---
    QJsonObject rootObj;
    rootObj["model_path"]      = modelPath;
    rootObj["model_type"]      = modelTypeStr;
    rootObj["input_dir"]       = inputDir;
    rootObj["total_images"]    = imagePaths.size();
    rootObj["total_results"]   = results.size();
    rootObj["wall_time_ms"]    = static_cast<qint64>(wallMs);
    rootObj["text_prompts"]    = QJsonArray::fromStringList(prompts);

    QJsonArray resultsArray;
    qint64 totalInferenceMs = 0;
    int successCount  = 0;
    int anomalyCount  = 0;
    const float anomalyThreshold = kit.anomalyThreshold();

    for (int i = 0; i < results.size() && i < imagePaths.size(); ++i) {
        const zsu::ZeroShotResult& r = results[i];
        QJsonObject item = r.toJson();
        item["image_path"] = imagePaths[i];
        item["index"]      = i;
        resultsArray.append(item);

        if (r.success) {
            ++successCount;
            totalInferenceMs += r.metrics.totalMs;
            if (r.anomalyScore >= anomalyThreshold) {
                ++anomalyCount;
            }
        }
    }
    rootObj["results"]            = resultsArray;
    rootObj["success_count"]      = successCount;
    rootObj["failure_count"]      = results.size() - successCount;
    rootObj["anomaly_count"]      = anomalyCount;
    rootObj["normal_count"]       = successCount - anomalyCount;
    rootObj["anomaly_threshold"]  = static_cast<double>(anomalyThreshold);
    rootObj["avg_inference_ms"]   = (successCount > 0)
        ? static_cast<double>(totalInferenceMs) / successCount : 0.0;

    QJsonDocument doc(rootObj);

    // --- 8. 写入 JSON 文件 ---
    QFile outFile(outputJson);
    if (!outFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        std::cerr << "[错误] 无法写入输出文件: "
                  << outputJson.toLocal8Bit().constData() << std::endl;
        return 5;
    }
    outFile.write(doc.toJson(QJsonDocument::Indented));
    outFile.close();

    // --- 9. 汇总输出 ---
    std::cout << "---------------- 批量推理汇总 ----------------" << std::endl;
    std::cout << "总图像数:       " << results.size() << std::endl;
    std::cout << "成功数:         " << successCount << std::endl;
    std::cout << "失败数:         " << (results.size() - successCount) << std::endl;
    std::cout << "异常样本:       " << anomalyCount << std::endl;
    std::cout << "正常样本:       " << (successCount - anomalyCount) << std::endl;
    std::cout << "平均推理耗时:   "
              << (successCount > 0
                  ? static_cast<double>(totalInferenceMs) / successCount : 0.0)
              << " ms" << std::endl;
    std::cout << "总墙钟耗时:     " << wallMs << " ms" << std::endl;
    std::cout << "报告已写入:     " << outputJson.toLocal8Bit().constData() << std::endl;

    return 0;
}
