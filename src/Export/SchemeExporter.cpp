// SchemeExporter.cpp - 导出编排器实现
// 异步执行导出流程：复制模板 + 序列化方案 + 生成文档 + 写 manifest

#include "Export/SchemeExporter.h"
#include "Export/ExportConfig.h"
#include "Export/ExportPackage.h"
#include "Export/TemplateLocator.h"
#include "Export/RuntimePackager.h"
#include "Export/InterfaceDocGenerator.h"
#include "Export/ExampleCodeGenerator.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QtConcurrent>
#include <QDateTime>

SchemeExporter::SchemeExporter(QObject* parent)
    : QObject(parent)
    , m_watcher(new QFutureWatcher<bool>(this))
{
    connect(m_watcher, &QFutureWatcher<bool>::finished, this, &SchemeExporter::onExportFinished);
}

SchemeExporter::~SchemeExporter()
{
    if (m_busy) {
        m_watcher->waitForFinished();
    }
}

void SchemeExporter::exportAsync(const ExportConfig& config,
                                 const QVariantList& nodes,
                                 const QVariantList& connections)
{
    if (m_busy) {
        emit errorOccurred(QStringLiteral("导出正在进行中"));
        return;
    }

    // 同步校验
    QString errMsg;
    if (!config.isValid(&errMsg)) {
        emit errorOccurred(errMsg);
        return;
    }
    if (nodes.isEmpty()) {
        emit errorOccurred(QStringLiteral("方案无节点，无法导出"));
        return;
    }

    // 检查模板存在
    QString missing;
    if (!TemplateLocator::checkTemplates(&missing)) {
        emit errorOccurred(QStringLiteral("模板缺失，请先构建项目: ") + missing);
        return;
    }

    m_busy = true;
    emit exportProgress(5);

    // 复制配置与数据（worker 线程不能直接捕获 Qt 对象引用）
    ExportConfig cfgCopy = config;
    QVariantList nodesCopy = nodes;
    QVariantList connsCopy = connections;

    // 异步执行
    auto future = QtConcurrent::run([this, cfgCopy, nodesCopy, connsCopy]() -> bool {
        QString msg;
        bool ok = doExport(cfgCopy, nodesCopy, connsCopy, &msg);
        m_lastMessage = msg;
        return ok;
    });
    m_watcher->setFuture(future);
}

void SchemeExporter::cancel()
{
    // QtConcurrent::run 不支持取消，这里仅标记
    // 实际取消需要将 doExport 拆分为可中断的步骤
    if (m_busy) {
        emit errorOccurred(QStringLiteral("取消导出：当前步骤完成后停止"));
    }
}

void SchemeExporter::onExportFinished()
{
    m_busy = false;
    bool ok = m_watcher->result();
    emit exportProgress(100);
    emit exportFinished(ok, m_lastMessage);
}

