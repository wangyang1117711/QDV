#include "OperatorLibrary/OperatorLibraryController.h"

// =====================================================================
// OperatorLibraryController 实现（spec §1.1 + §2.1/§2.2/§2.3 + tasks.md Task 4.1）
//
// 门面层：聚合 RegistryStore / Validator / Importer，桥接 OperatorDescriptors
//
// 循环依赖处理（方案 D）：
//   - Controller.cpp 直接 include "UI/OperatorDescriptors.h"
//   - src/OperatorLibrary/CMakeLists.txt 不链接 UI 模块
//   - 静态库允许未解析符号，由最终可执行文件（QDV_tests / SmartVision）解析
//   - 避免 OperatorLibrary ↔ UI 循环依赖（阶段 6 UI 将依赖 OperatorLibrary）
//
// toOperatorMeta 适配器（spec §1.4：仅在 Controller 边界）：
//   - 在匿名命名空间实现，不暴露到头文件
//   - OperatorLibrary 模块内部其他文件不感知 OperatorMeta
// =====================================================================

#include "OperatorLibrary/OperatorRegistryStore.h"
#include "OperatorLibrary/OperatorValidator.h"
#include "OperatorLibrary/OperatorImporter.h"
#include "UI/OperatorDescriptors.h"  // 方案 D：直接 include，CMake 不链接

#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QDateTime>
#include <QRegularExpression>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonParseError>

// ---------------------------------------------------------------------
// 匿名命名空间：私有适配器（spec §1.4：仅在 Controller 边界）
// OperatorDef (OperatorLibrary) → OperatorMeta (UI)
// ---------------------------------------------------------------------
namespace {

/// 读取训练产物 labels 文件（JSON 格式 {"labels": [...]}），返回标签列表
/// 解析失败或文件不存在时返回空列表（运行时回退到模型内置 labels）
QStringList loadLabelsFile(const QString& labelsPath) {
    if (labelsPath.isEmpty() || !QFile::exists(labelsPath)) {
        return QStringList();
    }
    QFile f(labelsPath);
    if (!f.open(QIODevice::ReadOnly)) {
        return QStringList();
    }
    QJsonParseError parseErr;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &parseErr);
    f.close();
    if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
        return QStringList();
    }
    const QJsonArray arr = doc.object().value(QStringLiteral("labels")).toArray();
    QStringList labels;
    labels.reserve(arr.size());
    for (const QJsonValue& v : arr) {
        if (v.isString()) labels.append(v.toString());
    }
    return labels;
}

QDV::UI::OperatorMeta toOperatorMeta(const QDV::OperatorLibrary::OperatorDef& def) {
    QDV::UI::OperatorMeta meta;
    meta.type        = def.type;
    meta.cnName      = def.cnName;
    meta.category    = def.category;
    meta.subGroup    = def.subGroup;
    meta.iconPath    = def.iconPath;
    meta.description = def.description;

    // ParamDef → ParamSpec（整数映射对齐：Int=0/Float=1/Enum=2/Bool=3/String=4/ROI=5/Vector=6）
    for (const auto& p : def.params) {
        QDV::UI::ParamSpec ps;
        ps.name         = p.name;
        ps.cnName       = p.cnName;
        ps.type         = static_cast<QDV::UI::ParamType>(static_cast<int>(p.type));
        ps.defaultValue = p.defaultValue;
        ps.minValue     = p.minValue;
        ps.maxValue     = p.maxValue;
        ps.step         = p.step;
        ps.options      = p.options;
        ps.optionKeys   = p.optionKeys;
        ps.help         = p.help;
        ps.unit         = p.unit;
        meta.params.append(ps);
    }

    // OutputPort → QVariantMap（对齐 OperatorMeta::outputs 注释：{name, cnName, typeName, desc, color, ...}）
    for (const auto& o : def.outputs) {
        QVariantMap om;
        om["name"]            = o.name;
        om["cnName"]          = o.cnName;
        om["typeName"]        = o.type;       // 注意 key 名：OperatorDef 用 "type"，OperatorMeta 用 "typeName"
        om["desc"]            = o.desc;
        om["color"]           = o.color;
        om["defaultEnabled"]  = o.defaultEnabled;
        om["multiTargetOnly"] = o.multiTargetOnly;
        meta.outputs.append(om);
    }

    return meta;
}

} // anonymous namespace

