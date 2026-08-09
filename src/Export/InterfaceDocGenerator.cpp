// InterfaceDocGenerator.cpp - Markdown 接口文档生成

#include "Export/InterfaceDocGenerator.h"
#include "Export/ExportConfig.h"

#include <QDateTime>
#include <QTextStream>
#include <QJsonArray>
#include <QJsonObject>

QString InterfaceDocGenerator::generate(const ExportConfig& config,
                                       const QVariantList& nodes,
                                       bool containsAi)
{
    QString md;
    QTextStream s(&md);

    s << QStringLiteral("# 算子流程导出接口文档\n\n");
    s << QStringLiteral("## 1. 概述\n\n");
    s << QStringLiteral("| 项目 | 值 |\n|------|----|\n");
    s << QStringLiteral("| 流程名称 | ") << config.exportName << QStringLiteral(" |\n");
    s << QStringLiteral("| 接口名称 | ") << config.interfaceName << QStringLiteral(" |\n");
    s << QStringLiteral("| 版本 | ") << config.version << QStringLiteral(" |\n");
    s << QStringLiteral("| 作者 | ") << config.author << QStringLiteral(" |\n");
    s << QStringLiteral("| 导出时间 | ") << QDateTime::currentDateTime().toString(Qt::ISODate) << QStringLiteral(" |\n");
    s << QStringLiteral("| 描述 | ") << config.description << QStringLiteral(" |\n");
    s << QStringLiteral("| 含AI算子 | ") << (containsAi ? QStringLiteral("是") : QStringLiteral("否")) << QStringLiteral(" |\n\n");

    // 算子清单
    s << QStringLiteral("### 包含算子清单\n\n");
    s << QStringLiteral("| 序号 | 算子类型 | 节点ID | 名称 |\n|------|----------|--------|------|\n");
    int idx = 1;
    for (const QVariant& nodeVal : nodes) {
        QVariantMap node = nodeVal.toMap();
        s << QStringLiteral("| ") << idx++ << QStringLiteral(" | ")
          << node.value("type").toString() << QStringLiteral(" | ")
          << node.value("id").toString() << QStringLiteral(" | ")
          << node.value("name").toString() << QStringLiteral(" |\n");
    }
    s << QStringLiteral("\n");

    s << QStringLiteral("## 2. 部署说明\n\n");
    s << QStringLiteral("### 运行环境要求\n");
    s << QStringLiteral("- 操作系统：Windows 10/11 x64\n");
    s << QStringLiteral("- 架构：x64（MinGW 13.1 编译）\n\n");
    s << QStringLiteral("### 目录结构\n```\n");
    s << config.exportName << QStringLiteral("/\n");
    s << QStringLiteral("├── bin/\n");
    if (config.exportDll)   s << QStringLiteral("│   ├── QDVPipeline.dll        (wrapper DLL)\n");
    if (config.exportExe)    s << QStringLiteral("│   ├── QDVPipelineRun.exe     (命令行运行器)\n");
    if (config.exportPython) s << QStringLiteral("│   ├── qdv_pipeline.py        (Python wrapper)\n");
    s << QStringLiteral("│   └── qdv_runtime/           (运行时依赖)\n");
    s << QStringLiteral("│       ├── opencv_world4xx.dll\n");
    s << QStringLiteral("│       ├── Qt6Core.dll / Qt6Gui.dll\n");
    s << QStringLiteral("│       ├── platforms/qwindows.dll\n");
    s << QStringLiteral("│       └── config/operators.json\n");
    if (!config.embedScheme) s << QStringLiteral("├── scheme/pipeline.json       (方案文件，外置模式)\n");
    s << QStringLiteral("├── include/QDVPipeline.h      (C 接口头文件)\n");
    if (containsAi) s << QStringLiteral("├── models/*.onnx             (AI 模型文件)\n");
    s << QStringLiteral("├── docs/接口文档.md\n");
    s << QStringLiteral("└── manifest.json\n```\n\n");

    // DLL 调用
    if (config.exportDll) {
        s << QStringLiteral("## 3. DLL 调用（C++）\n\n");
        s << QStringLiteral("### C 接口函数表\n\n");
        s << QStringLiteral("| 函数 | 参数 | 返回值 | 说明 |\n");
        s << QStringLiteral("|------|------|--------|------|\n");
        s << QStringLiteral("| QDVP_Pipeline_Create | void | QDVPipelineHandle* | 创建实例 |\n");
        s << QStringLiteral("| QDVP_Pipeline_Load | (schemePath: char*) | QDVPResult | 加载方案文件 |\n");
        s << QStringLiteral("| QDVP_Pipeline_SetInputImage | (imagePath: char*) | QDVPResult | 设置输入图像 |\n");
        s << QStringLiteral("| QDVP_Pipeline_Run | void | QDVPResult | 执行流程 |\n");
        s << QStringLiteral("| QDVP_Pipeline_GetResultJson | void | const char* | 获取 JSON 结果 |\n");
        s << QStringLiteral("| QDVP_Pipeline_Destroy | void | void | 释放实例 |\n");
        s << QStringLiteral("| QDVP_GetLastError | void | const char* | 获取错误信息 |\n\n");
        s << QStringLiteral("### QDVPResult 错误码\n\n");
        s << QStringLiteral("| 值 | 名称 | 说明 |\n|----|------|------|\n");
        s << QStringLiteral("| 0 | QDVP_OK | 成功 |\n");
        s << QStringLiteral("| 1 | QDVP_ERR_INVALID_ARG | 参数错误 |\n");
        s << QStringLiteral("| 2 | QDVP_ERR_LOAD_SCHEME | 方案加载失败 |\n");
        s << QStringLiteral("| 3 | QDVP_ERR_RUN | 执行失败 |\n");
        s << QStringLiteral("| 4 | QDVP_ERR_NO_RESULT | 无结果 |\n");
        s << QStringLiteral("| 99 | QDVP_ERR_INTERNAL | 内部错误 |\n\n");
        s << QStringLiteral("### 调用示例（LoadLibrary）\n```cpp\n");
        s << QStringLiteral("#include <windows.h>\n#include <stdio.h>\n\n");
        s << QStringLiteral("typedef void* (*CreateFn)();\n");
        s << QStringLiteral("typedef int  (*LoadFn)(void*, const char*);\n");
        s << QStringLiteral("// ... 其他函数指针类型\n\n");
        s << QStringLiteral("int main() {\n");
        s << QStringLiteral("    HMODULE h = LoadLibraryA(\"bin/QDVPipeline.dll\");\n");
        s << QStringLiteral("    // 获取函数指针并调用...\n");
        s << QStringLiteral("    return 0;\n}\n```\n\n");
    }

    // EXE 调用
    if (config.exportExe) {
        s << QStringLiteral("## 4. EXE 调用（命令行）\n\n");
        s << QStringLiteral("### 命令行参数\n\n");
        s << QStringLiteral("```\n");
        s << QStringLiteral("QDVPipelineRun.exe --scheme <path.json> --input <image.png>\n");
        s << QStringLiteral("                    [--output <result.json>] [--format json|text]\n");
        s << QStringLiteral("                    [--save-image <out.png>]\n```\n\n");
        s << QStringLiteral("### 参数说明\n\n");
        s << QStringLiteral("| 参数 | 必填 | 说明 |\n|------|------|------|\n");
        s << QStringLiteral("| --scheme / -s | 是 | 方案文件路径 (.json) |\n");
        s << QStringLiteral("| --input / -i | 是 | 输入图像路径 |\n");
        s << QStringLiteral("| --output / -o | 否 | 结果输出路径（不指定则输出到 stdout） |\n");
        s << QStringLiteral("| --format / -f | 否 | 输出格式：json|text（默认 json） |\n");
        s << QStringLiteral("| --save-image | 否 | 保存指定算子输出图 |\n\n");
        s << QStringLiteral("### 退出码\n\n");
        s << QStringLiteral("| 退出码 | 说明 |\n|--------|------|\n");
        s << QStringLiteral("| 0 | 成功 |\n");
        s << QStringLiteral("| 1 | 参数错误 |\n");
        s << QStringLiteral("| 2 | 方案加载失败 |\n");
        s << QStringLiteral("| 3 | 输入图像加载失败 |\n");
        s << QStringLiteral("| 4 | 流程执行失败 |\n");
        s << QStringLiteral("| 5 | 输出文件写入失败 |\n\n");
    }

    // Python 调用
    if (config.exportPython) {
        s << QStringLiteral("## 5. Python 调用\n\n");
        s << QStringLiteral("### 依赖安装\n```\n");
        s << QStringLiteral("pip install numpy opencv-python  # 可选\n```\n\n");
        s << QStringLiteral("### 类与方法\n\n");
        s << QStringLiteral("| 方法 | 参数 | 返回值 | 说明 |\n");
        s << QStringLiteral("|------|------|--------|------|\n");
        s << QStringLiteral("| __init__ | runtime_dir=None | - | 初始化，定位 DLL |\n");
        s << QStringLiteral("| load | scheme_path: str | None | 加载方案文件 |\n");
        s << QStringLiteral("| set_input_image | image_path: str | None | 设置输入图像 |\n");
        s << QStringLiteral("| run | - | None | 执行流程 |\n");
        s << QStringLiteral("| get_result_json | - | dict | 获取 JSON 结果（解析为 dict） |\n");
        s << QStringLiteral("| close | - | None | 释放实例 |\n\n");
        s << QStringLiteral("### 调用示例\n```python\n");
        s << QStringLiteral("from qdv_pipeline import ") << config.interfaceName << QStringLiteral("\n\n");
        s << QStringLiteral("with ") << config.interfaceName << QStringLiteral("() as pipeline:\n");
        s << QStringLiteral("    pipeline.load(\"scheme/pipeline.json\")\n");
        s << QStringLiteral("    pipeline.set_input_image(\"test.png\")\n");
        s << QStringLiteral("    pipeline.run()\n");
        s << QStringLiteral("    result = pipeline.get_result_json()\n");
        s << QStringLiteral("    print(result)\n");
        s << QStringLiteral("```\n\n");
    }

    s << QStringLiteral("## 6. 注意事项\n\n");
    s << QStringLiteral("1. **32/64 位**：仅支持 Windows x64，MinGW 13.1 编译\n");
    s << QStringLiteral("2. **编码**：所有字符串使用 UTF-8\n");
    s << QStringLiteral("3. **方案嵌入/外置**：当前为 ") << (config.embedScheme ? QStringLiteral("嵌入模式（方案打进 DLL）") : QStringLiteral("外置模式（方案为独立文件）")) << QStringLiteral("\n");
    if (containsAi) {
        s << QStringLiteral("4. **AI 模型**：本流程含 AI 算子，models/ 目录下含 onnx 模型，运行时内置 OpenCV DNN 推理引擎\n");
    }
    s << QStringLiteral("5. **多线程**：每个 QDVPipelineHandle 持有独立的算子实例，可多线程并发；但同一 handle 不可跨线程并发调用\n");
    s << QStringLiteral("6. **字符串缓冲**：GetResultJson/GetResult/GetLastError 返回值为内部缓冲，下次调用失效，调用方需立即拷贝\n");

    s.flush();
    return md;
}

QString InterfaceDocGenerator::generateQuickStart(const ExportConfig& config)
{
    QString md;
    QTextStream s(&md);

    s << QStringLiteral("# 快速上手\n\n");
    s << QStringLiteral("## 5 分钟跑通\n\n");
    s << QStringLiteral("### Python（最快）\n```bash\n");
    s << QStringLiteral("cd ") << config.exportName << QStringLiteral("\n");
    s << QStringLiteral("python -c \"from qdv_pipeline import ") << config.interfaceName
      << QStringLiteral("; p=") << config.interfaceName
      << QStringLiteral("(); p.load('scheme/pipeline.json'); p.set_input_image('test.png'); p.run(); print(p.get_result_json())\"\n```\n\n");
    s << QStringLiteral("### 命令行\n```bash\n");
    s << QStringLiteral("cd ") << config.exportName << QStringLiteral("/bin\n");
    s << QStringLiteral("QDVPipelineRun.exe --scheme ../scheme/pipeline.json --input test.png\n```\n");
    s.flush();
    return md;
}
