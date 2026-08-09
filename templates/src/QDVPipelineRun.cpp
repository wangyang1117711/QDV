// QDVPipelineRun.cpp - 命令行可执行
// 用法: QDVPipelineRun.exe --scheme <path.json> --input <image.png>
//                       [--output <result.json>] [--format json|text]
//                       [--save-image <out.png>]
// 退出码: 0=成功, 非0=失败

#include "QDVRuntime.h"

#include <QCoreApplication>
#include <QCommandLineParser>
#include <QTextStream>
#include <QFile>
#include <iostream>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    app.setApplicationName("QDVPipelineRun");
    app.setApplicationVersion("1.0.0");

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("QDV 算子流程命令行运行器"));
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption schemeOpt(QStringList() << "s" << "scheme",
        QStringLiteral("方案文件路径 (.json)"), "path");
    QCommandLineOption inputOpt(QStringList() << "i" << "input",
        QStringLiteral("输入图像路径"), "path");
    QCommandLineOption outputOpt(QStringList() << "o" << "output",
        QStringLiteral("结果输出路径 (不指定则输出到 stdout)"), "path");
    QCommandLineOption formatOpt(QStringList() << "f" << "format",
        QStringLiteral("输出格式: json|text (默认 json)"), "format", "json");
    QCommandLineOption saveImageOpt(QStringList() << "save-image",
        QStringLiteral("保存指定算子输出图"), "path");

    parser.addOption(schemeOpt);
    parser.addOption(inputOpt);
    parser.addOption(outputOpt);
    parser.addOption(formatOpt);
    parser.addOption(saveImageOpt);
    parser.process(app);

    if (!parser.isSet(schemeOpt) || !parser.isSet(inputOpt)) {
        std::cerr << "错误: 缺少必填参数 --scheme 和 --input" << std::endl;
        std::cerr << "用法: QDVPipelineRun --scheme <path.json> --input <image.png>" << std::endl;
        return 1;
    }

    QString schemePath = parser.value(schemeOpt);
    QString inputPath = parser.value(inputOpt);
    QString format = parser.value(formatOpt).toLower();

    QDVRuntime runtime;
    if (!runtime.loadScheme(schemePath)) {
        std::cerr << "错误: 方案加载失败: " << runtime.lastError().toStdString() << std::endl;
        return 2;
    }

    if (!runtime.setInputImage(inputPath)) {
        std::cerr << "错误: 输入图像加载失败: " << runtime.lastError().toStdString() << std::endl;
        return 3;
    }

    if (!runtime.run()) {
        std::cerr << "错误: 流程执行失败: " << runtime.lastError().toStdString() << std::endl;
        return 4;
    }

    // 输出结果
    QString result = runtime.getResultJson();
    if (format == "text") {
        // 简易文本格式
        result = "QDV Pipeline Run OK\n";
    }

    if (parser.isSet(outputOpt)) {
        QFile outFile(parser.value(outputOpt));
        if (!outFile.open(QIODevice::WriteOnly)) {
            std::cerr << "错误: 无法写入输出文件: " << parser.value(outputOpt).toStdString() << std::endl;
            return 5;
        }
        outFile.write(result.toUtf8());
        outFile.close();
    } else {
        std::cout << result.toStdString() << std::endl;
    }

    // 保存输出图（可选）
    if (parser.isSet(saveImageOpt)) {
        if (!runtime.saveOutputImage(parser.value(saveImageOpt).section('.', 0, 0),
                                      parser.value(saveImageOpt))) {
            std::cerr << "警告: 输出图保存失败" << std::endl;
        }
    }

    return 0;
}
