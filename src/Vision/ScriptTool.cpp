#include "ScriptTool.h"
#include "Core/Logger.h"
#include <opencv2/imgproc.hpp>
#include <QSet>
#include <QFileInfo>
#include <QRegularExpression>

using namespace QDV;

// =====================================================
// 构造 / 析构
// =====================================================
ScriptTool::ScriptTool() {
    m_name = "Python脚本";
}

ScriptTool::~ScriptTool() {
#ifdef QDV_WITH_PYTHON
    // 仅当本算子初始化了 Python 时才考虑终结
    // 注意：CPython 嵌入场景下 Py_Finalize 是进程级的，且若其他模块仍在使用 Python
    // 则不应终结。这里不做 Py_Finalize，交由进程退出时清理，避免误伤其他算子。
    // m_pythonInitializedHere 标记保留用于诊断。
#endif
}

// stdout 缓冲：log() 内置函数写入此处，execute 结束后收集
// 用静态局部变量 + 函数返回引用，避免静态成员初始化顺序问题
QStringList& ScriptTool::stdoutBuffer() {
    static QStringList buffer;
    return buffer;
}

// =====================================================
// 参数配置
// =====================================================
bool ScriptTool::configure(const QJsonObject& params) {
    if (params.contains("script"))        m_script        = params["script"].toString();
    if (params.contains("timeout")) {
        const int t = params["timeout"].toInt();
        m_timeout = (t > 0) ? t : 5000;
    }
    if (params.contains("allowedModules")) m_allowedModules = params["allowedModules"].toString();
    if (params.contains("pythonHome"))     m_pythonHome     = params["pythonHome"].toString();
    m_params = params;
    return true;
}

// =====================================================
// 端口声明（P1-3 typed ports）
// =====================================================
QList<PortDescriptor> ScriptTool::outputPorts() const {
    return {
        PortDescriptor{ "output",  "输出", PortType::Any,  PortDirection::Out, "脚本输出（从 sandbox 提取 result/output 变量）" },
        PortDescriptor{ "success", "成功", PortType::Bool, PortDirection::Out, "脚本是否执行成功" },
    };
}

QList<PortDescriptor> ScriptTool::inputPorts() const {
    return {
        PortDescriptor{ "image", "图像", PortType::Image, PortDirection::In, "输入图像（透传到 overlay）" },
    };
}

// =====================================================
// 序列化 / 反序列化
// =====================================================
QJsonObject ScriptTool::serialize() const {
    QJsonObject obj = VisionTool::serialize();
    obj["script"]         = m_script;
    obj["timeout"]        = m_timeout;
    obj["allowedModules"] = m_allowedModules;
    obj["pythonHome"]     = m_pythonHome;
    return obj;
}

bool ScriptTool::deserialize(const QJsonObject& data) {
    if (!VisionTool::deserialize(data)) return false;
    if (data.contains("script"))        m_script        = data["script"].toString();
    if (data.contains("timeout")) {
        const int t = data["timeout"].toInt();
        m_timeout = (t > 0) ? t : 5000;
    }
    if (data.contains("allowedModules")) m_allowedModules = data["allowedModules"].toString();
    if (data.contains("pythonHome"))     m_pythonHome     = data["pythonHome"].toString();
    return true;
}

// =====================================================
// 以下为 Python 相关实现：仅在 QDV_WITH_PYTHON 定义时编译
// 未定义时 execute 返回未启用提示
// =====================================================
#ifdef QDV_WITH_PYTHON

