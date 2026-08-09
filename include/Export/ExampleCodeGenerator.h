#ifndef EXAMPLE_CODE_GENERATOR_H
#define EXAMPLE_CODE_GENERATOR_H

#include <QString>

struct ExportConfig;

/**
 * @brief 生成 C++/Python 调用示例代码
 */
class ExampleCodeGenerator {
public:
    /// 生成 C++ 调用示例（main.cpp + CMakeLists.txt）
    /// @return {mainCpp, cmakeLists} 失败返回空字符串
    static QString generateCppMain(const ExportConfig& config);
    static QString generateCppCMake(const ExportConfig& config);

    /// 生成 Python 调用示例（main.py）
    static QString generatePythonMain(const ExportConfig& config);

    /// 生成 Python wrapper 模块（qdv_pipeline.py，ctypes 加载 DLL）
    static QString generatePythonWrapper(const ExportConfig& config);
};

#endif // EXAMPLE_CODE_GENERATOR_H
