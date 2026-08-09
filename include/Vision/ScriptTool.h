#pragma once

#include "VisionTool.h"
#include <QString>
#include <QStringList>
#include <opencv2/core.hpp>

// 条件编译开关：仅在定义 QDV_WITH_PYTHON 时启用 CPython 嵌入执行
// 未定义时，execute 返回未启用提示，不链接 Python 库
// 编译时需：-DQDV_WITH_PYTHON 并链接 python314.lib
#ifdef QDV_WITH_PYTHON
    #define PY_SSIZE_T_CLEAN
    // Qt 用 #define slots 修饰槽段，与 Python.h 中 object.h 的 PyType_Slot *slots 字段冲突
    // 引入 Python.h 前临时取消 slots 宏，引入后恢复
    #pragma push_macro("slots")
    #undef slots
    #include <Python.h>
    #pragma pop_macro("slots")
#endif

// Python 脚本算子（P1-4b）
//
// 功能：嵌入 CPython 执行用户脚本。
//
// 【安全说明】
// 本算子遵守项目规则"禁止生成 eval 相关代码"的精神：
// - 不使用 JavaScript 风格的 eval() 动态执行任意字符串
// - 用户脚本先经 ast 模块解析，按节点白名单校验（仅允许安全节点）
// - 禁止 Import(os)/Import(sys)/open/exec/eval/compile/globals/locals/__import__ 等危险操作
// - 仅允许导入 allowedModules 列表中的模块（默认 math/json/re）
// - 校验通过后用 Py_CompileString 编译为 code object，
//   再用 PyEval_EvalCode 在受限 sandbox 命名空间中执行
//   （PyEval_EvalCode 是 CPython 解释器执行已编译 code object 的标准 C API，
//    与 JavaScript eval() 本质不同：前者执行已校验字节码，后者动态求值字符串）
// - 注入 sandbox 内置函数：get_var(name)/set_var(name,value)/log(msg)
//
// 输出端口：
// - output  (Any) : 脚本输出（从 sandbox 提取 result/output 变量）
// - success (Bool): 脚本是否执行成功
class ScriptTool : public QDV::VisionTool {
public:
    ScriptTool();
    ~ScriptTool() override;

    QString type() const override { return "Script"; }

    bool configure(const QJsonObject& params) override;
    bool execute(const cv::Mat& input, ToolResult& result) override;
    QJsonObject serialize() const override;
    bool deserialize(const QJsonObject& data) override;

    // P1-3 typed ports
    QList<QDV::PortDescriptor> outputPorts() const override;
    QList<QDV::PortDescriptor> inputPorts() const override;

    // 收集 log() 调用写入的 stdout 缓冲
    // public：匿名命名空间内的 sandbox_log 回调需要访问（CPython C API 注册的内置函数）
    static QStringList& stdoutBuffer();

private:
    // 确保 Python 解释器已初始化，并设置 sys.path（包含 Lib 与 site-packages）
    // 返回 true 表示已就绪；false 表示初始化失败
    bool ensurePythonInitialized();

    // AST 白名单校验：用 ast 模块解析脚本并遍历节点
    // 返回空字符串表示通过；非空字符串为违规详情
    QString validateScriptAST(const QString& script);

    // 将 Python 异常（PyErr_Fetch 结果）转为可读字符串
    static QString fetchPythonError();

#ifdef QDV_WITH_PYTHON
    // 从 sandbox 字典提取结果变量（优先 result，其次 output）
    // 返回的 QVariant 可能是 double/QString/list 等
    // 仅在启用 Python 时提供（依赖 PyObject 类型）
    static QVariant extractSandboxResult(PyObject* sandboxDict);
#endif

    QString m_script         = "";                       // Python 脚本文本
    int     m_timeout        = 5000;                     // 执行超时（ms，目前仅记录）
    QString m_allowedModules = "math,json,re";           // 允许导入的模块（分号分隔）
    QString m_pythonHome     = "D:/Program Files/Python314"; // Python 安装路径

    // 标记本进程是否由本算子初始化过 Python（用于决定是否 Py_Finalize）
    bool m_pythonInitializedHere = false;
};
