#ifndef RUNTIME_PACKAGER_H
#define RUNTIME_PACKAGER_H

#include <QString>
#include <QStringList>

/**
 * @brief 收集运行时依赖 DLL（OpenCV/Qt）到指定目录
 *
 * 模仿 windeployqt 行为，手动复制必要的运行时 DLL。
 * 精简运行时不含 QML（符合 spec 约束）。
 */
class RuntimePackager {
public:
    /// 收集运行时依赖到目标目录
    /// @param destDir 目标目录（通常是 bin/qdv_runtime/）
    /// @param copiedFiles 返回已复制的文件列表
    /// @return true=成功；false=部分失败（错误信息见 missing）
    static bool package(const QString& destDir,
                        QStringList* copiedFiles = nullptr,
                        QString* err = nullptr);

private:
    /// 复制单个文件（若已存在则跳过）
    static bool copyFile(const QString& src, const QString& destDir,
                         QString* err = nullptr);

    /// 查找 OpenCV 运行时 DLL
    static QStringList findOpencvDlls();

    /// 查找 Qt 运行时基础 DLL（不含 QML 模块）
    static QStringList findQtDlls();

    /// 查找 Qt platforms 插件
    static QStringList findQtPlatformPlugins();
};

#endif // RUNTIME_PACKAGER_H
