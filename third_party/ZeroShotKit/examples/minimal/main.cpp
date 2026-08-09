// ============================================================================
// 最简推理示例
// 演示如何使用 zsu::Kit 门面类加载 AnomalyCLIP 模型，
// 并对单张图像进行零样本异常检测推理。
//
// 用法: minimal <model_path> <image_path> [text_prompt]
// ============================================================================

#include <QCoreApplication>
#include <QString>
#include <QStringList>
#include <QElapsedTimer>
#include <opencv2/opencv.hpp>
#include <iostream>

#include "ZeroShotKit/ZeroShotKit.h"
#include "ZeroShotKit/ZeroShotTypes.h"

// 将命令行参数（可能是本地编码的 char*）安全转换为 QString
static QString argToQString(const char* argv)
{
    return QString::fromLocal8Bit(argv);
}

int main(int argc, char* argv[])
{
    // Kit 继承自 QObject，需要 QCoreApplication 实例以支持元对象机制
    QCoreApplication app(argc, argv);

    // --- 1. 解析命令行参数 ---
    if (argc < 3) {
        std::cerr << "用法: minimal <model_path> <image_path> [text_prompt]" << std::endl;
        std::cerr << "示例: minimal D:/models/anomalyclip C:/images/sample.png" << std::endl;
        std::cerr << "      minimal D:/models/anomalyclip C:/images/sample.png "
                     "\"normal:product|anomaly:damaged product\"" << std::endl;
        return 1;
    }

    const QString modelPath = argToQString(argv[1]);
    const QString imagePath = argToQString(argv[2]);

    // 默认文本提示词（AnomalyCLIP 用：正常 vs 异常）
    QStringList prompts = {
        QString::fromUtf8("normal:product"),
        QString::fromUtf8("anomaly:damaged product")
    };

    // 若命令行提供了自定义提示词，则按 '|' 分割
    if (argc >= 4) {
        const QString raw = argToQString(argv[3]);
        const QStringList parts = raw.split('|', Qt::SkipEmptyParts);
        if (!parts.isEmpty()) {
            prompts = parts;
        }
    }

    // --- 2. 创建 Kit 实例 ---
    zsu::Kit kit;

    // --- 3. 加载 AnomalyCLIP 模型 ---
    std::cout << "[信息] 正在加载 AnomalyCLIP 模型: "
              << modelPath.toLocal8Bit().constData() << std::endl;
    if (!kit.loadModel(zsu::ZeroShotModelType::AnomalyCLIP, modelPath)) {
        std::cerr << "[错误] 模型加载失败，请检查路径是否正确: "
                  << modelPath.toLocal8Bit().constData() << std::endl;
        return 2;
    }
    std::cout << "[信息] 模型加载成功" << std::endl;

    // --- 4. 设置文本提示词 ---
    kit.setTextPrompts(prompts);
    std::cout << "[信息] 文本提示词: "
              << prompts.join(" | ").toLocal8Bit().constData() << std::endl;

    // --- 5. 读取待推理图像 ---
    // 注意：cv::imread 在 Windows 下对中文路径支持有限，
    // 这里使用 toLocal8Bit().toStdString() 以本地编码传递路径以尽量兼容中文路径。
    // 若仍遇中文路径问题，建议改用 QFile + cv::imdecode 的方式。
    cv::Mat image = cv::imread(imagePath.toLocal8Bit().toStdString(), cv::IMREAD_COLOR);
    if (image.empty()) {
        std::cerr << "[错误] 无法读取图像: "
                  << imagePath.toLocal8Bit().constData() << std::endl;
        return 3;
    }
    std::cout << "[信息] 图像尺寸: " << image.cols << "x" << image.rows << std::endl;

    // --- 6. 执行同步推理 ---
    QElapsedTimer timer;
    timer.start();
    zsu::ZeroShotResult result = kit.infer(image);
    const qint64 wallMs = timer.elapsed();

    // --- 7. 输出结果 ---
    if (!result.success) {
        std::cerr << "[错误] 推理失败: "
                  << result.errorMessage.toLocal8Bit().constData() << std::endl;
        return 4;
    }

    std::cout << "---------------- 推理结果 ----------------" << std::endl;
    std::cout << "分类结果:     " << result.category.toLocal8Bit().constData() << std::endl;
    std::cout << "置信度:       " << result.confidence << std::endl;
    std::cout << "异常分数:     " << result.anomalyScore
              << " (阈值 " << kit.anomalyThreshold() << ")" << std::endl;
    std::cout << "是否异常:     "
              << (result.anomalyScore >= kit.anomalyThreshold() ? "是" : "否") << std::endl;
    std::cout << "检测框数量:   " << result.detections.size() << std::endl;
    std::cout << "---------------- 耗时统计 ----------------" << std::endl;
    std::cout << "预处理:       " << result.metrics.preprocessMs << " ms" << std::endl;
    std::cout << "推理:         " << result.metrics.inferenceMs << " ms" << std::endl;
    std::cout << "后处理:       " << result.metrics.postprocessMs << " ms" << std::endl;
    std::cout << "引擎总耗时:   " << result.metrics.totalMs << " ms" << std::endl;
    std::cout << "墙钟耗时:     " << wallMs << " ms" << std::endl;
    std::cout << "后端:         "
              << result.metrics.backend.toLocal8Bit().constData() << std::endl;

    return 0;
}