bool SchemeExporter::doExport(const ExportConfig& config,
                              const QVariantList& nodes,
                              const QVariantList& connections,
                              QString* resultMsg)
{
    // 1. 创建产物目录
    QString packageDir = config.outputPath + QStringLiteral("/") + config.exportName;
    QDir().mkpath(packageDir);
    if (!QDir(packageDir).exists()) {
        *resultMsg = QStringLiteral("无法创建产物目录: ") + packageDir;
        return false;
    }
    emit exportProgress(15);

    // 2. 复制模板二进制
    QString binDir = packageDir + QStringLiteral("/") + ExportPackage::binDir();
    QDir().mkpath(binDir);

    if (config.exportDll) {
        if (!QFile::copy(TemplateLocator::dllTemplatePath(),
                         binDir + QStringLiteral("/") + ExportPackage::dllName())) {
            // DLL 可能已存在（重复导出），尝试删除后复制
            QFile::remove(binDir + QStringLiteral("/") + ExportPackage::dllName());
            QFile::copy(TemplateLocator::dllTemplatePath(),
                        binDir + QStringLiteral("/") + ExportPackage::dllName());
        }
        // 复制导入库（.a）
        QString libPath = QFileInfo(TemplateLocator::dllTemplatePath()).absolutePath() +
                          QStringLiteral("/libQDVPipeline.a");
        if (QFileInfo::exists(libPath)) {
            QDir libDir(packageDir + QStringLiteral("/lib"));
            libDir.mkpath(".");
            QFile::copy(libPath, libDir.absolutePath() + QStringLiteral("/libQDVPipeline.a"));
        }
    }
    if (config.exportExe) {
        QString exeDest = binDir + QStringLiteral("/") + ExportPackage::exeName();
        QFile::remove(exeDest);
        QFile::copy(TemplateLocator::exeTemplatePath(), exeDest);
    }
    emit exportProgress(35);

    // 3. 复制 C 接口头文件
    QString includeDir = packageDir + QStringLiteral("/") + ExportPackage::includeDir();
    QDir().mkpath(includeDir);
    QFile::copy(TemplateLocator::headerTemplatePath(),
                includeDir + QStringLiteral("/") + ExportPackage::headerName());
    emit exportProgress(45);

    // 4. 收集运行时依赖
    QString runtimeDir = packageDir + QStringLiteral("/") + ExportPackage::runtimeDir();
    QString pkgErr;
    QStringList copied;
    RuntimePackager::package(runtimeDir, &copied, &pkgErr);
    emit exportProgress(60);

    // 5. 序列化方案
    QString schemePath = packageDir + QStringLiteral("/") + ExportPackage::schemeFile();
    QDir().mkpath(QFileInfo(schemePath).absolutePath());
    if (!serializeScheme(schemePath, nodes, connections, config.exportName)) {
        *resultMsg = QStringLiteral("方案序列化失败");
        return false;
    }
    emit exportProgress(70);

    // 6. 检测 AI 算子
    bool containsAi = false;
    QStringList opTypes;
    for (const QVariant& v : nodes) {
        QVariantMap node = v.toMap();
        QString type = node.value("type").toString();
        opTypes << type;
        if (type == "AiClassify" || type.contains("Dl") || type.contains("Yolo")) {
            containsAi = true;
        }
    }

    // 7. 生成 Python wrapper
    if (config.exportPython) {
        QString pyPath = binDir + QStringLiteral("/") + ExportPackage::pyName();
        QFile pyFile(pyPath);
        if (pyFile.open(QIODevice::WriteOnly)) {
            pyFile.write(ExampleCodeGenerator::generatePythonWrapper(config).toUtf8());
            pyFile.close();
        }
    }
    emit exportProgress(80);

    // 8. 生成文档
    if (config.generateDoc) {
        QString docsDir = packageDir + QStringLiteral("/") + ExportPackage::docsDir();
        QDir().mkpath(docsDir);

        QFile docFile(docsDir + QStringLiteral("/接口文档.md"));
        if (docFile.open(QIODevice::WriteOnly)) {
            docFile.write(InterfaceDocGenerator::generate(config, nodes, containsAi).toUtf8());
            docFile.close();
        }

        QFile qsFile(docsDir + QStringLiteral("/使用说明.md"));
        if (qsFile.open(QIODevice::WriteOnly)) {
            qsFile.write(InterfaceDocGenerator::generateQuickStart(config).toUtf8());
            qsFile.close();
        }
    }
    emit exportProgress(88);

    // 9. 生成调用示例
    if (config.generateExamples) {
        QString exDir = packageDir + QStringLiteral("/") + ExportPackage::examplesDir();
        if (config.exportDll) {
            QDir cppDir(exDir + QStringLiteral("/cpp"));
            cppDir.mkpath(".");
            QFile mainFile(cppDir.absolutePath() + QStringLiteral("/main.cpp"));
            if (mainFile.open(QIODevice::WriteOnly)) {
                mainFile.write(ExampleCodeGenerator::generateCppMain(config).toUtf8());
                mainFile.close();
            }
            QFile cmakeFile(cppDir.absolutePath() + QStringLiteral("/CMakeLists.txt"));
            if (cmakeFile.open(QIODevice::WriteOnly)) {
                cmakeFile.write(ExampleCodeGenerator::generateCppCMake(config).toUtf8());
                cmakeFile.close();
            }
        }
        if (config.exportPython) {
            QDir pyDir(exDir + QStringLiteral("/python"));
            pyDir.mkpath(".");
            QFile pyFile(pyDir.absolutePath() + QStringLiteral("/main.py"));
            if (pyFile.open(QIODevice::WriteOnly)) {
                pyFile.write(ExampleCodeGenerator::generatePythonMain(config).toUtf8());
                pyFile.close();
            }
        }
    }
    emit exportProgress(95);

    // 10. 写 manifest.json
    if (!writeManifest(packageDir, config, nodes, containsAi)) {
        *resultMsg = QStringLiteral("manifest.json 写入失败");
        return false;
    }

    emit exportProgress(100);
    *resultMsg = packageDir;
    return true;
}

bool SchemeExporter::serializeScheme(const QString& outPath,
                                    const QVariantList& nodes,
                                    const QVariantList& connections,
                                    const QString& schemeName)
{
    QJsonObject root;
    root["version"] = QStringLiteral("2.1.0");
    root["schemeName"] = schemeName;
    root["savedAt"] = QDateTime::currentDateTime().toString(Qt::ISODate);

    QJsonArray nodeArray;
    for (const QVariant& v : nodes) {
        QJsonObject node = QJsonObject::fromVariantMap(v.toMap());
        nodeArray.append(node);
    }
    root["nodes"] = nodeArray;

    QJsonArray connArray;
    for (const QVariant& v : connections) {
        QJsonObject conn = QJsonObject::fromVariantMap(v.toMap());
        connArray.append(conn);
    }
    root["connections"] = connArray;

    QJsonArray emptyVars;
    root["variables"] = emptyVars;

    QFile file(outPath);
    if (!file.open(QIODevice::WriteOnly)) return false;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.close();
    return true;
}

bool SchemeExporter::writeManifest(const QString& dir,
                                  const ExportConfig& config,
                                  const QVariantList& nodes,
                                  bool containsAi)
{
    QJsonObject manifest;
    manifest["exportName"] = config.exportName;
    manifest["interfaceName"] = config.interfaceName;
    manifest["version"] = config.version;
    manifest["author"] = config.author;
    manifest["description"] = config.description;
    manifest["exportedAt"] = QDateTime::currentDateTime().toString(Qt::ISODate);

    QJsonArray formats;
    if (config.exportDll)    formats.append("dll");
    if (config.exportExe)    formats.append("exe");
    if (config.exportPython) formats.append("python");
    manifest["formats"] = formats;

    manifest["schemeEmbedded"] = config.embedScheme;

    QJsonArray ops;
    for (const QVariant& v : nodes) {
        ops.append(v.toMap().value("type").toString());
    }
    manifest["operators"] = ops;
    manifest["containsAi"] = containsAi;
    manifest["qdvVersion"] = QStringLiteral("1.0.0");

    QFile file(dir + QStringLiteral("/") + ExportPackage::manifestFile());
    if (!file.open(QIODevice::WriteOnly)) return false;
    file.write(QJsonDocument(manifest).toJson(QJsonDocument::Indented));
    file.close();
    return true;
}
