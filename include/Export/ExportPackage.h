#ifndef EXPORT_PACKAGE_H
#define EXPORT_PACKAGE_H

#include <QString>

/**
 * @brief 导出包目录结构常量
 *
 * 定义产物包内部的相对路径，供 SchemeExporter 各阶段引用。
 *
 * 结构：
 *   <outputPath>/<exportName>/
 *   ├── bin/
 *   │   ├── QDVPipeline.dll / QDVPipelineRun.exe / qdv_pipeline.py
 *   │   └── qdv_runtime/
 *   ├── scheme/pipeline.json
 *   ├── include/QDVPipeline.h
 *   ├── models/*.onnx
 *   ├── docs/接口文档.md / 使用说明.md / examples/
 *   └── manifest.json
 */
namespace ExportPackage {

inline QString binDir()           { return QStringLiteral("bin"); }
inline QString runtimeDir()       { return QStringLiteral("bin/qdv_runtime"); }
inline QString schemeDir()         { return QStringLiteral("scheme"); }
inline QString schemeFile()        { return QStringLiteral("scheme/pipeline.json"); }
inline QString includeDir()       { return QStringLiteral("include"); }
inline QString modelsDir()        { return QStringLiteral("models"); }
inline QString docsDir()          { return QStringLiteral("docs"); }
inline QString examplesDir()      { return QStringLiteral("docs/examples"); }
inline QString manifestFile()     { return QStringLiteral("manifest.json"); }

inline QString dllName()          { return QStringLiteral("QDVPipeline.dll"); }
inline QString exeName()          { return QStringLiteral("QDVPipelineRun.exe"); }
inline QString pyName()           { return QStringLiteral("qdv_pipeline.py"); }
inline QString headerName()       { return QStringLiteral("QDVPipeline.h"); }

} // namespace ExportPackage

#endif // EXPORT_PACKAGE_H
