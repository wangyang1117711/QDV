#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QDir>
#include "Logger.h"
#include "SchemeManager.h"
#include "ToolFactory.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setOrganizationName("QDetectVision");
    app.setApplicationName("Q-DetectVision");
    app.setApplicationVersion("V2.0-0809");
    
    Logger::initialize(QDir::currentPath() + "/logs");
    Logger::info("Q-DetectVision v2.0-0809 starting...");
    
    ToolFactory::instance();
    
    QQmlApplicationEngine engine;
    
    const QUrl url(u"qrc:/main.qml"_qs);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated,
                     &app, [url](QObject *obj, const QUrl &objUrl) {
        if (!obj && url == objUrl)
            QCoreApplication::exit(-1);
    }, Qt::QueuedConnection);
    
    engine.load(url);
    
    return app.exec();
}