namespace QDV {
namespace OperatorLibrary {

// ---------------------------------------------------------------------
// 单例
// ---------------------------------------------------------------------
OperatorLibraryController* OperatorLibraryController::s_instance = nullptr;

OperatorLibraryController* OperatorLibraryController::instance() {
    if (!s_instance) {
        s_instance = new OperatorLibraryController();
    }
    return s_instance;
}

OperatorLibraryController::OperatorLibraryController(QObject* parent)
    : QObject(parent) {}

// ---------------------------------------------------------------------
// init：启动加载流程（spec §2.1）
//   1. RegistryStore::init + scanAll
//   2. 逐个 def → toOperatorMeta → registerExternalOperator
// ---------------------------------------------------------------------
void OperatorLibraryController::init(const QString& operatorsPath, const QString& versionsPath) {
    Q_UNUSED(versionsPath);  // O1e 版本管理切片才使用

    OperatorRegistryStore* store = OperatorRegistryStore::instance();
    QString err;
    if (!store->init(operatorsPath, &err)) {
        qWarning().noquote() << "[OperatorLibraryController] init failed:" << err;
        return;  // 启动失败不阻断应用（spec §2.1：算子库是增量功能）
    }

    // 逐个注册到 OperatorDescriptors（spec §2.1）
    const QList<OperatorDef> defs = store->all();
    int registered = 0;
    for (const OperatorDef& def : defs) {
        UI::OperatorMeta meta = toOperatorMeta(def);
        if (UI::OperatorDescriptors::registerExternalOperator(meta)) {
            ++registered;
        } else {
            // registerExternalOperator 返回 false 表示已存在（去重），不致命
            qWarning().noquote() << "[OperatorLibraryController] 注册失败（可能已存在）:" << def.type;
        }
    }

    qDebug().noquote() << "[OperatorLibraryController] init completed, loaded" << defs.size()
                       << "operator(s), registered" << registered;
    emit storeChanged();
}

// ---------------------------------------------------------------------
// importFile：委托 OperatorImporter + 补充冲突检测（spec §2.2）
// ---------------------------------------------------------------------
ImportPreview OperatorLibraryController::importFile(const QString& path, QString* err) {
    // 1. 委托 Importer 解析 + 校验
    ImportPreview preview = OperatorImporter::importFile(path, err);

    // 2. 补充：检测与已导入算子的冲突（spec §2.2 ④）
    QList<ValidationIssue> storeConflicts =
        OperatorImporter::detectConflicts(preview.def, OperatorRegistryStore::instance());
    preview.conflicts.append(storeConflicts);

    // 3. 补充：检测与内置算子的冲突（spec §2.2 ⑤）
    if (!preview.def.type.isEmpty() && UI::OperatorDescriptors::has(preview.def.type)) {
        ValidationIssue i;
        i.level   = ValidationIssue::Error;
        i.field   = QStringLiteral("type");
        i.message = QStringLiteral("type '%1' 已存在（内置算子）").arg(preview.def.type);
        preview.conflicts.append(i);
    }

    return preview;
}

// ---------------------------------------------------------------------
// commitImport：事务性提交（spec §2.2 + §5.2 不变量 4）
//   1. 二次校验 conflicts 为空（防 TOCTOU）
//   2. 二次校验 type 非空
//   3. 二次校验文件系统（防 TOCTOU）
//   4. RegistryStore::save（原子写入）
//   5. registerExternalOperator（失败回滚）
//   6. 发信号 importedChanged(type, "added")
// ---------------------------------------------------------------------
bool OperatorLibraryController::commitImport(const ImportPreview& preview, QString* err) {
    // 1. 二次校验：conflicts 必须为空
    if (!preview.conflicts.isEmpty()) {
        if (err) *err = QStringLiteral("存在 %1 个冲突，无法导入").arg(preview.conflicts.size());
        return false;
    }

    const OperatorDef& def = preview.def;

    // 2. 二次校验：type 不能为空
    if (def.type.isEmpty()) {
        if (err) *err = QStringLiteral("算子 type 为空");
        return false;
    }

    // 3. 二次校验：再次检查文件系统（防 TOCTOU，spec §3.4）
    OperatorRegistryStore* store = OperatorRegistryStore::instance();
    if (store->contains(def.type)) {
        if (err) *err = QStringLiteral("type '%1' 已存在（TOCTOU 检测：已导入算子）").arg(def.type);
        return false;
    }
    if (UI::OperatorDescriptors::has(def.type)) {
        if (err) *err = QStringLiteral("type '%1' 已存在（TOCTOU 检测：内置算子）").arg(def.type);
        return false;
    }

    // 4. RegistryStore::save（含原子写入 + 备份）
    if (!store->save(def, err)) {
        return false;  // 保存失败，不更新内存，不注册
    }

    // 5. registerExternalOperator（spec §2.2：失败回滚删除已写文件）
    UI::OperatorMeta meta = toOperatorMeta(def);
    if (!UI::OperatorDescriptors::registerExternalOperator(meta)) {
        // 注册失败，回滚：删除刚写入的文件
        QString rollbackErr;
        store->remove(def.type, &rollbackErr);
        if (err) *err = QStringLiteral("注册到算子库失败，已回滚: %1").arg(def.type);
        qWarning().noquote() << "[OperatorLibraryController] commitImport rollback:" << def.type
                             << "rollbackErr:" << rollbackErr;
        return false;
    }

    // 6. 成功：发信号
    qDebug().noquote() << "[OperatorLibraryController] commitImport success:" << def.type;
    emit importedChanged(def.type, QStringLiteral("added"));
    emit storeChanged();
    return true;
}

// ---------------------------------------------------------------------
// removeImported：删除已导入算子（spec §2.3）
//   1. 查 m_loaded 是否存在
//   2. RegistryStore::remove（含文件删除 + 内存清理）
//   3. 发信号 importedChanged(type, "removed")
//
//   注意：画布预检在 Bridge 层完成，Controller 不感知画布
//   注意：OperatorDescriptors 无反注册 API（spec §2.3 已知技术债），下次启动彻底生效
// ---------------------------------------------------------------------
QVariantMap OperatorLibraryController::removeImported(const QString& type) {
    QVariantMap result;
    OperatorRegistryStore* store = OperatorRegistryStore::instance();

    // 1. 查 m_loaded 是否存在
    if (!store->contains(type)) {
        result["ok"]    = false;
        result["error"] = QStringLiteral("未找到算子 '%1'").arg(type);
        return result;
    }

    // 2. RegistryStore::remove
    QString err;
    if (!store->remove(type, &err)) {
        result["ok"]    = false;
        result["error"] = err;
        return result;
    }

    // 3. 内存清理已在 store->remove 中完成
    // ⚠️ OperatorDescriptors::s_externalRegistry 无反注册 API（spec §2.3 已知技术债）
    //    MVP 策略：UI 上对"已删除但本次仍可见"的算子添加置灰标识，下次启动彻底生效

    // 4. 发信号
    qDebug().noquote() << "[OperatorLibraryController] removeImported:" << type;
    emit importedChanged(type, QStringLiteral("removed"));
    emit storeChanged();

    result["ok"] = true;
    return result;
}

// ---------------------------------------------------------------------
// registerTrainedModelAsOperator：训练产物自动注册为算子（spec D）
//   1. sanitize(modelName)：移除空格/特殊字符，保留中英文数字下划线
//   2. 构造 OperatorDef（type=YoloTrained_<sanitized>，recipe=["YoloDetect"]，
//      modelPath 锁定训练产物路径）
//   3. 写入 config/operators_imported/<type>.qdvop（通过 RegistryStore::save 原子写入）
//   4. 调用 registerExternalOperator 即时生效（让算子面板立即出现）
//   5. 启动时 OperatorLibraryController::init 已通过 store->scanAll 自动加载
// ---------------------------------------------------------------------
bool OperatorLibraryController::registerTrainedModelAsOperator(
    const QString& modelId,
    const QString& onnxPath,
    const QString& labelsPath,
    const QString& modelName)
{
    Q_UNUSED(modelId);  // 当前实现以 modelName 作为算子 type 后缀，modelId 暂预留

    // --- 输入校验 ---
    if (modelName.isEmpty()) {
        qWarning().noquote() << "[OperatorLibraryController] registerTrainedModelAsOperator: modelName 为空";
        return false;
    }
    if (onnxPath.isEmpty() || !QFile::exists(onnxPath)) {
        qWarning().noquote() << "[OperatorLibraryController] registerTrainedModelAsOperator: ONNX 路径无效或不存在:" << onnxPath;
        return false;
    }

    // --- sanitize：移除空格/特殊字符，保留中英文数字下划线 ---
    // Unicode 范围 \u4e00-\u9fa5 匹配基本汉字；a-zA-Z0-9_ 匹配常规标识符字符
    static const QRegularExpression sanitizeRe(
        QStringLiteral("[^\\u4e00-\\u9fa5a-zA-Z0-9_]"));
    QString sanitized = modelName;
    sanitized.remove(sanitizeRe);
    if (sanitized.isEmpty()) {
        // 兜底：sanitize 后为空时用时间戳，避免 type 出现 "YoloTrained_" 这样的空后缀
        sanitized = QStringLiteral("model_%1")
                        .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMddhhmmss")));
    }

    const QString opType = QStringLiteral("YoloTrained_") + sanitized;

    // --- 检查是否已存在（用户重复训练同名模型时覆盖更新，不视为致命） ---
    OperatorRegistryStore* store = OperatorRegistryStore::instance();
    if (store->contains(opType)) {
        qDebug().noquote() << "[OperatorLibraryController] 训练产物算子已存在，覆盖更新:" << opType;
    }

    // --- 构造 OperatorDef ---
    // 与 YoloDetect 算子保持端口/参数一致，但 modelPath 默认值锁定为训练产物路径
    OperatorDef def;
    def.type        = opType;
    def.kind        = OperatorKind::Config;        // 组合算子语义（recipe 指向 YoloDetect）
    def.cnName      = QStringLiteral("YOLO: ") + modelName;
    def.category    = QStringLiteral("深度学习");
    def.subGroup    = QStringLiteral("训练产物");
    def.iconPath    = QStringLiteral("qrc:/icons/ai.svg");
    def.description = QStringLiteral("训练产物自动注册的 YOLO 检测算子（模型：%1）").arg(modelName);
    def.version     = QStringLiteral("1.0.0");
    def.status      = OperatorStatus::Published;
    def.author      = QStringLiteral("TrainingBridge");
    def.createdAt   = QDateTime::currentDateTime();
    def.updatedAt   = def.createdAt;
    def.tags        = { QStringLiteral("YOLO"), QStringLiteral("训练产物"), modelName };

    // 输入端口（与 YoloDetect 一致）
    InputPort imgIn;
    imgIn.name     = QStringLiteral("image");
    imgIn.cnName   = QStringLiteral("输入图像");
    imgIn.type     = QStringLiteral("Image");
    imgIn.desc     = QStringLiteral("待检测的输入图像");
    imgIn.required = true;
    def.inputs.append(imgIn);

    // 输出端口（与 YoloDetect 一致）
    OutputPort detOut;
    detOut.name           = QStringLiteral("detections");
    detOut.cnName         = QStringLiteral("检测结果");
    detOut.type           = QStringLiteral("Detection[]");
    detOut.desc           = QStringLiteral("检测框数组（含 class_id/confidence/bbox）");
    detOut.color          = QStringLiteral("#81C784");
    detOut.defaultEnabled = true;
    def.outputs.append(detOut);

    OutputPort numOut;
    numOut.name           = QStringLiteral("numDetections");
    numOut.cnName         = QStringLiteral("检测数量");
    numOut.type           = QStringLiteral("int");
    numOut.desc           = QStringLiteral("检测到的目标数量");
    numOut.color          = QStringLiteral("#81C784");
    numOut.defaultEnabled = true;
    def.outputs.append(numOut);

    // 参数：与 YoloDetect 一致，modelPath 默认值锁定为训练产物路径
    ParamDef modelPathParam;
    modelPathParam.name         = QStringLiteral("modelPath");
    modelPathParam.cnName       = QStringLiteral("模型路径");
    modelPathParam.type         = ParamType::String;
    modelPathParam.defaultValue = onnxPath;  // 锁定训练产物路径
    modelPathParam.help         = QStringLiteral("训练产物 ONNX 模型路径（已锁定）");
    def.params.append(modelPathParam);

    ParamDef confParam;
    confParam.name         = QStringLiteral("confidenceThreshold");
    confParam.cnName       = QStringLiteral("置信度阈值");
    confParam.type         = ParamType::Float;
    confParam.defaultValue = 0.25;
    confParam.minValue     = 0.0;
    confParam.maxValue     = 1.0;
    confParam.step         = 0.05;
    confParam.help         = QStringLiteral("分数 ≥ 阈值 视为有效检测");
    def.params.append(confParam);

    ParamDef iouParam;
    iouParam.name         = QStringLiteral("iouThreshold");
    iouParam.cnName       = QStringLiteral("NMS IoU阈值");
    iouParam.type         = ParamType::Float;
    iouParam.defaultValue = 0.45;
    iouParam.minValue     = 0.0;
    iouParam.maxValue     = 1.0;
    iouParam.step         = 0.05;
    iouParam.help         = QStringLiteral("非极大值抑制 IoU 阈值");
    def.params.append(iouParam);

    ParamDef iwParam;
    iwParam.name         = QStringLiteral("inputWidth");
    iwParam.cnName       = QStringLiteral("输入宽");
    iwParam.type         = ParamType::Int;
    iwParam.defaultValue = 640;
    iwParam.minValue     = 32;
    iwParam.maxValue     = 4096;
    iwParam.step         = 32;
    iwParam.unit         = QStringLiteral("px");
    iwParam.help         = QStringLiteral("YOLO 模型输入尺寸");
    def.params.append(iwParam);

    ParamDef ihParam;
    ihParam.name         = QStringLiteral("inputHeight");
    ihParam.cnName       = QStringLiteral("输入高");
    ihParam.type         = ParamType::Int;
    ihParam.defaultValue = 640;
    ihParam.minValue     = 32;
    ihParam.maxValue     = 4096;
    ihParam.step         = 32;
    ihParam.unit         = QStringLiteral("px");
    ihParam.help         = QStringLiteral("YOLO 模型输入尺寸");
    def.params.append(ihParam);

    // 类别标签参数：若有 labelsPath，读取并作为默认值（Vector 类型，对应 ParamType=6）
    // labels 文件格式：{"labels": ["cls0", "cls1", ...]}；解析失败时默认值留空，运行时回退到模型内置 labels
    ParamDef labelsParam;
    labelsParam.name         = QStringLiteral("categoryLabels");
    labelsParam.cnName       = QStringLiteral("类别标签");
    labelsParam.type         = ParamType::Vector;
    const QStringList labels = loadLabelsFile(labelsPath);
    if (!labels.isEmpty()) {
        labelsParam.defaultValue = QVariant(labels);
    } else {
        labelsParam.defaultValue = QStringLiteral("");  // 与 YoloDetect 默认值一致
    }
    labelsParam.help = QStringLiteral("训练时定义的类别标签（来自训练产物）");
    def.params.append(labelsParam);

    // 实现方式：组合算子，复用 YoloDetect（执行链中仅一项）
    def.implementation.recipe = QStringList{ QStringLiteral("YoloDetect") };

    // --- 写入 config/operators_imported/<type>.qdvop（RegistryStore::save 含原子写入 + 备份）---
    QString saveErr;
    if (!store->save(def, &saveErr)) {
        qWarning().noquote() << "[OperatorLibraryController] registerTrainedModelAsOperator: 保存失败:"
                             << opType << "err:" << saveErr;
        return false;
    }

    // --- 即时注册到 OperatorDescriptors（让算子面板立即出现）---
    // 注意：registerExternalOperator 对已存在的 type 返回 false（去重），不视为致命
    UI::OperatorMeta meta = toOperatorMeta(def);
    if (!UI::OperatorDescriptors::registerExternalOperator(meta)) {
        qDebug().noquote() << "[OperatorLibraryController] registerTrainedModelAsOperator: "
                              "算子可能已注册（覆盖更新）:" << opType;
    }

    qDebug().noquote() << "[OperatorLibraryController] registerTrainedModelAsOperator: 成功:"
                       << opType << "modelPath=" << onnxPath;
    emit importedChanged(opType, QStringLiteral("added"));
    emit storeChanged();
    return true;
}

// ---------------------------------------------------------------------
// query：遍历 m_loaded 过滤（spec §1.1）
// ---------------------------------------------------------------------
QVariantList OperatorLibraryController::query(const QString& category, const QString& keyword) const {
    QVariantList result;
    const QList<OperatorDef> defs = OperatorRegistryStore::instance()->query(category, keyword);
    for (const OperatorDef& def : defs) {
        result.append(def.toMap());
    }
    return result;
}

QVariantMap OperatorLibraryController::getOperator(const QString& type) const {
    bool ok = false;
    OperatorDef def = OperatorRegistryStore::instance()->get(type, &ok);
    if (!ok) return QVariantMap();
    return def.toMap();
}

QStringList OperatorLibraryController::categories() const {
    return OperatorRegistryStore::instance()->categories();
}

ValidationResult OperatorLibraryController::validate(const OperatorDef& def) const {
    return OperatorValidator::validate(def);
}

// =====================================================================
// 以下方法不在 O1a 切片，提供空实现避免链接错误
// 后续切片（O1b/O1c/O1d/O1e/O2）逐步实现
// =====================================================================

bool OperatorLibraryController::createOperator(const OperatorDef& def, QString* err) {
    Q_UNUSED(def); Q_UNUSED(err);
    qWarning() << "[OperatorLibraryController] createOperator not implemented in O1a";
    return false;
}

bool OperatorLibraryController::updateOperator(const OperatorDef& def, QString* err) {
    Q_UNUSED(def); Q_UNUSED(err);
    qWarning() << "[OperatorLibraryController] updateOperator not implemented in O1a";
    return false;
}

QVariantMap OperatorLibraryController::deleteOperator(const QString& type) {
    // O1a 切片：委托给 removeImported（spec §2.3 的接口）
    qWarning() << "[OperatorLibraryController] deleteOperator delegates to removeImported in O1a";
    return removeImported(type);
}

bool OperatorLibraryController::publishOperator(const QString& type, bool enabled, QString* err) {
    Q_UNUSED(type); Q_UNUSED(enabled); Q_UNUSED(err);
    qWarning() << "[OperatorLibraryController] publishOperator not implemented in O1a";
    return false;
}

QVariantList OperatorLibraryController::history(const QString& type) const {
    Q_UNUSED(type);
    qWarning() << "[OperatorLibraryController] history not implemented in O1a";
    return QVariantList();
}

bool OperatorLibraryController::rollback(const QString& type, const QString& version, QString* err) {
    Q_UNUSED(type); Q_UNUSED(version); Q_UNUSED(err);
    qWarning() << "[OperatorLibraryController] rollback not implemented in O1a";
    return false;
}

TestResult OperatorLibraryController::testOperator(const QString& type, const QString& sampleInput, QString* err) {
    Q_UNUSED(type); Q_UNUSED(sampleInput); Q_UNUSED(err);
    qWarning() << "[OperatorLibraryController] testOperator not implemented in O1a";
    return TestResult();
}

QString OperatorLibraryController::generateDoc(const QString& type) const {
    Q_UNUSED(type);
    qWarning() << "[OperatorLibraryController] generateDoc not implemented in O1a";
    return QString();
}

QVariantMap OperatorLibraryController::permissions(const QString& type) const {
    Q_UNUSED(type);
    qWarning() << "[OperatorLibraryController] permissions not implemented in O1a";
    return QVariantMap();
}

bool OperatorLibraryController::setPermissions(const QString& type, const QVariantMap& roles, QString* err) {
    Q_UNUSED(type); Q_UNUSED(roles); Q_UNUSED(err);
    qWarning() << "[OperatorLibraryController] setPermissions not implemented in O1a";
    return false;
}

bool OperatorLibraryController::canCurrentUser(const QString& action, const QString& type) const {
    Q_UNUSED(action); Q_UNUSED(type);
    // O1a 切片：RBAC 未实现，所有用户都有权限
    return true;
}

} // namespace OperatorLibrary
} // namespace QDV
