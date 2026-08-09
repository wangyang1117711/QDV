#include "OperatorLibrary/OperatorDefinition.h"

// =====================================================================
// 自定义算子模块 —— 数据模型序列化实现
// 说明：本文件为纯数据（反）序列化层，属“管道”代码而非核心算法，
//       故完整实现；算法类（校验/导入/测试/版本/依赖图等）逻辑见各自桩文件。
// =====================================================================

namespace QDV {
namespace OperatorLibrary {

// ---------- 辅助 ----------
static QString jstr(const QJsonObject& o, const QString& k, const QString& d = QString()) {
    return o.contains(k) && o.value(k).isString() ? o.value(k).toString() : d;
}
static bool jbool(const QJsonObject& o, const QString& k, bool d) {
    return o.contains(k) && o.value(k).isBool() ? o.value(k).toBool() : d;
}
static QVariant jvar(const QJsonObject& o, const QString& k) {
    return o.contains(k) ? o.value(k).toVariant() : QVariant();
}
static QStringList jlist(const QJsonObject& o, const QString& k) {
    QStringList r;
    if (o.contains(k) && o.value(k).isArray()) {
        for (const QJsonValue& v : o.value(k).toArray()) if (v.isString()) r << v.toString();
    }
    return r;
}

// ===================== InputPort =====================
QJsonObject InputPort::toJson() const {
    QJsonObject o;
    o["name"] = name; o["cnName"] = cnName; o["type"] = type;
    o["desc"] = desc; o["required"] = required;
    return o;
}
InputPort InputPort::fromJson(const QJsonObject& o) {
    InputPort p;
    p.name = jstr(o,"name"); p.cnName = jstr(o,"cnName"); p.type = jstr(o,"type");
    p.desc = jstr(o,"desc"); p.required = jbool(o,"required",true);
    return p;
}
QVariantMap InputPort::toMap() const {
    QVariantMap m; m["name"]=name; m["cnName"]=cnName; m["type"]=type;
    m["desc"]=desc; m["required"]=required; return m;
}
InputPort InputPort::fromMap(const QVariantMap& m) {
    InputPort p;
    p.name=m.value("name").toString(); p.cnName=m.value("cnName").toString();
    p.type=m.value("type").toString(); p.desc=m.value("desc").toString();
    p.required=m.value("required",true).toBool(); return p;
}

// ===================== OutputPort =====================
QJsonObject OutputPort::toJson() const {
    QJsonObject o;
    o["name"]=name; o["cnName"]=cnName; o["type"]=type; o["desc"]=desc;
    o["color"]=color; o["defaultEnabled"]=defaultEnabled;
    o["multiTargetOnly"]=multiTargetOnly; return o;
}
OutputPort OutputPort::fromJson(const QJsonObject& o) {
    OutputPort p;
    p.name=jstr(o,"name"); p.cnName=jstr(o,"cnName"); p.type=jstr(o,"type");
    p.desc=jstr(o,"desc"); p.color=jstr(o,"color","#FFA726");
    p.defaultEnabled=jbool(o,"defaultEnabled",true);
    p.multiTargetOnly=jbool(o,"multiTargetOnly",false); return p;
}
QVariantMap OutputPort::toMap() const {
    QVariantMap m; m["name"]=name; m["cnName"]=cnName; m["type"]=type;
    m["desc"]=desc; m["color"]=color; m["defaultEnabled"]=defaultEnabled;
    m["multiTargetOnly"]=multiTargetOnly; return m;
}
OutputPort OutputPort::fromMap(const QVariantMap& m) {
    OutputPort p;
    p.name=m.value("name").toString(); p.cnName=m.value("cnName").toString();
    p.type=m.value("type").toString(); p.desc=m.value("desc").toString();
    p.color=m.value("color","#FFA726").toString();
    p.defaultEnabled=m.value("defaultEnabled",true).toBool();
    p.multiTargetOnly=m.value("multiTargetOnly",false).toBool(); return p;
}

// ===================== ParamDef =====================
QJsonObject ParamDef::toJson() const {
    QJsonObject o;
    o["name"]=name; o["cnName"]=cnName; o["type"]=typeToInt();
    if (!defaultValue.isNull()) o["defaultValue"]=QJsonValue::fromVariant(defaultValue);
    if (!minValue.isNull())     o["minValue"]=QJsonValue::fromVariant(minValue);
    if (!maxValue.isNull())     o["maxValue"]=QJsonValue::fromVariant(maxValue);
    if (!step.isNull())         o["step"]=QJsonValue::fromVariant(step);
    o["options"]=QJsonArray::fromStringList(options);
    o["optionKeys"]=QJsonArray::fromStringList(optionKeys);
    o["help"]=help; o["unit"]=unit; o["group"]=group;
    return o;
}
ParamDef ParamDef::fromJson(const QJsonObject& o) {
    ParamDef p;
    p.name=jstr(o,"name"); p.cnName=jstr(o,"cnName");
    p.type=typeFromInt(o.value("type").toInt(4));
    p.defaultValue=jvar(o,"defaultValue"); p.minValue=jvar(o,"minValue");
    p.maxValue=jvar(o,"maxValue"); p.step=jvar(o,"step");
    p.options=jlist(o,"options"); p.optionKeys=jlist(o,"optionKeys");
    p.help=jstr(o,"help"); p.unit=jstr(o,"unit"); p.group=jstr(o,"group");
    return p;
}
QVariantMap ParamDef::toMap() const {
    QVariantMap m; m["name"]=name; m["cnName"]=cnName; m["type"]=typeToInt();
    m["defaultValue"]=defaultValue; m["minValue"]=minValue; m["maxValue"]=maxValue;
    m["step"]=step; m["options"]=options; m["optionKeys"]=optionKeys;
    m["help"]=help; m["unit"]=unit; m["group"]=group; return m;
}
ParamDef ParamDef::fromMap(const QVariantMap& m) {
    ParamDef p;
    p.name=m.value("name").toString(); p.cnName=m.value("cnName").toString();
    p.type=typeFromInt(m.value("type",4).toInt());
    p.defaultValue=m.value("defaultValue"); p.minValue=m.value("minValue");
    p.maxValue=m.value("maxValue"); p.step=m.value("step");
    p.options=m.value("options").toStringList(); p.optionKeys=m.value("optionKeys").toStringList();
    p.help=m.value("help").toString(); p.unit=m.value("unit").toString();
    p.group=m.value("group").toString(); return p;
}
int ParamDef::typeToInt() const { return static_cast<int>(type); }
ParamType ParamDef::typeFromInt(int v) {
    switch (v) {
        case 0: return ParamType::Int;
        case 1: return ParamType::Float;
        case 2: return ParamType::Enum;
        case 3: return ParamType::Bool;
        case 4: return ParamType::String;
        case 5: return ParamType::ROI;
        case 6: return ParamType::Vector;
        default: return ParamType::String;
    }
}

// ===================== DependencyRef =====================
QJsonObject DependencyRef::toJson() const {
    QJsonObject o; o["type"]=type; o["version"]=version; return o;
}
DependencyRef DependencyRef::fromJson(const QJsonObject& o) {
    DependencyRef d; d.type=jstr(o,"type"); d.version=jstr(o,"version"); return d;
}

// ===================== OperatorPermission =====================
QJsonObject OperatorPermission::toJson() const {
    QJsonObject o; o["owner"]=owner; o["roles"]=QJsonObject::fromVariantMap(roles); return o;
}
OperatorPermission OperatorPermission::fromJson(const QJsonObject& o) {
    OperatorPermission p; p.owner=jstr(o,"owner");
    if (o.contains("roles") && o.value("roles").isObject())
        p.roles = o.value("roles").toObject().toVariantMap();
    return p;
}

// ===================== TestConfig =====================
QJsonObject TestConfig::toJson() const {
    QJsonObject o; o["sampleInput"]=sampleInput; o["expectedOutput"]=expectedOutput;
    o["tolerance"]=tolerance; o["metrics"]=QJsonArray::fromStringList(metrics); return o;
}
TestConfig TestConfig::fromJson(const QJsonObject& o) {
    TestConfig t; t.sampleInput=jstr(o,"sampleInput"); t.expectedOutput=jstr(o,"expectedOutput");
    t.tolerance=o.value("tolerance").toDouble(0.0); t.metrics=jlist(o,"metrics"); return t;
}

// ===================== Implementation =====================
QJsonObject Implementation::toJson() const {
    QJsonObject o; o["recipe"]=QJsonArray::fromStringList(recipe);
    o["script"]=script; o["library"]=library; o["entry"]=entry; return o;
}
Implementation Implementation::fromJson(const QJsonObject& o) {
    Implementation i; i.recipe=jlist(o,"recipe"); i.script=jstr(o,"script");
    i.library=jstr(o,"library"); i.entry=jstr(o,"entry"); return i;
}

// ===================== OperatorDef =====================
QJsonObject OperatorDef::toJson() const {
    QJsonObject o;
    o["type"]=type; o["kind"]=kindToString(kind); o["cnName"]=cnName;
    o["category"]=category; o["subGroup"]=subGroup; o["iconPath"]=iconPath;
    o["description"]=description; o["version"]=version; o["status"]=statusToString(status);
    o["author"]=author;
    o["createdAt"]=createdAt.toString(Qt::ISODate);
    o["updatedAt"]=updatedAt.toString(Qt::ISODate);
    o["tags"]=QJsonArray::fromStringList(tags);

    QJsonArray in, out, par, dep;
    for (const auto& v : inputs) in.append(v.toJson());
    for (const auto& v : outputs) out.append(v.toJson());
    for (const auto& v : params) par.append(v.toJson());
    for (const auto& v : dependencies) dep.append(v.toJson());
    o["inputs"]=in; o["outputs"]=out; o["params"]=par; o["dependencies"]=dep;
    o["implementation"]=implementation.toJson();
    o["permission"]=permission.toJson();
    o["tests"]=tests.toJson();
    o["doc"]=doc;
    return o;
}
OperatorDef OperatorDef::fromJson(const QJsonObject& o, QString* err) {
    OperatorDef d;
    d.type=jstr(o,"type");
    if (d.type.isEmpty() && err) *err = "missing required field: type";
    d.kind=kindFromString(jstr(o,"kind","builtin"));
    d.cnName=jstr(o,"cnName");
    d.category=jstr(o,"category");
    d.subGroup=jstr(o,"subGroup");
    d.iconPath=jstr(o,"iconPath");
    d.description=jstr(o,"description");
    d.version=jstr(o,"version","1.0.0");
    d.status=statusFromString(jstr(o,"status","draft"));
    d.author=jstr(o,"author");
    d.createdAt=QDateTime::fromString(jstr(o,"createdAt"),Qt::ISODate);
    d.updatedAt=QDateTime::fromString(jstr(o,"updatedAt"),Qt::ISODate);
    d.tags=jlist(o,"tags");

    auto arr = [&](const QString& k){ QList<QJsonObject> r;
        if (o.contains(k) && o.value(k).isArray())
            for (const QJsonValue& v : o.value(k).toArray())
                if (v.isObject()) r.append(v.toObject());
        return r; };
    for (const auto& v : arr("inputs"))  d.inputs.append(InputPort::fromJson(v));
    for (const auto& v : arr("outputs")) d.outputs.append(OutputPort::fromJson(v));
    for (const auto& v : arr("params"))  d.params.append(ParamDef::fromJson(v));
    for (const auto& v : arr("dependencies")) d.dependencies.append(DependencyRef::fromJson(v));

    if (o.contains("implementation") && o.value("implementation").isObject())
        d.implementation=Implementation::fromJson(o.value("implementation").toObject());
    if (o.contains("permission") && o.value("permission").isObject())
        d.permission=OperatorPermission::fromJson(o.value("permission").toObject());
    if (o.contains("tests") && o.value("tests").isObject())
        d.tests=TestConfig::fromJson(o.value("tests").toObject());
    d.doc=jstr(o,"doc");
    return d;
}
QVariantMap OperatorDef::toMap() const {
    QVariantMap m;
    m["type"]=type; m["kind"]=kindToString(kind); m["cnName"]=cnName;
    m["category"]=category; m["subGroup"]=subGroup; m["iconPath"]=iconPath;
    m["description"]=description; m["version"]=version; m["status"]=statusToString(status);
    m["author"]=author; m["createdAt"]=createdAt.toString(Qt::ISODate);
    m["updatedAt"]=updatedAt.toString(Qt::ISODate); m["tags"]=tags;

    QVariantList in,out,par,dep;
    for (const auto& v : inputs)  in.append(v.toMap());
    for (const auto& v : outputs) out.append(v.toMap());
    for (const auto& v : params)  par.append(v.toMap());
    for (const auto& v : dependencies) { QVariantMap dm; dm["type"]=v.type; dm["version"]=v.version; dep.append(dm); }
    m["inputs"]=in; m["outputs"]=out; m["params"]=par; m["dependencies"]=dep;

    QVariantMap impl; impl["recipe"]=implementation.recipe; impl["script"]=implementation.script;
    impl["library"]=implementation.library; impl["entry"]=implementation.entry; m["implementation"]=impl;
    m["permission"]=permission.roles; m["owner"]=permission.owner;
    QVariantMap tst; tst["sampleInput"]=tests.sampleInput; tst["expectedOutput"]=tests.expectedOutput;
    tst["tolerance"]=tests.tolerance; tst["metrics"]=tests.metrics; m["tests"]=tst;
    m["doc"]=doc;
    return m;
}
OperatorDef OperatorDef::fromMap(const QVariantMap& m) {
    OperatorDef d;
    d.type=m.value("type").toString();
    d.kind=kindFromString(m.value("kind","builtin").toString());
    d.cnName=m.value("cnName").toString();
    d.category=m.value("category").toString();
    d.subGroup=m.value("subGroup").toString();
    d.iconPath=m.value("iconPath").toString();
    d.description=m.value("description").toString();
    d.version=m.value("version","1.0.0").toString();
    d.status=statusFromString(m.value("status","draft").toString());
    d.author=m.value("author").toString();
    d.createdAt=QDateTime::fromString(m.value("createdAt").toString(),Qt::ISODate);
    d.updatedAt=QDateTime::fromString(m.value("updatedAt").toString(),Qt::ISODate);
    d.tags=m.value("tags").toStringList();

    auto vlist=[&](const QString& k){ QVariantList r;
        QVariant v=m.value(k); if (v.canConvert<QVariantList>()) r=v.toList(); return r; };
    for (const QVariant& v : vlist("inputs"))  d.inputs.append(InputPort::fromMap(v.toMap()));
    for (const QVariant& v : vlist("outputs")) d.outputs.append(OutputPort::fromMap(v.toMap()));
    for (const QVariant& v : vlist("params"))  d.params.append(ParamDef::fromMap(v.toMap()));
    for (const QVariant& v : vlist("dependencies")) {
        QVariantMap dm=v.toMap(); DependencyRef dr;
        dr.type=dm.value("type").toString(); dr.version=dm.value("version").toString();
        d.dependencies.append(dr);
    }
    QVariantMap impl=m.value("implementation").toMap();
    d.implementation.recipe=impl.value("recipe").toStringList();
    d.implementation.script=impl.value("script").toString();
    d.implementation.library=impl.value("library").toString();
    d.implementation.entry=impl.value("entry").toString();
    d.permission.owner=m.value("owner").toString();
    d.permission.roles=m.value("permission").toMap();
    QVariantMap tst=m.value("tests").toMap();
    d.tests.sampleInput=tst.value("sampleInput").toString();
    d.tests.expectedOutput=tst.value("expectedOutput").toString();
    d.tests.tolerance=tst.value("tolerance").toDouble();
    d.tests.metrics=tst.value("metrics").toStringList();
    d.doc=m.value("doc").toString();
    return d;
}
bool OperatorDef::isValid() const {
    // 基础合法性：type/cnName/category 非空
    return !type.isEmpty() && !cnName.isEmpty() && !category.isEmpty();
}
QString OperatorDef::kindToString(OperatorKind k) {
    switch (k) { case OperatorKind::Config: return "config";
                case OperatorKind::Plugin: return "plugin";
                default: return "builtin"; }
}
OperatorKind OperatorDef::kindFromString(const QString& s) {
    if (s=="config") return OperatorKind::Config;
    if (s=="plugin") return OperatorKind::Plugin;
    return OperatorKind::Builtin;
}
QString OperatorDef::statusToString(OperatorStatus s) {
    switch (s) { case OperatorStatus::Published: return "published";
                case OperatorStatus::Deprecated: return "deprecated";
                default: return "draft"; }
}
OperatorStatus OperatorDef::statusFromString(const QString& s) {
    if (s=="published") return OperatorStatus::Published;
    if (s=="deprecated") return OperatorStatus::Deprecated;
    return OperatorStatus::Draft;
}
QString OperatorDef::kindString() const { return kindToString(kind); }
QString OperatorDef::statusString() const { return statusToString(status); }

// ===================== ValidationIssue / Result =====================
QJsonObject ValidationIssue::toJson() const {
    QJsonObject o; o["level"]=(level==Error?"error":"warning");
    o["field"]=field; o["message"]=message; return o;
}
ValidationIssue ValidationIssue::fromJson(const QJsonObject& o) {
    ValidationIssue i;
    i.level = (jstr(o,"level")=="warning") ? Warning : Error;
    i.field=jstr(o,"field"); i.message=jstr(o,"message"); return i;
}
void ValidationResult::addError(const QString& field, const QString& msg) {
    ok=false; ValidationIssue i; i.level=ValidationIssue::Error; i.field=field; i.message=msg; issues.append(i);
}
void ValidationResult::addWarning(const QString& field, const QString& msg) {
    ValidationIssue i; i.level=ValidationIssue::Warning; i.field=field; i.message=msg; issues.append(i);
}

// ===================== ImportPreview =====================
QJsonObject ImportPreview::toJson() const {
    QJsonObject o; o["def"]=def.toJson();
    QJsonArray c,w;
    for (const auto& v : conflicts) c.append(v.toJson());
    for (const auto& v : warnings) w.append(v.toJson());
    o["conflicts"]=c; o["warnings"]=w; return o;
}

// ===================== TestResult =====================
QJsonObject TestResult::toJson() const {
    QJsonObject o; o["ok"]=ok; o["outputPath"]=outputPath;
    o["elapsedMs"]=elapsedMs; o["metrics"]=QJsonObject::fromVariantMap(metrics);
    o["error"]=error; return o;
}

// ===================== VersionRecord =====================
QJsonObject VersionRecord::toJson() const {
    QJsonObject o; o["type"]=type; o["version"]=version; o["author"]=author;
    o["date"]=date.toString(Qt::ISODate); o["comment"]=comment;
    o["snapshot"]=snapshot.toJson(); return o;
}
VersionRecord VersionRecord::fromJson(const QJsonObject& o) {
    VersionRecord r; r.type=jstr(o,"type"); r.version=jstr(o,"version");
    r.author=jstr(o,"author"); r.date=QDateTime::fromString(jstr(o,"date"),Qt::ISODate);
    r.comment=jstr(o,"comment");
    if (o.contains("snapshot") && o.value("snapshot").isObject())
        r.snapshot=OperatorDef::fromJson(o.value("snapshot").toObject());
    return r;
}

} // namespace OperatorLibrary
} // namespace QDV
