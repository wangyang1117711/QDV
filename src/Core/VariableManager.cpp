#include "Core/VariableManager.h"
#include "Core/Logger.h"
#include <QRegularExpression>
#include <QRegularExpressionMatchIterator>
#include <QJsonArray>
#include <QJsonObject>
#include <QMutexLocker>
#include <QRectF>
#include <QPointF>

namespace QDV {

VariableManager::VariableManager(QObject* parent) : QObject(parent) {}

VariableManager::~VariableManager() = default;

// =====================================================================
// CRUD
// =====================================================================

bool VariableManager::createVariable(const QString& name, const QString& typeStr,
                                      const QVariant& value, const QString& description) {
    QMutexLocker locker(&m_mutex);
    if (!isValidName(name)) {
        Logger::warn("VariableManager: 非法变量名 '" + name + "'");
        return false;
    }
    if (m_variables.contains(name)) {
        Logger::warn("VariableManager: 变量已存在 '" + name + "'");
        return false;
    }
    bool typeOk = false;
    const Type t = stringToType(typeStr, &typeOk);
    if (!typeOk) {
        Logger::warn("VariableManager: 未知类型 '" + typeStr + "'");
        return false;
    }

    Variable v;
    v.name = name;
    v.type = t;
    v.description = description;
    // 类型匹配的默认值
    switch (t) {
    case Type::Int:    v.value = value.canConvert<int>() ? value.toInt() : 0; break;
    case Type::Double: v.value = value.canConvert<double>() ? value.toDouble() : 0.0; break;
    case Type::String: v.value = value.toString(); break;
    case Type::Bool:   v.value = value.canConvert<bool>() ? value.toBool() : false; break;
    case Type::Roi: {
        // ROI 用 QVariantMap {x,y,w,h} 存储
        QVariantMap m = value.toMap();
        if (m.isEmpty()) { m["x"]=0.0; m["y"]=0.0; m["w"]=100.0; m["h"]=100.0; }
        v.value = m;
        break;
    }
    case Type::Region:
        // Region 用 QVariantList [x1,y1,x2,y2,...] 多边形点集表示（简化存储，不用 cv::Mat）
        v.value = value.toList().isEmpty() ? QVariantList() : value.toList();
        break;
    case Type::Points:
        // Points 用 QVariantList [{x,y}, {x,y}, ...] 存储
        v.value = value.toList().isEmpty() ? QVariantList() : value.toList();
        break;
    }
    m_variables.insert(name, v);
    emit variableCreated(name);
    emit variablesChanged();
    Logger::info("VariableManager: 创建变量 " + name + " (" + typeStr + ")");
    return true;
}

bool VariableManager::removeVariable(const QString& name) {
    QMutexLocker locker(&m_mutex);
    if (!m_variables.contains(name)) return false;
    m_variables.remove(name);
    emit variableRemoved(name);
    emit variablesChanged();
    return true;
}

bool VariableManager::setValue(const QString& name, const QVariant& value) {
    QMutexLocker locker(&m_mutex);
    auto it = m_variables.find(name);
    if (it == m_variables.end()) {
        Logger::warn("VariableManager: setValue 变量不存在 '" + name + "'");
        return false;
    }
    // 类型匹配转换
    QVariant converted;
    switch (it->type) {
    case Type::Int:
        if (!value.canConvert<int>()) {
            Logger::warn("VariableManager: " + name + " 类型不匹配（期望 int）");
            return false;
        }
        converted = value.toInt();
        break;
    case Type::Double:
        if (!value.canConvert<double>()) {
            Logger::warn("VariableManager: " + name + " 类型不匹配（期望 double）");
            return false;
        }
        converted = value.toDouble();
        break;
    case Type::String:
        converted = value.toString();
        break;
    case Type::Bool:
        if (!value.canConvert<bool>()) {
            Logger::warn("VariableManager: " + name + " 类型不匹配（期望 bool）");
            return false;
        }
        converted = value.toBool();
        break;
    case Type::Roi: {
        // ROI 类型接受 QVariantMap {x,y,w,h}
        const QVariantMap roiMap = value.toMap();
        if (roiMap.isEmpty()) {
            Logger::warn("VariableManager: " + name + " 类型不匹配（期望 roi {x,y,w,h}）");
            return false;
        }
        converted = roiMap;
        break;
    }
    case Type::Region:
        // Region 用 QVariantList 多边形点集
        converted = value.toList();
        break;
    case Type::Points:
        // Points 用 QVariantList 点集
        converted = value.toList();
        break;
    }
    if (it->value == converted) return true;  // 值未变化，不发信号
    it->value = converted;
    emit valueChanged(name, converted);
    return true;
}

bool VariableManager::setDescription(const QString& name, const QString& description) {
    QMutexLocker locker(&m_mutex);
    auto it = m_variables.find(name);
    if (it == m_variables.end()) return false;
    if (it->description == description) return true;
    it->description = description;
    emit descriptionChanged(name, description);
    return true;
}

QVariant VariableManager::value(const QString& name) const {
    QMutexLocker locker(&m_mutex);
    auto it = m_variables.constFind(name);
    if (it == m_variables.constEnd()) return QVariant();
    return it->value;
}

QVariantMap VariableManager::variable(const QString& name) const {
    QMutexLocker locker(&m_mutex);
    QVariantMap result;
    auto it = m_variables.constFind(name);
    if (it == m_variables.constEnd()) return result;
    result["name"] = it->name;
    result["type"] = typeToString(it->type);
    result["value"] = it->value;
    result["description"] = it->description;
    return result;
}

QVariantList VariableManager::variables() const {
    QMutexLocker locker(&m_mutex);
    QVariantList list;
    for (auto it = m_variables.constBegin(); it != m_variables.constEnd(); ++it) {
        QVariantMap vm;
        vm["name"] = it->name;
        vm["type"] = typeToString(it->type);
        vm["value"] = it->value;
        vm["description"] = it->description;
        list.append(vm);
    }
    return list;
}

int VariableManager::count() const {
    QMutexLocker locker(&m_mutex);
    return m_variables.size();
}

bool VariableManager::exists(const QString& name) const {
    QMutexLocker locker(&m_mutex);
    return m_variables.contains(name);
}

void VariableManager::clear() {
    QMutexLocker locker(&m_mutex);
    if (m_variables.isEmpty()) return;
    m_variables.clear();
    emit variablesChanged();
}

// =====================================================================
// 变量绑定解析
// =====================================================================

QString VariableManager::resolveBinding(const QString& input, bool* ok) const {
    QMutexLocker locker(&m_mutex);
    if (ok) *ok = true;
    if (input.isEmpty()) return input;

    // v5.4：匹配 ${varName}，支持算子输出变量名 "<nodeId>.<outputName>"
    // nodeId 为 UUID（含连字符），变量名含点和连字符
    // 正则向后兼容：原有 [A-Za-z_][A-Za-z0-9_]* 仍匹配，新增对 '.' 和 '-' 的支持
    static const QRegularExpression re(QStringLiteral("\\$\\{([A-Za-z_0-9][A-Za-z0-9_.\\-]*)\\}"));
    QString result = input;
    QRegularExpressionMatchIterator it = re.globalMatch(input);
    bool allResolved = true;
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        const QString varName = m.captured(1);
        if (!m_variables.contains(varName)) {
            // v5.4：未注册变量返回明确告警（含变量名以便调试）
            Logger::warn(QString("VariableManager: variable %1 not registered").arg(varName));
            allResolved = false;
            continue;  // 未定义变量保留原样
        }
        const QVariant v = m_variables.value(varName).value;
        result.replace(m.captured(0), v.toString());
    }
    if (ok) *ok = allResolved;
    return result;
}

