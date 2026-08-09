#ifndef OPERATOR_RECOMMENDER_H
#define OPERATOR_RECOMMENDER_H

// ============================================================================
// OperatorRecommender —— 智能算子推荐引擎（spec：editor-output-connection-optimization，Task 9）
// ----------------------------------------------------------------------------
// 职责：
//   1. 给定当前选中算子节点，结合方案结构（节点连接关系）+ 使用频率，
//      输出 Top3 候选算子（{type, confidence, reason}）。
//   2. 基于最近使用（EditViewBridge::m_recents）输出推荐。
//   3. 三类权重（typeCompat / structure / frequency）可配置并持久化到
//      config/recommender.json。
//
// 设计要点：
//   - 与 PortBindingManager 一致，持有 EditViewBridge* 弱引用，通过其 public
//     接口（operatorTypes / getOperatorPorts / getShortDesc）访问算子元数据。
//   - 通过 friend 声明访问 EditViewBridge 私有数据（m_useCounts / m_recents /
//     m_currentNodes / m_connections / nodeTypeById），避免数据复制。
//   - 不新增算子元数据存储，端口类型直接复用 getOperatorPorts 返回结构。
// ============================================================================

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

class EditViewBridge;

class OperatorRecommender : public QObject {
    Q_OBJECT
public:
    explicit OperatorRecommender(EditViewBridge* bridge, QObject* parent = nullptr);
    ~OperatorRecommender() override = default;

    /// 给定当前选中节点 nodeId，返回 Top3 候选算子 [{type, confidence, reason}]
    /// reason 为中文说明（端口兼容 / 结构下游 / 使用频率 组合）
    Q_INVOKABLE QVariantList recommend(const QString& nodeId);

    /// 基于最近使用返回 Top3（排除 nodeType 自身）
    /// 每个元素 {type, confidence, reason="最近使用"}
    Q_INVOKABLE QVariantList recommendRecent(const QString& nodeType);

    /// 设置权重并持久化（keys: "typeCompat","structure","frequency"）
    Q_INVOKABLE void setWeight(const QString& key, double v);

    /// 读取当前权重（QML 端展示用）
    Q_INVOKABLE double getWeight(const QString& key) const;

private:
    struct Candidate {
        QString  type;
        double   score = 0.0;
        QStringList reasons;
    };

    /// 两类端口类型字符串是否兼容（镜像 QDV::PortDescriptor::compatible 逻辑）
    static bool portTypesCompatible(const QString& outType, const QString& inType);

    /// 当前算子类型 T 的输出端口是否与候选类型 C 的任一输入端口兼容
    /// @param outTypeMatched 输出端口名（用于 reason）
    bool isTypeCompat(const QString& curType, const QString& candType,
                      QStringList& matchedOut) const;

    /// 从 config/recommender.json 加载权重
    void loadWeights();
    /// 持久化权重到 config/recommender.json
    void saveWeights() const;

    EditViewBridge* m_bridge;

    // 三类权重（0~1，总和无强制约束，用于线性加权）
    double m_wTypeCompat = 0.5;   ///< 端口类型兼容权重
    double m_wStructure  = 0.3;   ///< 方案结构（下游关联）权重
    double m_wFrequency  = 0.2;   ///< 使用频率权重

    QString m_configPath;         ///< config/recommender.json 绝对路径
};

#endif // OPERATOR_RECOMMENDER_H