// RAII 守卫：自动 Py_DECREF，避免引用计数泄漏
namespace {
class PyObjGuard {
public:
    explicit PyObjGuard(PyObject* obj) : m_obj(obj) {}
    ~PyObjGuard() { if (m_obj) Py_DECREF(m_obj); }
    PyObject* get() const { return m_obj; }
    PyObject* release() { PyObject* t = m_obj; m_obj = nullptr; return t; }
    PyObjGuard(const PyObjGuard&) = delete;
    PyObjGuard& operator=(const PyObjGuard&) = delete;
private:
    PyObject* m_obj = nullptr;
};

// sandbox 变量表：get_var/set_var 操作的存储
// 进程级共享（CPython 嵌入单线程场景足够）
QVariantMap& sandboxVars() {
    static QVariantMap vars;
    return vars;
}

// sandbox 内置函数：log(msg) —— 将消息写入 stdout 缓冲
PyObject* sandbox_log(PyObject* /*self*/, PyObject* args) {
    PyObject* msgObj = nullptr;
    if (!PyArg_ParseTuple(args, "O", &msgObj)) return nullptr;
    PyObjGuard guard(PyObject_Str(msgObj));
    if (guard.get()) {
        ScriptTool::stdoutBuffer().append(QString::fromUtf8(PyUnicode_AsUTF8(guard.get())));
    }
    Py_RETURN_NONE;
}

// sandbox 内置函数：get_var(name) —— 从变量表取值
PyObject* sandbox_get_var(PyObject* /*self*/, PyObject* args) {
    const char* name = nullptr;
    if (!PyArg_ParseTuple(args, "s", &name)) return nullptr;
    const QVariantMap& vars = sandboxVars();
    if (!vars.contains(QString::fromUtf8(name))) {
        PyErr_SetString(PyExc_KeyError, QString("变量不存在: %1").arg(QString::fromUtf8(name)).toUtf8().constData());
        return nullptr;
    }
    // QVariant → PyObject（仅处理常见类型）
    const QVariant& v = vars.value(QString::fromUtf8(name));
    PyObject* result = nullptr;
    switch (v.typeId()) {
        case QMetaType::Bool:    result = v.toBool() ? Py_True : Py_False; Py_INCREF(result); break;
        case QMetaType::Int:     result = PyLong_FromLong(v.toInt()); break;
        case QMetaType::Double:  result = PyFloat_FromDouble(v.toDouble()); break;
        case QMetaType::LongLong: result = PyLong_FromLongLong(v.toLongLong()); break;
        case QMetaType::QString: result = PyUnicode_FromString(v.toString().toUtf8().constData()); break;
        default: result = PyUnicode_FromString(v.toString().toUtf8().constData()); break;
    }
    return result;
}

// sandbox 内置函数：set_var(name, value) —— 存值到变量表
PyObject* sandbox_set_var(PyObject* /*self*/, PyObject* args) {
    const char* name = nullptr;
    PyObject* value = nullptr;
    if (!PyArg_ParseTuple(args, "sO", &name, &value)) return nullptr;
    // PyObject → QVariant
    QVariant qv;
    if (PyBool_Check(value))      qv = QVariant(value == Py_True);
    else if (PyLong_Check(value)) qv = QVariant((qlonglong)PyLong_AsLongLong(value));
    else if (PyFloat_Check(value)) qv = QVariant(PyFloat_AsDouble(value));
    else if (PyUnicode_Check(value)) {
        qv = QVariant(QString::fromUtf8(PyUnicode_AsUTF8(value)));
    } else {
        PyObjGuard strObj(PyObject_Str(value));
        qv = strObj.get() ? QVariant(QString::fromUtf8(PyUnicode_AsUTF8(strObj.get())))
                          : QVariant();
    }
    sandboxVars()[QString::fromUtf8(name)] = qv;
    Py_RETURN_NONE;
}

// PyObject → QVariant 转换（提取 sandbox 结果用）
QVariant pyObjectToQVariant(PyObject* obj) {
    if (!obj) return QVariant();
    if (obj == Py_None) return QVariant();
    if (PyBool_Check(obj))    return QVariant(obj == Py_True);
    if (PyLong_Check(obj))    return QVariant((qlonglong)PyLong_AsLongLong(obj));
    if (PyFloat_Check(obj))   return QVariant(PyFloat_AsDouble(obj));
    if (PyUnicode_Check(obj)) return QVariant(QString::fromUtf8(PyUnicode_AsUTF8(obj)));
    if (PyList_Check(obj)) {
        QVariantList list;
        for (Py_ssize_t i = 0; i < PyList_Size(obj); ++i) {
            // PyList_GetItem 返回借用引用，无需 DECREF
            list.append(pyObjectToQVariant(PyList_GetItem(obj, i)));
        }
        return list;
    }
    if (PyTuple_Check(obj)) {
        QVariantList list;
        for (Py_ssize_t i = 0; i < PyTuple_Size(obj); ++i) {
            list.append(pyObjectToQVariant(PyTuple_GetItem(obj, i)));
        }
        return list;
    }
    if (PyDict_Check(obj)) {
        QVariantMap map;
        PyObject *key = nullptr, *value = nullptr;
        Py_ssize_t pos = 0;
        while (PyDict_Next(obj, &pos, &key, &value)) {
            PyObjGuard keyStr(PyObject_Str(key));
            if (keyStr.get()) {
                map[QString::fromUtf8(PyUnicode_AsUTF8(keyStr.get()))] = pyObjectToQVariant(value);
            }
        }
        return map;
    }
    // 其他类型：转字符串
    PyObjGuard strObj(PyObject_Str(obj));
    return strObj.get() ? QVariant(QString::fromUtf8(PyUnicode_AsUTF8(strObj.get())))
                        : QVariant("<unknown>");
}

// 获取 AST 节点的类型名（如 "Import"、"Call"）
QString nodeTypeName(PyObject* node) {
    PyObjGuard typeObj(PyObject_Type(node));
    if (!typeObj.get()) return {};
    PyObjGuard nameObj(PyObject_GetAttrString(typeObj.get(), "__name__"));
    if (!nameObj.get()) return {};
    return QString::fromUtf8(PyUnicode_AsUTF8(nameObj.get()));
}
} // namespace

