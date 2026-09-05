// =====================================================================
// main.cpp — QDV 数据标注工具 入口
//
// 集成要点：
// - 标注逻辑后端由 AnnotationSession 提供，纯 Qt（无需 OpenCV）。
// - 使用本地 QDVAnnotator QML 模块（tools/qdv-annotator/qml/QDVAnnotator/：
//   AnnotateOverlay.qml 绘制层 + DesignTokens.qml 设计令牌），
//   与主工程 QDV.EditView 完全隔离，界面视觉保持一致且可独立演进。
// =====================================================================
#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QDebug>

#include "src/AnnotationSession.h"
#include "src/ShapeRegistry.h"

int main(int argc, char* argv[])
{
    // 强制 Basic 非原生样式（与主工程 SmartVision 一致）：
    // Windows 原生样式不支持 background 定制，会导致 TextField/Frame 等控件的
    // 本地设计令牌配色（圆角、边框色）被忽略并刷 QML 警告。
    qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");

    QApplication app(argc, argv);
    app.setApplicationName("QDV Annotator");
    app.setOrganizationName("QDV");

    QQmlApplicationEngine engine;

    // 本地 QDVAnnotator 模块（qrc 打包优先，随 exe 一起发布无需源码目录）
    engine.addImportPath(QStringLiteral("qrc:/qml"));

    // 开发环境退化路径：从文件系统定位 tools/qdv-annotator/qml/ 目录。
    // 策略：优先环境变量 QDV_QML_IMPORT；否则从可执行文件目录向上回溯至多 6 层，
    // 自动命中任意启动位置的 QDVAnnotator/qmldir（无需关心从哪个目录启动）。
    QStringList candidates;
    if (qEnvironmentVariableIsSet("QDV_QML_IMPORT"))
        candidates << qEnvironmentVariable("QDV_QML_IMPORT");
    QDir updir(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 6; ++i) {
        candidates << QDir(updir.absolutePath() + "/qml").absolutePath(); // 标注工具本地 qml/ 目录
        if (!updir.cdUp()) break;
    }
    candidates << QDir(QDir::currentPath() + "/qml").absolutePath();
    for (const QString& c : candidates) {
        if (QFileInfo(c + "/QDVAnnotator/qmldir").exists()) {
            engine.addImportPath(QDir(c).absolutePath());
            qDebug() << "[QDV Annotator] 使用本地 QDVAnnotator 模块于:" << QDir(c).absolutePath();
            break;
        }
    }

    // 后端会话对象，作为全局上下文属性暴露给 QML
    AnnotationSession session;
    engine.rootContext()->setContextProperty("session", &session);

    // 能力注册表（S0）：读取 capabilities.json，作为全局上下文属性 "capabilities"
    // 暴露给 QML 分派逻辑。加新形状/导出/训练任务只需改配置文件，无需改代码。
    // 显式优先从运行目录的 capabilities.json 加载（随 exe 一起发布），qrc 兜底。
    ShapeRegistry capabilities;
    capabilities.load(QDir(QCoreApplication::applicationDirPath() + "/capabilities.json").absolutePath());
    // 开发时若 exe 目录无配置文件，尝试回溯到源码目录（便于直接运行不打补丁）
    if (!capabilities.loaded())
        capabilities.load(QDir(QCoreApplication::applicationDirPath() + "/../capabilities.json").absolutePath());
    engine.rootContext()->setContextProperty("capabilities", &capabilities);

    const QUrl url(QStringLiteral("qrc:/qml/Main.qml"));
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated,
                     &app, [url, &session](QObject* obj, const QUrl& loaded) {
                         if (!obj && loaded == url) QCoreApplication::exit(-1);
                         // 命令行/拖拽/脚本方式传入 .qdvann 工程路径，加载完成后自动打开。
                         const QStringList args = QCoreApplication::arguments();
                         if (obj && args.size() >= 2) {
                             const QString proj = args.at(1);
                             if (QFileInfo(proj).isFile()) {
                                 qDebug() << "[QDV Annotator] 命令行打开工程:" << proj;
                                 session.openProject(proj);
                             }
                         }
                     }, Qt::QueuedConnection);
    engine.load(url);

    return app.exec();
}