QVariant VariableManager::resolveVariant(const QVariant& input) const {
    QMutexLocker locker(&m_mutex);
    if (!input.isValid()) return input;
    switch (input.type()) {
    case QVariant::String: {
        bool ok = false;
        const QString resolved = resolveBinding(input.toString(), &ok);
        if (ok) return resolved;
        return input;  // 含未定义变量，返回原值
    }
    case QVariant::Map: {
        QVariantMap result;
        const QVariantMap src = input.toMap();
        for (auto it = src.constBegin(); it != src.constEnd(); ++it) {
            result[it.key()] = resolveVariant(it.value());
        }
        return result;
    }
    case QVariant::List: {
        QVariantList result;
        const QVariantList src = input.toList();
        for (const QVariant& v : src) {
            result.append(resolveVariant(v));
        }
        return result;
    }
    default:
        return input;  // 数值/布尔等非字符串类型直接返回
    }
}

QStringList VariableManager::extractReferences(const QString& input) const {
    QMutexLocker locker(&m_mutex);
    QStringList refs;
    // v5.4：与 resolveBinding 保持一致，支持含点和连字符的算子输出变量名
    static const QRegularExpression re(QStringLiteral("\\$\\{([A-Za-z_0-9][A-Za-z0-9_.\\-]*)\\}"));
    QRegularExpressionMatchIterator it = re.globalMatch(input);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        refs.append(m.captured(1));
    }
    return refs;
}