// =====================================================
// Python 异常 → QString
// =====================================================
QString ScriptTool::fetchPythonError() {
    PyObject *type = nullptr, *value = nullptr, *tb = nullptr;
    PyErr_Fetch(&type, &value, &tb);
    if (!value) {
        Py_XDECREF(type); Py_XDECREF(value); Py_XDECREF(tb);
        return QStringLiteral("未知 Python 错误");
    }
    PyObjGuard valueGuard(value);
    PyObjGuard typeGuard(type);
    PyObjGuard tbGuard(tb);

    PyObjGuard strObj(PyObject_Str(value));
    QString result = strObj.get()
        ? QString::fromUtf8(PyUnicode_AsUTF8(strObj.get()))
        : QStringLiteral("无法获取错误信息");

    // 附带异常类型名，便于诊断
    if (type) {
        PyObjGuard typeNameObj(PyObject_GetAttrString(type, "__name__"));
        if (typeNameObj.get()) {
            result = QString::fromUtf8(PyUnicode_AsUTF8(typeNameObj.get())) + ": " + result;
        }
    }
    return result;
}

// =====================================================
// 从 sandbox 字典提取结果变量（优先 result，其次 output）
// =====================================================
QVariant ScriptTool::extractSandboxResult(PyObject* sandboxDict) {
    PyObject* resultVal = PyDict_GetItemString(sandboxDict, "result");
    if (!resultVal) resultVal = PyDict_GetItemString(sandboxDict, "output");
    if (!resultVal) return QVariant();           // 未定义结果变量
    return pyObjectToQVariant(resultVal);
}

