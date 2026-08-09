#ifndef QDV_OPERATORLIBRARY_OPERATORDEFINITION_H
#define QDV_OPERATORLIBRARY_OPERATORDEFINITION_H

// =====================================================================
// 自定义算子模块 —— 核心数据模型（OperatorLibrary）
//
// 设计要点：
//  - 自包含：不依赖 UI 命名空间的 OperatorMeta / ParamSpec，便于模块解耦。
//  - 扩展 schema：在现有 OperatorMeta(type/cnName/category/subGroup/iconPath/
//    description/params/outputs) 之上，新增 kind/version/status/author/tags/
//    inputs/dependencies/implementation/permission/tests/doc 等字段。
//  - toJson/fromJson：与 config/operators.json 扩展格式互操作（publish 时
//    与 OperatorMeta::fromMap 共用的键名保持一致，保证 EditView 可读取）。
//  - toMap/fromMap：QML 端 JS 友好（桥接层使用）。
//  - ParamType 整数映射对齐 UI::ParamType（Int0/Float1/Enum2/Bool3/String4/
//    ROI5/Vector6），便于复用 EditView 的 ParamForm 动态表单。
// =====================================================================

#include <QString>
#include <QStringList>
#include <QList>
#include <QVariant>
#include <QVariantMap>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>

namespace QDV {
namespace OperatorLibrary {

enum class OperatorKind   { Builtin, Config, Plugin };
enum class OperatorStatus { Draft, Published, Deprecated };

/// 超参/参数类型（整数映射对齐 UI::ParamType）
enum class ParamType { Int = 0, Float = 1, Enum = 2, Bool = 3, String = 4, ROI = 5, Vector = 6 };

// ---------------------------------------------------------------------
// 输入端口（输入参数 / 数据端口）
// ---------------------------------------------------------------------
struct InputPort {
    QString name;
    QString cnName;
    QString type;     ///< 类型词表：Image / Mat / Number / Region / Contour / Any ...
    QString desc;
    bool    required = true;

    QJsonObject toJson() const;
    static InputPort fromJson(const QJsonObject& o);
    QVariantMap toMap() const;
    static InputPort fromMap(const QVariantMap& m);
};

// ---------------------------------------------------------------------
// 输出端口（输出参数）
// ---------------------------------------------------------------------
struct OutputPort {
    QString name;
    QString cnName;
    QString type;
    QString desc;
    QString color = "#FFA726";
    bool    defaultEnabled = true;
    bool    multiTargetOnly = false;

    QJsonObject toJson() const;
    static OutputPort fromJson(const QJsonObject& o);
    QVariantMap toMap() const;
    static OutputPort fromMap(const QVariantMap& m);
};

// ---------------------------------------------------------------------
// 超参数（对应 ParamSpec；复用于动态表单）
// ---------------------------------------------------------------------
struct ParamDef {
    QString     name;
    QString     cnName;
    ParamType   type = ParamType::String;
    QVariant    defaultValue;
    QVariant    minValue;
    QVariant    maxValue;
    QVariant    step;
    QStringList options;       ///< Enum 可选项显示名
    QStringList optionKeys;    ///< Enum 可选项内部 key（与 options 一一对应）
    QString     help;
    QString     unit;
    QString     group;         ///< 参数分组

    QJsonObject toJson() const;
    static ParamDef fromJson(const QJsonObject& o);
    QVariantMap toMap() const;
    static ParamDef fromMap(const QVariantMap& m);
    int typeToInt() const;
    static ParamType typeFromInt(int v);
};

// ---------------------------------------------------------------------
// 依赖引用（算子依赖图边）
// ---------------------------------------------------------------------
struct DependencyRef {
    QString type;       ///< 被依赖算子 type
    QString version;    ///< 可选：锁定的版本（空=任意已发布版本）

    QJsonObject toJson() const;
    static DependencyRef fromJson(const QJsonObject& o);
};

// ---------------------------------------------------------------------
// 权限（RBAC：角色 -> 动作列表）
// ---------------------------------------------------------------------
struct OperatorPermission {
    QString     owner;
    QVariantMap roles;     ///< role(QString) -> actions(QStringList)