// =====================================================================
// 序列化
// =====================================================================

QJsonArray VariableManager::toJson() const {
    QMutexLocker locker(&m_mutex);
    QJsonArray arr;
    for (auto it = m_variables.constBegin(); it != m_variables.constEnd(); ++it) {
        QJsonObject obj;
        obj["name"] = it->name;
        obj["type"] = typeToString(it->type);
        obj["description"] = it->description;
        switch (it->type) {
        case Type::Int:    obj["value"] = it->value.toInt(); break;
        case Type::Double: obj["value"] = it->value.toDouble(); break;
        case Type::String: obj["value"] = it->value.toString(); break;
        case Type::Bool:   obj["value"] = it->value.toBool(); break;
        case Type::Roi:    obj["value"] = QJsonObject::fromVariantMap(it->value.toMap()); break;
        case Type::Region: obj["value"] = QJsonArray::fromVariantList(it->value.toList()); break;
        case Type::Points: obj["value"] = QJsonArray::fromVariantList(it->value.toList()); break;
        }
        arr.append(obj);
    }
    return arr;
}

bool VariableManager::fromJson(const QJsonArray& arr) {
    QMutexLocker locker(&m_mutex);
    m_variables.clear();
    for (const QJsonValue& v : arr) {
        const QJsonObject obj = v.toObject();
        const QString name = obj.value("name").toString();
        const QString typeStr = obj.value("type").toString();
        const QString desc = obj.value("description").toString();
        const QJsonValue val = obj.value("value");
        QVariant value;
        bool typeOk = false;
        const Type t = stringToType(typeStr, &typeOk);
        if (!typeOk || !isValidName(name)) continue;
        switch (t) {
        case Type::Int:    value = val.toInt(); break;
        case Type::Double: value = val.toDouble(); break;
        case Type::String: value = val.toString(); break;
        case Type::Bool:   value = val.toBool(); break;
        case Type::Roi:    value = val.toObject().toVariantMap(); break;
        case Type::Region: value = val.toArray().toVariantList(); break;
        case Type::Points: value = val.toArray().toVariantList(); break;
        }
        Variable var;
        var.name = name;
        var.type = t;
        var.value = value;
        var.description = desc;
        m_variables.insert(name, var);
    }
    emit variablesChanged();
    return true;
}

// =====================================================================
// v5.4 算子输出 → 全局变量映射
// =====================================================================

