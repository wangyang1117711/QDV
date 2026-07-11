#include "Core/VariableManager.h"
#include "Core/Logger.h"
#include <QRegularExpression>
#include <QRegularExpressionMatchIterator>
#include <QJsonArray>
#include <QJsonObject>

namespace QDV {

VariableManager::VariableManager(QObject* parent) : QObject(parent) {}

VariableManager::~VariableManager() = default;

// =====================================================================
// CRUD
// =====================================================================

bool VariableManager::createVariable(const QString& name, const QString& typeStr,
                                      const QVariant& value, const QString& description) {
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
    }
    m_variables.insert(name, v);
    emit variableCreated(name);
    emit variablesChanged();
    Logger::info("VariableManager: 创建变量 " + name + " (" + typeStr + ")");
    return true;
}

bool VariableManager::removeVariable(const QString& name) {
    if (!m_variables.contains(name)) return false;
    m_variables.remove(name);
    emit variableRemoved(name);
    emit variablesChanged();
    return true;
}

bool VariableManager::setValue(const QString& name, const QVariant& value) {
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
    }
    if (it->value == converted) return true;  // 值未变化，不发信号
    it->value = converted;
    emit valueChanged(name, converted);
    return true;
}

bool VariableManager::setDescription(const QString& name, const QString& description) {
    auto it = m_variables.find(name);
    if (it == m_variables.end()) return false;
    if (it->description == description) return true;
    it->description = description;
    emit descriptionChanged(name, description);
    return true;
}

QVariant VariableManager::value(const QString& name) const {
    auto it = m_variables.constFind(name);
    if (it == m_variables.constEnd()) return QVariant();
    return it->value;
}

QVariantMap VariableManager::variable(const QString& name) const {
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
    return m_variables.size();
}

bool VariableManager::exists(const QString& name) const {
    return m_variables.contains(name);
}

void VariableManager::clear() {
    if (m_variables.isEmpty()) return;
    m_variables.clear();
    emit variablesChanged();
}

// =====================================================================
// 变量绑定解析
// =====================================================================

QString VariableManager::resolveBinding(const QString& input, bool* ok) const {
    if (ok) *ok = true;
    if (input.isEmpty()) return input;

    // 匹配 ${varName}
    static const QRegularExpression re(QStringLiteral("\\$\\{([A-Za-z_][A-Za-z0-9_]*)\\}"));
    QString result = input;
    QRegularExpressionMatchIterator it = re.globalMatch(input);
    bool allResolved = true;
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        const QString varName = m.captured(1);
        if (!m_variables.contains(varName)) {
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
    QStringList refs;
    static const QRegularExpression re(QStringLiteral("\\$\\{([A-Za-z_][A-Za-z0-9_]*)\\}"));
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
        }
        arr.append(obj);
    }
    return arr;
}

bool VariableManager::fromJson(const QJsonArray& arr) {
    clear();
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
// 类型工具
// =====================================================================

QString VariableManager::typeToString(Type t) {
    switch (t) {
    case Type::Int:    return QStringLiteral("int");
    case Type::Double: return QStringLiteral("double");
    case Type::String: return QStringLiteral("string");
    case Type::Bool:   return QStringLiteral("bool");
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
    if (ok) *ok = false;
    return Type::String;
}

bool VariableManager::isValidName(const QString& name) {
    if (name.isEmpty()) return false;
    static const QRegularExpression re(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$"));
    return re.match(name).hasMatch();
}

} // namespace QDV
