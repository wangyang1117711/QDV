/*
 * MobileSAM 真实模型推理冒烟测试
 * 验证: ZeroShotEngine 加载 mobile_sam_encoder + decoder 并成功生成掩码
 *
 * 运行: .\build\tests\test_mobilesmoke.exe
 * 需要: E:/anchor/Trae/QDV/models/zero_shot/ 下有 encoder/decoder ONNX 模型
 */

#include <iostream>
#include <opencv2/opencv.hpp>
#include <windows.h>
#include "ZeroShotEngine.h"
#include "ZeroShotTypes.h"

int main(int argc, char** argv) {
    // 修复 Windows 控制台中文乱码
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    std::cout << "=== MobileSAM 真实模型推理冒烟测试 ===" << std::endl;
    std::cout << std::endl;

    // 模型路径: 当前使用的真实模型位于 models/zero_shot/ 目录
    QString modelDir = QString::fromUtf8("E:/anchor/Trae/QDV/models/zero_shot");
    std::string testImagePath = "E:/anchor/Trae/QDV/docs/screenshots/02_main_camera.png";

    std::cout << "模型目录: " << modelDir.toStdString() << std::endl;
    std::cout << "测试图像: " << testImagePath << std::endl;
    std::cout << std::endl;

    // 1. 加载测试图像
    cv::Mat image = cv::imread(testImagePath);
    if (image.empty()) {
        std::cerr << "ERROR: 无法读取测试图像: " << testImagePath << std::endl;
        return 1;
    }
    std::cout << "图像加载成功: " << image.cols << "x" << image.rows
              << " channels=" << image.channels() << std::endl;
    std::cout << std::endl;

    // 2. 创建 ZeroShotEngine 实例
    zsu::ZeroShotEngine engine;

    // 3. 加载 MobileSAM 模型（编码器+解码器会自动从目录扫描）
    bool loadOk = engine.loadModel(zsu::ZeroShotModelType::MobileSAM, modelDir, QSize(1024, 1024));

    if (!loadOk) {
        std::cerr << "ERROR: 模型加载失败" << std::endl;
        std::cerr << "详细错误: " << engine.lastError().toStdString() << std::endl;
        return 1;
    }

    if (!engine.isModelLoaded()) {
        std::cerr << "ERROR: loadModel 返回 true，但 isModelLoaded() 为 false" << std::endl;
        return 1;
    }

    std::cout << "OK: 模型加载成功" << std::endl;
    std::cout << std::endl;

    // 4. 执行推理
    std::cout << "开始推理..." << std::endl;
    zsu::ZeroShotResult result = engine.infer(image);

    if (!result.success) {
        std::cerr << "ERROR: 推理失败" << std::endl;
        std::cerr << "错误信息: " << result.errorMessage.toStdString() << std::endl;
        return 1;
    }

    std::cout << "OK: 推理成功" << std::endl;
    std::cout << "  总耗时: " << result.metrics.totalMs << " ms" << std::endl;
    std::cout << "  推理耗时: " << result.metrics.inferenceMs << " ms" << std::endl;
    std::cout << "  后端: " << result.metrics.backend.toStdString() << std::endl;
    std::cout << "  异常分数 (前景比例): " << result.anomalyScore << std::endl;
    std::cout << "  置信度 (最佳 IoU): " << result.confidence << std::endl;
    std::cout << std::endl;

    // 5. 验证掩码生成
    if (result.mask.empty()) {
        std::cerr << "ERROR: 掩码为空" << std::endl;
        return 1;
    }

    std::cout << "OK: 掩码生成成功" << std::endl;
    std::cout << "  掩码尺寸: " << result.mask.cols << "x" << result.mask.rows << std::endl;
    std::cout << "  掩码类型: CV_" << (result.mask.type() == CV_8UC1 ? "8UC1" : "other") << std::endl;

    // 计算前景比例验证
    int nonZero = cv::countNonZero(result.mask);
    double ratio = (double)nonZero / (result.mask.rows * result.mask.cols);
    std::cout << "  前景像素数: " << nonZero << " (" << (ratio * 100) << "%)" << std::endl;
    std::cout << std::endl;

    // 6. 保存输出掩码（供人工检查）
    std::string outputPath = "E:/anchor/Trae/QDV/cache_test_msmask.png";
    cv::imwrite(outputPath, result.mask);
    std::cout << "OK: 掩码已保存到: " << outputPath << std::endl;
    std::cout << std::endl;

    std::cout << "========================================" << std::endl;
    std::cout << "PASS: MobileSAM 真实模型推理工作正常" << std::endl;
    std::cout << "========================================" << std::endl;

    return 0;
}