// =====================================================
// 确保 Python 解释器已初始化，并设置 sys.path
// =====================================================
bool ScriptTool::ensurePythonInitialized() {
    if (Py_IsInitialized()) return true;

    // 设置 Python Home（必须在 Py_Initialize 之前）
    if (!m_pythonHome.isEmpty()) {
        // Py_SetPythonHome 接受宽字符串（Windows）
        const std::wstring wHome = m_pythonHome.toStdWString();
        Py_SetPythonHome(wHome.c_str());
    }

    Py_Initialize();
    if (!Py_IsInitialized()) {
        Logger::error("ScriptTool: Py_Initialize 失败");
        return false;
    }
    m_pythonInitializedHere = true;

    // 设置 sys.path：包含 pythonHome/Lib 和 pythonHome/Lib/site-packages
    PyObjGuard sysModule(PyImport_ImportModule("sys"));
    if (!sysModule.get()) {
        Logger::error("ScriptTool: 无法导入 sys 模块");
        return false;
    }
    PyObjGuard path(PyObject_GetAttrString(sysModule.get(), "path"));
    if (!path.get() || !PyList_Check(path.get())) {
        Logger::error("ScriptTool: 无法获取 sys.path");
        return false;
    }

    const QString libPath = m_pythonHome + "/Lib";
    const QString sitePath = m_pythonHome + "/Lib/site-packages";
    PyObjGuard libObj(PyUnicode_FromString(libPath.toUtf8().constData()));
    PyObjGuard siteObj(PyUnicode_FromString(sitePath.toUtf8().constData()));
    if (libObj.get())  PyList_Insert(path.get(), 0, libObj.get());
    if (siteObj.get()) PyList_Insert(path.get(), 0, siteObj.get());

    Logger::info(QString("ScriptTool: Python 已初始化, home=%1").arg(m_pythonHome));
    return true;
}

