// P1-3 typed ports：端口描述符实现
// 提供 PortType 名称映射、类型兼容性校验（连线期）、QML 序列化
#include "Core/VisionTool.h"
#include <QVariantList>

namespace QDV {

QString PortDescriptor::typeName(PortType t) {
    switch (t) {
        case PortType::Any:     return QStringLiteral("Any");
        case PortType::Image:   return QStringLiteral("Image");
        case PortType::Region:  return QStringLiteral("Region");
        case PortType::Pose:    return QStringLiteral("Pose");
        case PortType::Contour: return QStringLiteral("Contour");
        case PortType::String:  return QStringLiteral("String");
        case PortType::Number:  return QStringLiteral("Number");
        case PortType::Bool:    return QStringLiteral("Bool");
        case PortType::Points:  return QStringLiteral("Points");
    }
    return QStringLiteral("Unknown");
}

bool PortDescriptor::compatible(PortType outType, PortType inType) {
    // Any 兼容一切（旧算子未声明端口时默认 Any，放行）
    if (outType == PortType::Any || inType == PortType::Any) {
        return true;
    }
    // 同类型直接兼容
    if (outType == inType) {
        return true;
    }
    // Region/Contour/Points 都是点集类，互相兼容
    if ((outType == PortType::Region || outType == PortType::Contour || outType == PortType::Points) &&
        (inType  == PortType::Region || inType  == PortType::Contour || inType  == PortType::Points)) {
        return true;
    }
    // 其余类型不兼容（如 String → Image 应阻断）
    return false;
}

QVariantMap PortDescriptor::toMap() const {
    QVariantMap m;
    m["name"]   = name;
    m["cnName"] = cnName;
    m["type"]   = typeName(type);
    m["dir"]    = (dir == PortDirection::In) ? QStringLiteral("in") : QStringLiteral("out");
    m["desc"]   = desc;
    return m;
}

} // namespace QDV