void VariableManager::registerOperatorOutput(const QString& nodeId,
                                              const QString& outputName,
                                              const QString& typeName) {
    QMutexLocker locker(&m_mutex);
    if (nodeId.isEmpty() || outputName.isEmpty()) {
        Logger::warn("VariableManager: registerOperatorOutput 参数为空");
        return;
    }
    const QString varName = QString("%1.%2").arg(nodeId, outputName);
    // 若已注册则跳过（避免重复注册覆盖已有值）
    if (m_variables.contains(varName)) {
        return;
    }

    // 按 typeName 映射到 Type 枚举并设置默认值
    // 注意：数组类型（string[]/double[]）在现有 Type 枚举中没有对应项，
    //       归并为 String/Double，实际值以 QVariant 持有 QStringList/QVariantList
    Type t = Type::String;
    QVariant defaultValue;
    const QString lower = typeName.toLower();
    if (lower == "int") {
        t = Type::Int;
        defaultValue = 0;
    } else if (lower == "double") {
        t = Type::Double;
        defaultValue = 0.0;
    } else if (lower == "string") {
        t = Type::String;
        defaultValue = QString();
    } else if (lower == "string[]") {
        // 数组类型归并为 String，值以 QStringList 存储
        t = Type::String;
        defaultValue = QStringList();
    } else if (lower == "double[]") {
        // 数组类型归并为 Double，值以 QVariantList 存储
        t = Type::Double;
        defaultValue = QVariantList();
    } else if (lower == "roi" || lower == "rect") {
        t = Type::Roi;
        QVariantMap m; m["x"]=0.0; m["y"]=0.0; m["w"]=100.0; m["h"]=100.0;
        defaultValue = m;
    } else if (lower == "region") {
        t = Type::Region;
        defaultValue = QVariantList();
    } else if (lower == "points" || lower == "point[]") {
        t = Type::Points;
        defaultValue = QVariantList();
    } else {
        // 未知类型默认 String
        t = Type::String;
        defaultValue = QString();
    }

    // 直接写入 m_variables，绕过 createVariable/isValidName
    // （算子输出变量名含点和连字符，不符合用户变量命名规范）
    Variable v;
    v.name = varName;
    v.type = t;
    v.value = defaultValue;
    v.description = QStringLiteral("算子输出 (%1)").arg(typeName);
    m_variables.insert(varName, v);
    emit variableCreated(varName);
    emit variablesChanged();
    Logger::info(QString("VariableManager: registered operator output %1 (type=%2)")
                  .arg(varName, typeName));
}

void VariableManager::unregisterOperatorOutput(const QString& nodeId,
                                                const QString& outputName) {
    QMutexLocker locker(&m_mutex);
    if (nodeId.isEmpty()) return;

    if (outputName.isEmpty()) {
        // 反注册该节点所有输出（前缀匹配 "nodeId."）
        const QString prefix = QString("%1.").arg(nodeId);
        QStringList toRemove;
        for (auto it = m_variables.constBegin(); it != m_variables.constEnd(); ++it) {
            if (it.key().startsWith(prefix)) {
                toRemove.append(it.key());
            }
        }
        for (const QString& key : toRemove) {
            m_variables.remove(key);
            emit variableRemoved(key);
            Logger::info(QString("VariableManager: unregistered operator output %1").arg(key));
        }
        if (!toRemove.isEmpty()) {
            emit variablesChanged();
        }
    } else {
        const QString varName = QString("%1.%2").arg(nodeId, outputName);
        if (m_variables.remove(varName) > 0) {
            emit variableRemoved(varName);
            emit variablesChanged();
            Logger::info(QString("VariableManager: unregistered operator output %1").arg(varName));
        }
    }
}

