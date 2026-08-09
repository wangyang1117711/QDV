#ifndef EXPORT_CONFIG_H
#define EXPORT_CONFIG_H

#include <QString>
#include <QStringList>

/**
 * @brief 导出配置（由 ExportDialog.qml 收集，传给 SchemeExporter）
 */
struct ExportConfig {
    // 基础项
    bool exportDll = true;
    bool exportExe = true;
    bool exportPython = true;
    QString exportName;        ///< 导出包名（目录名）
    QString interfaceName;     ///< 接口名（Python 类名/C++ 示例类名/文档命名）
    QString outputPath;        ///< 输出根目录（默认 D:\exports\）

    // 额外项
    bool embedScheme = true;   ///< 方案嵌入 DLL/EXE 还是外置文件
    bool generateDoc = true;   ///< 生成 Markdown 接口文档
    bool generateExamples = true; ///< 生成调用示例代码
    QString version = "1.0.0"; ///< SemVer
    QString author;
    QString description;

    // 自检（可选，不阻断导出）
    bool runSelfCheck = false;
    QString selfCheckSampleImage;

    /// 校验配置完整性
    bool isValid(QString* err = nullptr) const {
        if (exportName.isEmpty()) {
            if (err) *err = QStringLiteral("导出名称不能为空");
            return false;
        }
        if (interfaceName.isEmpty()) {
            if (err) *err = QStringLiteral("接口名称不能为空");
            return false;
        }
        if (outputPath.isEmpty()) {
            if (err) *err = QStringLiteral("输出路径不能为空");
            return false;
        }
        if (!exportDll && !exportExe && !exportPython) {
            if (err) *err = QStringLiteral("至少选择一种导出格式");
            return false;
        }
        return true;
    }
};

#endif // EXPORT_CONFIG_H
