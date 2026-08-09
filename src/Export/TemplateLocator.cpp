// TemplateLocator.cpp - 模板路径定位

#include "Export/TemplateLocator.h"
#include "Export/ExportPackage.h"

#include <QDir>
#include <QFileInfo>
#include <QCoreApplication>

// 编译定义注入（src/Export/CMakeLists.txt target_compile_definitions）
#ifndef QDV_TEMPLATE_BIN_DIR
  #define QDV_TEMPLATE_BIN_DIR "."
#endif

QString TemplateLocator::templateBinDir()
{
    // 优先用编译定义的路径
    QString dir = QString::fromUtf8(QDV_TEMPLATE_BIN_DIR);
    if (QDir(dir).exists()) return dir;

    // 回退 1：构建目录下 templates/bin
    dir = QDir::currentPath() + QStringLiteral("/templates/bin");
    if (QDir(dir).exists()) return dir;

    // 回退 2：可执行文件同级 templates/bin
    dir = QCoreApplication::applicationDirPath() + QStringLiteral("/templates/bin");
    return dir;
}

QString TemplateLocator::dllTemplatePath()
{
    return templateBinDir() + QStringLiteral("/") + ExportPackage::dllName();
}

QString TemplateLocator::exeTemplatePath()
{
    return templateBinDir() + QStringLiteral("/") + ExportPackage::exeName();
}

QString TemplateLocator::headerTemplatePath()
{
    // 头文件在 templates/include/ 下（源码树）
    // 从编译定义推导：QDV_TEMPLATE_BIN_DIR 是 .../templates/bin
    // 头文件在 .../templates/include/QDVPipeline.h
    QString binDir = templateBinDir();
    QDir dir(binDir);
    dir.cdUp();  // .../templates
    return dir.absolutePath() + QStringLiteral("/include/") + ExportPackage::headerName();
}

bool TemplateLocator::checkTemplates(QString* missing)
{
    QStringList missingList;

    if (!QFileInfo::exists(dllTemplatePath())) {
        missingList << dllTemplatePath();
    }
    if (!QFileInfo::exists(exeTemplatePath())) {
        missingList << exeTemplatePath();
    }
    if (!QFileInfo::exists(headerTemplatePath())) {
        missingList << headerTemplatePath();
    }

    if (missing && !missingList.isEmpty()) {
        *missing = missingList.join(QStringLiteral("; "));
    }
    return missingList.isEmpty();
}