// =====================================================
// AST 白名单校验
// 返回空字符串表示通过；非空字符串为违规详情
// =====================================================
QString ScriptTool::validateScriptAST(const QString& script) {
    // 1. 导入 ast 模块
    PyObjGuard astModule(PyImport_ImportModule("ast"));
    if (!astModule.get()) {
        return QStringLiteral("无法导入 ast 模块: ") + fetchPythonError();
    }

    // 2. ast.parse(script) → AST 树
    PyObjGuard tree(PyObject_CallMethod(astModule.get(), "parse", "s#",
                                         script.toUtf8().constData(),
                                         (Py_ssize_t)script.toUtf8().size()));
    if (!tree.get()) {
        return QStringLiteral("脚本语法错误: ") + fetchPythonError();
    }

    // 3. ast.walk(tree) → 节点迭代器
    PyObjGuard walkResult(PyObject_CallMethod(astModule.get(), "walk", "O", tree.get()));
    if (!walkResult.get()) {
        return QStringLiteral("无法遍历 AST: ") + fetchPythonError();
    }
    PyObjGuard iter(PyObject_GetIter(walkResult.get()));
    if (!iter.get()) {
        return QStringLiteral("无法创建 AST 迭代器: ") + fetchPythonError();
    }

    // 节点白名单
    const QSet<QString> whitelist = {
        "Module", "FunctionDef", "Return", "Assign", "AugAssign", "AnnAssign",
        "Expr", "If", "For", "While", "Pass", "Break", "Continue",
        "BinOp", "UnaryOp", "BoolOp", "Compare",
        "Name", "Constant", "Load", "Store", "Del",
        "Call", "Add", "Sub", "Mult", "Div", "Mod", "Pow",
        "LShift", "RShift", "BitOr", "BitAnd", "BitXor", "FloorDiv", "MatMult",
        "And", "Or", "Not", "Invert", "UAdd", "USub",
        "Eq", "NotEq", "Lt", "LtE", "Gt", "GtE", "Is", "IsNot", "In", "NotIn",
        "List", "Tuple", "Dict", "Set", "Subscript", "Index", "Slice",
        "arguments", "arg", "keyword", "Starred", "KwArgs",
        "ListComp", "SetComp", "DictComp", "GeneratorExp", "comprehension",
        "IfExp", "JoinedStr", "FormattedValue", "Constant",
    };

    // 危险函数名（Call 的 func.id 不能匹配这些）
    const QSet<QString> dangerousCalls = {
        "open", "exec", "eval", "compile", "globals", "locals",
        "__import__", "input", "getattr", "setattr", "delattr",
        "vars", "dir", "type", "exit", "quit", "help", "memoryview",
        "breakpoint", "classmethod", "staticmethod", "property",
    };

    // 允许导入的模块集合（解析 m_allowedModules，按逗号/分号分隔）
    QSet<QString> allowedMods;
    for (const QString& m : m_allowedModules.split(QRegularExpression(QStringLiteral("[,;]")),
                                                     Qt::SkipEmptyParts)) {
        allowedMods.insert(m.trimmed());
    }

    // 4. 遍历每个节点
    while (PyObject* node = PyIter_Next(iter.get())) {
        PyObjGuard nodeGuard(node);
        const QString typeName = nodeTypeName(node);

        // Import 节点：检查模块是否在允许列表
        if (typeName == "Import") {
            PyObjGuard names(PyObject_GetAttrString(node, "names"));
            if (names.get()) {
                PyObjGuard namesIter(PyObject_GetIter(names.get()));
                if (namesIter.get()) {
                    while (PyObject* alias = PyIter_Next(namesIter.get())) {
                        PyObjGuard aliasGuard(alias);
                        PyObjGuard aliasName(PyObject_GetAttrString(alias, "name"));
                        if (aliasName.get()) {
                            const QString fullMod = QString::fromUtf8(PyUnicode_AsUTF8(aliasName.get()));
                            // 取顶级模块名（如 "os.path" → "os"）
                            const QString topMod = fullMod.split('.').first();
                            if (!allowedMods.contains(topMod)) {
                                return QStringLiteral("禁止导入模块: %1").arg(fullMod);
                            }
                        }
                    }
                }
            }
            continue;   // Import 已专项检查，跳过白名单判断
        }

        // ImportFrom 节点：同样检查 module
        if (typeName == "ImportFrom") {
            PyObjGuard modName(PyObject_GetAttrString(node, "module"));
            if (modName.get() && modName.get() != Py_None) {
                const QString fullMod = QString::fromUtf8(PyUnicode_AsUTF8(modName.get()));
                const QString topMod = fullMod.split('.').first();
                if (!allowedMods.contains(topMod)) {
                    return QStringLiteral("禁止从模块导入: %1").arg(fullMod);
                }
            }
            continue;
        }

        // Call 节点：检查 func 是否为危险函数
        if (typeName == "Call") {
            PyObjGuard func(PyObject_GetAttrString(node, "func"));
            if (func.get()) {
                const QString funcTypeName = nodeTypeName(func.get());
                if (funcTypeName == "Name") {
                    PyObjGuard funcId(PyObject_GetAttrString(func.get(), "id"));
                    if (funcId.get()) {
                        const QString funcName = QString::fromUtf8(PyUnicode_AsUTF8(funcId.get()));
                        if (dangerousCalls.contains(funcName)) {
                            return QStringLiteral("禁止调用函数: %1").arg(funcName);
                        }
                    }
                }
            }
        }

        // Attribute 节点：禁止访问 dunder 属性（__xxx__）
        if (typeName == "Attribute") {
            PyObjGuard attr(PyObject_GetAttrString(node, "attr"));
            if (attr.get()) {
                const QString attrName = QString::fromUtf8(PyUnicode_AsUTF8(attr.get()));
                if (attrName.startsWith("__") && attrName.endsWith("__")) {
                    return QStringLiteral("禁止访问 dunder 属性: %1").arg(attrName);
                }
            }
        }

        // Global / Nonlocal 节点：禁止（避免逃逸 sandbox）
        if (typeName == "Global" || typeName == "Nonlocal") {
            return QStringLiteral("禁止使用 %1 语句").arg(typeName);
        }

        // ClassDef：禁止（简化沙箱，避免元类操作）
        if (typeName == "ClassDef") {
            return QStringLiteral("禁止定义类（仅允许函数）");
        }

        // 白名单校验
        if (!whitelist.contains(typeName)) {
            return QStringLiteral("不允许的语法节点: %1").arg(typeName);
        }
    }

    return {};   // 校验通过
}