    QJsonObject toJson() const;
    static OperatorPermission fromJson(const QJsonObject& o);
};

// ---------------------------------------------------------------------
// 测试配置
// ---------------------------------------------------------------------
struct TestConfig {
    QString      sampleInput;
    QString      expectedOutput;
    double       tolerance = 0.0;
    QStringList  metrics;

    QJsonObject toJson() const;
    static TestConfig fromJson(const QJsonObject& o);
};

// ---------------------------------------------------------------------
// 实现方式（config=组合/脚本；plugin=动态库）
// ---------------------------------------------------------------------
struct Implementation {
    QStringList recipe;   ///< config：组合的算子 type 列表（执行链）
    QString     script;   ///< config：可选 qdv-script
    QString     library;  ///< plugin：动态库文件名（如 "MyOp.dll"）
    QString     entry;    ///< plugin：工厂入口符号（如 "createMyOp"）

    QJsonObject toJson() const;
    static Implementation fromJson(const QJsonObject& o);
};

// ---------------------------------------------------------------------
// 算子完整定义（模块工作单元）
// ---------------------------------------------------------------------
struct OperatorDef {
    QString           type;
    OperatorKind      kind = OperatorKind::Builtin;
    QString           cnName;
    QString           category;
    QString           subGroup;
    QString           iconPath;
    QString           description;
    QString           version = "1.0.0";
    OperatorStatus    status = OperatorStatus::Draft;
    QString           author;
    QDateTime         createdAt;
    QDateTime         updatedAt;
    QStringList       tags;
    QList<InputPort>  inputs;
    QList<OutputPort> outputs;
    QList<ParamDef>   params;        ///< 超参数
    QList<DependencyRef> dependencies;
    Implementation     implementation;
    OperatorPermission permission;
    TestConfig         tests;
    QString            doc;

    QJsonObject toJson() const;
    static OperatorDef fromJson(const QJsonObject& o, QString* err = nullptr);
    QVariantMap toMap() const;
    static OperatorDef fromMap(const QVariantMap& m);
    bool isValid() const;

    QString kindString() const;
    static QString kindToString(OperatorKind k);
    static OperatorKind kindFromString(const QString& s);
    QString statusString() const;
    static QString statusToString(OperatorStatus s);
    static OperatorStatus statusFromString(const QString& s);
};

// ---------------------------------------------------------------------
// 校验/导入/测试/版本 结果类型
// ---------------------------------------------------------------------
struct ValidationIssue {
    enum Level { Error, Warning } level = Error;
    QString field;
    QString message;

    QJsonObject toJson() const;
    static ValidationIssue fromJson(const QJsonObject& o);
};

struct ValidationResult {
    bool ok = true;
    QList<ValidationIssue> issues;

    void addError(const QString& field, const QString& msg);
    void addWarning(const QString& field, const QString& msg);
};

/// 智能识别得到的元信息（OperatorMetadataInferrer 输出）
struct InferredMeta {
    QList<InputPort>  inputs;
    QList<OutputPort> outputs;
    QList<DependencyRef> dependencies;
    QStringList hints;     ///< 识别依据说明
};

/// 导入预览（OperatorImporter 输出）
struct ImportPreview {
    OperatorDef def;
    QList<ValidationIssue> conflicts;   ///< 冲突（须解决才能导入）
    QList<ValidationIssue> warnings;    ///< 警告（可不解决）

    QJsonObject toJson() const;
};

/// 测试结果（OperatorTester 输出）
struct TestResult {
    bool       ok = false;
    QString    outputPath;
    double     elapsedMs = 0.0;
    QVariantMap metrics;
    QString    error;

    QJsonObject toJson() const;
};

/// 版本记录（版本链节点，含完整快照）
struct VersionRecord {
    QString    type;
    QString    version;
    QString    author;
    QDateTime  date;
    QString    comment;
    OperatorDef snapshot;

    QJsonObject toJson() const;
    static VersionRecord fromJson(const QJsonObject& o);
};

} // namespace OperatorLibrary
} // namespace QDV

#endif // QDV_OPERATORLIBRARY_OPERATORDEFINITION_H