void VariableManager::updateOperatorOutputValues(const QString& nodeId,
                                                  const QJsonObject& outputs) {
    QMutexLocker locker(&m_mutex);
    if (nodeId.isEmpty() || outputs.isEmpty()) return;

    const QString prefix = QString("%1.").arg(nodeId);
    bool anyUpdated = false;
    for (auto it = outputs.constBegin(); it != outputs.constEnd(); ++it) {
        const QString varName = prefix + it.key();
        // 仅更新已注册的变量，不主动创建新变量
        auto varIt = m_variables.find(varName);
        if (varIt != m_variables.end()) {
            const QVariant newValue = it.value().toVariant();
            if (varIt->value == newValue) continue;  // 值未变化，跳过
            varIt->value = newValue;
            anyUpdated = true;
            emit valueChanged(varName, newValue);
            Logger::info(QString("VariableManager: updated %1 = %2")
                          .arg(varName, it.value().toString()));
        }
    }
    if (anyUpdated) {
        emit variablesChanged();
    }
}

bool VariableManager::isOperatorOutputRegistered(const QString& nodeId,
                                                  const QString& outputName) const {
    QMutexLocker locker(&m_mutex);
    if (nodeId.isEmpty() || outputName.isEmpty()) return false;
    const QString varName = QString("%1.%2").arg(nodeId, outputName);
    return m_variables.contains(varName);
}

// =====================================================================
// 类型工具
// =====================================================================

QString VariableManager::typeToString(Type t) {
    switch (t) {
    case Type::Int:    return QStringLiteral("int");
    case Type::Double: return QStringLiteral("double");
    case Type::String: return QStringLiteral("string");
    case Type::Bool:   return QStringLiteral("bool");
    case Type::Roi:    return QStringLiteral("roi");
    case Type::Region: return QStringLiteral("region");
    case Type::Points: return QStringLiteral("points");
    }
    return QStringLiteral("string");
}

VariableManager::Type VariableManager::stringToType(const QString& s, bool* ok) {
    if (ok) *ok = true;
    const QString lower = s.toLower();
    if (lower == "int" || lower == "integer") return Type::Int;
    if (lower == "double" || lower == "float" || lower == "real") return Type::Double;
    if (lower == "string" || lower == "str" || lower == "text") return Type::String;
    if (lower == "bool" || lower == "boolean") return Type::Bool;
    if (lower == "roi" || lower == "rect") return Type::Roi;
    if (lower == "region") return Type::Region;
    if (lower == "points" || lower == "point[]") return Type::Points;
    if (ok) *ok = false;
    return Type::String;
}

bool VariableManager::isValidName(const QString& name) {
    if (name.isEmpty()) return false;
    static const QRegularExpression re(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$"));
    return re.match(name).hasMatch();
}

// =====================================================================
// v2.7.0 ROI/Region/Points 辅助方法
// =====================================================================

QRectF VariableManager::toRoi(const QVariant& v) {
    const QVariantMap m = v.toMap();
    if (m.isEmpty()) return QRectF();
    return QRectF(m.value("x").toDouble(), m.value("y").toDouble(),
                  m.value("w").toDouble(), m.value("h").toDouble());
}

QVariant VariableManager::fromRoi(const QRectF& r) {
    QVariantMap m;
    m["x"] = r.x();
    m["y"] = r.y();
    m["w"] = r.width();
    m["h"] = r.height();
    return m;
}

QList<QPointF> VariableManager::toPoints(const QVariant& v) {
    QList<QPointF> pts;
    const QVariantList lst = v.toList();
    for (const QVariant& item : lst) {
        const QVariantMap m = item.toMap();
        if (m.contains("x") && m.contains("y")) {
            pts.append(QPointF(m.value("x").toDouble(), m.value("y").toDouble()));
        }
    }
    return pts;
}

QVariant VariableManager::fromPoints(const QList<QPointF>& pts) {
    QVariantList lst;
    for (const QPointF& p : pts) {
        QVariantMap m;
        m["x"] = p.x();
        m["y"] = p.y();
        lst.append(m);
    }
    return lst;
}

} // namespace QDV