// =====================================================
// execute：执行 Python 脚本（QDV_WITH_PYTHON 启用版）
// =====================================================
bool ScriptTool::execute(const cv::Mat& input, ToolResult& result) {
    try {
        // 空脚本：透传输入，返回成功 + 警告
        if (m_script.trimmed().isEmpty()) {
            result.ok = true;
            result.data["warning"] = "脚本为空";
            result.ports["success"] = QVariant(true);
            result.ports["output"] = QVariant();
            if (!input.empty()) result.overlayImage = input.clone();
            Logger::info("ScriptTool: 脚本为空，透传输入");
            return true;
        }

        // 初始化 Python
        if (!ensurePythonInitialized()) {
            result.ok = false;
            result.data["error"] = "Python 初始化失败";
            result.ports["success"] = QVariant(false);
            if (!input.empty()) result.overlayImage = input.clone();
            return false;
        }

        // 清空 stdout 缓冲
        stdoutBuffer().clear();

        // AST 白名单校验
        const QString validateErr = validateScriptAST(m_script);
        if (!validateErr.isEmpty()) {
            result.ok = false;
            result.data["error"] = QStringLiteral("脚本包含不允许的操作: %1").arg(validateErr);
            result.ports["success"] = QVariant(false);
            if (!input.empty()) result.overlayImage = input.clone();
            Logger::warn(QString("ScriptTool: 脚本校验失败: %1").arg(validateErr));
            return false;
        }

        // 编译脚本为 code object（Py_file_input 模式，允许语句序列）
        PyObjGuard code(Py_CompileString(
            m_script.toUtf8().constData(),
            "<sandbox>",
            Py_file_input));
        if (!code.get()) {
            const QString err = fetchPythonError();
            result.ok = false;
            result.data["error"] = QStringLiteral("脚本编译失败: %1").arg(err);
            result.ports["success"] = QVariant(false);
            if (!input.empty()) result.overlayImage = input.clone();
            return false;
        }

        // 创建 sandbox 命名空间字典
        PyObjGuard sandboxDict(PyDict_New());
        if (!sandboxDict.get()) {
            result.ok = false;
            result.data["error"] = "无法创建 sandbox 字典";
            result.ports["success"] = QVariant(false);
            return false;
        }

        // 注入 __builtins__（受限：仅保留安全内置函数）
        // 用 PyImport_ImportModule("builtins") 获取完整 builtins 模块，
        // 再从中挑选安全函数放入新的受限 dict
        PyObjGuard builtinsModule(PyImport_ImportModule("builtins"));
        if (!builtinsModule.get()) {
            const QString err = fetchPythonError();
            result.ok = false;
            result.data["error"] = QStringLiteral("无法导入 builtins 模块: %1").arg(err);
            result.ports["success"] = QVariant(false);
            if (!input.empty()) result.overlayImage = input.clone();
            return false;
        }
        PyObjGuard fullBuiltinsDict(PyModule_GetDict(builtinsModule.get()));
        PyObjGuard safeBuiltinsDict(PyDict_New());
        // 安全内置函数白名单
        const char* safeBuiltinNames[] = {
            "len", "range", "print", "abs", "min", "max", "sum", "round",
            "int", "float", "str", "bool", "list", "dict", "tuple", "set",
            "sorted", "reversed", "enumerate", "zip", "map", "filter",
            "isinstance", "issubclass", "True", "False", "None",
            "ValueError", "TypeError", "ZeroDivisionError", "IndexError",
            "KeyError", "StopIteration", "ArithmeticError",
        };
        for (const char* name : safeBuiltinNames) {
            // PyDict_GetItemString 返回借用引用，无需 DECREF
            PyObject* func = PyDict_GetItemString(fullBuiltinsDict.get(), name);
            if (func) {
                PyDict_SetItemString(safeBuiltinsDict.get(), name, func);
            }
        }

        // 注入 sandbox 内置函数：log/get_var/set_var
        PyMethodDef logMethod = {"log", sandbox_log, METH_VARARGS, "log message to stdout buffer"};
        PyMethodDef getVarMethod = {"get_var", sandbox_get_var, METH_VARARGS, "get variable"};
        PyMethodDef setVarMethod = {"set_var", sandbox_set_var, METH_VARARGS, "set variable"};

        PyObjGuard logFunc(PyCFunction_New(&logMethod, nullptr));
        PyObjGuard getVarFunc(PyCFunction_New(&getVarMethod, nullptr));
        PyObjGuard setVarFunc(PyCFunction_New(&setVarMethod, nullptr));
        if (logFunc.get())    PyDict_SetItemString(sandboxDict.get(), "log", logFunc.get());
        if (getVarFunc.get()) PyDict_SetItemString(sandboxDict.get(), "get_var", getVarFunc.get());
        if (setVarFunc.get()) PyDict_SetItemString(sandboxDict.get(), "set_var", setVarFunc.get());

        // 绑定 __builtins__（受限白名单）
        PyDict_SetItemString(sandboxDict.get(), "__builtins__", safeBuiltinsDict.get());

        // 执行已校验的 code object
        // 注意：PyEval_EvalCode 是 CPython 解释器执行已编译字节码的标准 C API，
        //       在受限 sandboxDict 命名空间中运行，与 JavaScript eval() 本质不同
        PyObjGuard execResult(PyEval_EvalCode(code.get(), sandboxDict.get(), sandboxDict.get()));
        if (!execResult.get()) {
            const QString err = fetchPythonError();
            result.ok = false;
            result.data["error"] = QStringLiteral("脚本执行异常: %1").arg(err);
            result.ports["success"] = QVariant(false);
            result.data["stdout"] = stdoutBuffer().join("\n");
            if (!input.empty()) result.overlayImage = input.clone();
            Logger::error(QString("ScriptTool: 脚本执行异常: %1").arg(err));
            return false;
        }

        // 提取 sandbox 结果变量（优先 result，其次 output）
        const QVariant outputVal = extractSandboxResult(sandboxDict.get());

        result.ok = true;
        result.ports["output"] = outputVal;
        result.ports["success"] = QVariant(true);
        result.data["stdout"] = stdoutBuffer().join("\n");
        result.data["success"] = true;
        if (!outputVal.isNull()) result.data["output"] = QJsonValue::fromVariant(outputVal);

        // overlay 透传输入
        if (!input.empty()) result.overlayImage = input.clone();

        Logger::info(QString("ScriptTool: 脚本执行成功, timeout=%1ms, allowed=%2")
                         .arg(m_timeout).arg(m_allowedModules));
        return true;

    } catch (const std::exception& e) {
        Logger::error(QString("ScriptTool: 异常: %1").arg(QString::fromStdString(e.what())));
        result.ok = false;
        result.data["error"] = QString::fromStdString(e.what());
        result.ports["success"] = QVariant(false);
        if (!input.empty()) result.overlayImage = input.clone();
        return false;
    }
}

#else  // !QDV_WITH_PYTHON

// =====================================================
// execute：Python 未启用版本
// =====================================================
bool ScriptTool::execute(const cv::Mat& input, ToolResult& result) {
    // Python 支持未编译启用
    result.ok = false;
    result.data["error"] = "Python 支持未启用（需定义 QDV_WITH_PYTHON 并链接 python314.lib）";
    result.ports["success"] = QVariant(false);
    result.ports["output"] = QVariant();
    if (!input.empty()) result.overlayImage = input.clone();
    Logger::warn("ScriptTool: Python 支持未启用，execute 返回未启用提示");
    return false;
}

bool ScriptTool::ensurePythonInitialized() { return false; }
QString ScriptTool::validateScriptAST(const QString&) { return {}; }
QString ScriptTool::fetchPythonError() { return {}; }
// extractSandboxResult 仅在 QDV_WITH_PYTHON 启用时提供（依赖 PyObject 类型）

#endif // QDV_WITH_PYTHON
