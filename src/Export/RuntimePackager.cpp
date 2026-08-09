// RuntimePackager.cpp - 收集运行时依赖 DLL

#include "Export/RuntimePackager.h"

#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QCoreApplication>
#include <QProcess>
#include <QSettings>

// OpenCV 运行时路径（来自顶层 CMakeLists.txt 的 OpenCV_BIN_DIR）
// 硬编码回退到 D:/opencv/build_mingw/bin（与项目约定一致）
#ifndef OPENCV_BIN_DIR
  #define OPENCV_BIN_DIR "D:/opencv/build_mingw/bin"
#endif

bool RuntimePackager::copyFile(const QString& src, const QString& destDir,
                                QString* err)
{
    QFileInfo fi(src);
    if (!fi.exists()) {
        if (err) *err = QStringLiteral("源文件不存在: %1").arg(src);
        return false;
    }
    QDir().mkpath(destDir);
    QString dest = destDir + QStringLiteral("/") + fi.fileName();
    if (QFileInfo::exists(dest)) {
        return true;  // 已存在则跳过
    }
    if (!QFile::copy(src, dest)) {
        if (err) *err = QStringLiteral("复制失败: %1 -> %2").arg(src, dest);
        return false;
    }
    return true;
}

QStringList RuntimePackager::findOpencvDlls()
{
    QStringList result;
    QDir dir(QString::fromUtf8(OPENCV_BIN_DIR));
    if (!dir.exists()) return result;

    // opencv_world4xx.dll 或独立模块 dll
    QStringList filters;
    filters << QStringLiteral("opencv_world*.dll")
            << QStringLiteral("libopencv_world*.dll")
            << QStringLiteral("opencv_core*.dll");
    QStringList files = dir.entryList(filters, QDir::Files);
    for (const QString& f : files) {
        result << dir.absoluteFilePath(f);
    }
    return result;
}

QStringList RuntimePackager::findQtDlls()
{
    QStringList result;
    // Qt DLL 路径：从 QCoreApplication::applicationDirPath() 推导
    // 或从环境变量 QT_BIN_DIR
    QString qtBin = QString::fromUtf8(qgetenv("QT_BIN_DIR"));
    if (qtBin.isEmpty()) {
        // 回退：从 qmake 路径推导
        qtBin = QCoreApplication::applicationDirPath();
    }

    QDir dir(qtBin);
    if (!dir.exists()) return result;

    // 精简运行时：仅 Core/Gui（不含 QML）
    QStringList needed;
    needed << QStringLiteral("Qt6Core.dll")
           << QStringLiteral("Qt6Gui.dll");

    for (const QString& name : needed) {
        QString path = dir.absoluteFilePath(name);
        if (QFileInfo::exists(path)) {
            result << path;
        }
    }
    return result;
}

QStringList RuntimePackager::findQtPlatformPlugins()
{
    QStringList result;
    QString qtBin = QString::fromUtf8(qgetenv("QT_BIN_DIR"));
    if (qtBin.isEmpty()) {
        qtBin = QCoreApplication::applicationDirPath();
    }

    // platforms/qwindows.dll
    QString pluginPath = qtBin + QStringLiteral("/../plugins/platforms/qwindows.dll");
    if (QFileInfo::exists(pluginPath)) {
        result << pluginPath;
    }
    return result;
}

bool RuntimePackager::package(const QString& destDir,
                             QStringList* copiedFiles,
                             QString* err)
{
    QStringList allCopied;
    QString localErr;

    // 1. OpenCV DLL
    for (const QString& src : findOpencvDlls()) {
        if (copyFile(src, destDir, &localErr)) {
            allCopied << QFileInfo(src).fileName();
        }
    }

    // 2. Qt 基础 DLL
    for (const QString& src : findQtDlls()) {
        if (copyFile(src, destDir, &localErr)) {
            allCopied << QFileInfo(src).fileName();
        }
    }

    // 3. Qt platforms 插件
    QString platformsDir = destDir + QStringLiteral("/platforms");
    for (const QString& src : findQtPlatformPlugins()) {
        if (copyFile(src, platformsDir, &localErr)) {
            allCopied << QStringLiteral("platforms/") + QFileInfo(src).fileName();
        }
    }

    // 4. operators.json（算子元数据）
    // 从 config/ 目录复制
    QString operatorsJson = QCoreApplication::applicationDirPath() +
                            QStringLiteral("/config/operators.json");
    if (!QFileInfo::exists(operatorsJson)) {
        // 回退到源码树
        operatorsJson = QDir::currentPath() + QStringLiteral("/config/operators.json");
    }
    if (QFileInfo::exists(operatorsJson)) {
        copyFile(operatorsJson, destDir + QStringLiteral("/config"), &localErr);
    }

    if (copiedFiles) *copiedFiles = allCopied;

    // 即使部分失败也返回 true（运行时依赖缺失不阻断导出，文档会提示）
    return true;
}
