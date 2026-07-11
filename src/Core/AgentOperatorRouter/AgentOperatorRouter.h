#ifndef QDV_AGENT_OPERATOR_ROUTER_H
#define QDV_AGENT_OPERATOR_ROUTER_H

#include <QString>
#include <QStringList>
#include <QVector>
#include <QMap>
#include <QJsonObject>
#include <QJsonArray>

namespace QDV {

/// 算子与 Agent 角色的映射项
/// 描述：某类任务应使用哪个算子 + 由哪个 Agent 角色执行 + 优先级
struct OperatorAgentMapping {
    QString operatorType;   ///< 算子类型（如 "GaussFilter"）
    QString agentRole;      ///< Agent 角色（如 "Developer" / "Reviewer" / "Gatekeeper"）
    int priority;           ///< 优先级：1=首选, 2=备选

    /// 序列化为 JSON
    QJsonObject toJson() const {
        QJsonObject obj;
        obj["operatorType"] = operatorType;
        obj["agentRole"] = agentRole;
        obj["priority"] = priority;
        return obj;
    }

    /// 从 JSON 反序列化
    static OperatorAgentMapping fromJson(const QJsonObject& obj) {
        OperatorAgentMapping m;
        m.operatorType = obj["operatorType"].toString();
        m.agentRole = obj["agentRole"].toString();
        m.priority = obj["priority"].toInt(1);
        return m;
    }
};

/// Agent 协作框架的算子路由组件
///
/// 设计目标：基于任务类型（参照 Halcon 算子分类标准），
///           返回推荐的算子 + Agent 角色映射列表，
///           供 Agent 协作框架调度使用。
///
/// 路由策略：静态硬编码映射表（QMap<QString, QVector<OperatorAgentMapping>>）
///           - key = 任务类型字符串（如 "滤波预处理"）
///           - value = 算子+Agent角色+优先级列表
///
/// 映射规则：基于算子 manifest.json 的 category 字段与 .agent_mesh/roles/*.md 的角色职责
class AgentOperatorRouter {
public:
    AgentOperatorRouter();

    /// 路由：任务类型 → 算子+Agent角色列表
    /// 空任务或未知任务返回空列表（不崩溃）
    QVector<OperatorAgentMapping> route(const QString& taskType) const;

    /// 获取所有支持的任务类型
    QStringList supportedTaskTypes() const;

    /// 查询某任务类型是否被支持
    bool isSupported(const QString& taskType) const;

    /// 获取某任务类型的首选映射（priority=1）
    /// 不存在返回空的 OperatorAgentMapping（operatorType 为空）
    OperatorAgentMapping firstChoice(const QString& taskType) const;

    /// 路由结果序列化为 JSON 数组
    QJsonArray routeAsJson(const QString& taskType) const;

private:
    QMap<QString, QVector<OperatorAgentMapping>> m_routingTable;

    /// 初始化静态路由表（12 类任务，参照 Halcon 分类）
    void initRoutingTable();
};

} // namespace QDV

#endif // QDV_AGENT_OPERATOR_ROUTER_H
