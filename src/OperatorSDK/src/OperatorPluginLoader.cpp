#include "OperatorSDK/OperatorManifest.h"
#include "OperatorSDK/IOperator.h"
#include "OperatorSDK/IOperatorRegistry.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>

#if defined(_WIN32) || defined(__WIN32__) || defined(WIN32)
#   include <windows.h>
#else
#   include <dlfcn.h>
#endif

namespace QDV {

// 动态库导出 C API 的函数指针类型
typedef const char* (*OperatorTypeFn)();
typedef const char* (*OperatorVersionFn)();
typedef IOperator*  (*CreateOperatorFn)();

/// 加载 .dll 并读取同目录 manifest.json（RT-002）
/// libraryPath 同目录下与 .dll 同名 .json 或 manifest.json
bool loadPlugin(const QString& libraryPath, OperatorManifest& outManifest, QString* outError);

/// loadPlugin + registerOperator；接口版本不匹配返回 false（RT-003）
bool loadAndRegister(const QString& libraryPath, QString* outError);

// ------------------------------------------------------------
// 平台相关：动态库句柄抽象
// ------------------------------------------------------------
namespace {

class NativeLib {
public:
#if defined(_WIN32) || defined(__WIN32__) || defined(WIN32)
    using Handle = HMODULE;
    static Handle load(const char* path) { return LoadLibraryA(path); }
    static void*  sym(Handle h, const char* name) { return (void*)GetProcAddress(h, name); }
    static void   unload(Handle h) { if (h) FreeLibrary(h); }
    static QString lastError() {
        DWORD e = GetLastError();
        return QStringLiteral("LoadLibrary failed (GetLastError=%1)").arg(e);
    }
#else
    using Handle = void*;
    static Handle load(const char* path) { return dlopen(path, RTLD_LAZY); }
    static void*  sym(Handle h, const char* name) { return dlsym(h, name); }
    static void   unload(Handle h) { if (h) dlclose(h); }
    static QString lastError() { return QStringLiteral("dlopen failed: %1").arg(dlerror()); }
#endif
};

/// 把 QString 转为本地 8 位编码（Windows 下 LoadLibraryA 用本地编码）
QByteArray toLocalBytes(const QString& s) {
    return s.toLocal8Bit();
}

/// 查找与 .dll 同目录的 manifest 文件
/// 优先：与 .dll 同名 .json（Histogram.dll → Histogram.json）
/// 其次：同目录 manifest.json
QString findManifestPath(const QString& libraryPath) {
    QFileInfo fi(libraryPath);
    QString sameName = fi.dir().filePath(fi.completeBaseName() + ".json");
    if (QFile::exists(sameName)) {
        return sameName;
    }
    QString manifestPath = fi.dir().filePath("manifest.json");
    if (QFile::exists(manifestPath)) {
        return manifestPath;
    }
    return QString();
}

} // namespace

// ------------------------------------------------------------
// loadPlugin 实现
// ------------------------------------------------------------
bool loadPlugin(const QString& libraryPath, OperatorManifest& outManifest, QString* outError) {
    // RT-002：.dll 缺失返回 false 不崩溃
    if (!QFile::exists(libraryPath)) {
        if (outError) *outError = QStringLiteral("library not found: %1").arg(libraryPath);
        return false;
    }

    // 1) 加载动态库
    NativeLib::Handle h = NativeLib::load(toLocalBytes(libraryPath).constData());
    if (!h) {
        if (outError) *outError = NativeLib::lastError();
        return false;
    }

    // 2) 解析导出符号
    auto typeFn    = reinterpret_cast<OperatorTypeFn>(NativeLib::sym(h, "operator_type"));
    auto versionFn = reinterpret_cast<OperatorVersionFn>(NativeLib::sym(h, "operator_version"));
    auto createFn  = reinterpret_cast<CreateOperatorFn>(NativeLib::sym(h, "create_operator"));
    if (!typeFn || !versionFn || !createFn) {
        if (outError) *outError = QStringLiteral("missing exported C API (operator_type/operator_version/create_operator)");
        NativeLib::unload(h);
        return false;
    }

    // 3) 读取 manifest.json（用于版本比对 + 注册元数据）
    QString manifestPath = findManifestPath(libraryPath);
    if (manifestPath.isEmpty()) {
        if (outError) *outError = QStringLiteral("manifest json not found alongside library: %1").arg(libraryPath);
        NativeLib::unload(h);
        return false;
    }

    OperatorManifest m;
    QString manifestErr;
    if (!loadManifest(manifestPath, m, &manifestErr)) {
        if (outError) *outError = QStringLiteral("manifest load failed: %1").arg(manifestErr);
        NativeLib::unload(h);
        return false;
    }

    // 4) RT-003：接口版本比对（dll 导出的 version() 与 manifest.version 必须一致）
    QString dllVersion = QString::fromUtf8(versionFn());
    if (dllVersion != m.version) {
        if (outError) {
            *outError = QStringLiteral("version mismatch: dll=%1 manifest=%2")
                            .arg(dllVersion, m.version);
        }
        NativeLib::unload(h);
        return false;
    }

    outManifest = m;

    // 注意：本函数只负责"加载并校验"，不持有句柄
    // 真正的算子创建在 loadAndRegister 中通过再次加载来注册（保持简单）
    NativeLib::unload(h);
    return true;
}

// ------------------------------------------------------------
// loadAndRegister 实现
// ------------------------------------------------------------
bool loadAndRegister(const QString& libraryPath, QString* outError) {
    // 1) 先用 loadPlugin 校验 .dll + manifest
    OperatorManifest m;
    if (!loadPlugin(libraryPath, m, outError)) {
        return false;
    }

    // 2) 重新加载 .dll 创建创建器，并注册到 IOperatorRegistry
    //    （loadPlugin 卸载了句柄，这里需要再加载一次以保留创建器）
    NativeLib::Handle h = NativeLib::load(toLocalBytes(libraryPath).constData());
    if (!h) {
        if (outError) *outError = NativeLib::lastError();
        return false;
    }

    auto createFn = reinterpret_cast<CreateOperatorFn>(NativeLib::sym(h, "create_operator"));
    if (!createFn) {
        if (outError) *outError = QStringLiteral("missing create_operator symbol");
        NativeLib::unload(h);
        return false;
    }

    // 注：此处故意不释放 h，因为创建器在后续调用 createOperator 时仍需使用 dll 中的代码
    //    （Windows 下 FreeLibrary 后再调用其函数会崩溃）
    //    实际项目中应有完整的插件生命周期管理；本 Phase 0 阶段保持最小实现
    std::function<IOperator*()> creator = [createFn]() -> IOperator* {
        return createFn();
    };

    bool ok = IOperatorRegistry::instance().registerOperator(m.type, m.version, std::move(creator));
    if (!ok) {
        // RT-001：重复注册（可能同 .dll 加载两次）
        if (outError) *outError = QStringLiteral("operator already registered: %1").arg(m.type);
        // 重复注册时释放新加载的句柄（旧的仍由首次注册保留）
        NativeLib::unload(h);
        return false;
    }

    return true;
}

} // namespace QDV
