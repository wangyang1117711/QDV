#ifndef TEMPLATE_LOCATOR_H
#define TEMPLATE_LOCATOR_H

#include <QString>

/**
 * @brief 定位模板预编译产物
 *
 * 模板路径由编译定义 QDV_TEMPLATE_BIN_DIR 注入（见 src/Export/CMakeLists.txt）。
 * 当未定义时回退到 ${CMAKE_BINARY_DIR}/templates/bin。
 */
class TemplateLocator {
public:
    /// 模板二进制目录
    static QString templateBinDir();

    /// wrapper DLL 模板路径
    static QString dllTemplatePath();

    /// EXE 模板路径
    static QString exeTemplatePath();

    /// C 接口头文件模板路径
    static QString headerTemplatePath();

    /// 检查所有必需模板是否存在
    static bool checkTemplates(QString* missing = nullptr);
};

#endif // TEMPLATE_LOCATOR_H
