// ============================================================================
// 输出连接优化（spec：editor-output-connection-optimization）新增组件单元测试
// ----------------------------------------------------------------------------
// 覆盖范围（Task 3/6/9，Task 13 回归）：
//   1. PortBindingManager：bindingsForInput（多输入）、consumersOfOutput（多扇出）、
//      validateBinding（类型不兼容返回 false）、isOutputEnabled（禁用输出返回 false）
//   2. OutputConflictDetector：detectAll 识别 dupAlias / typeMismatch / multiInputAmbiguity
//   3. OperatorRecommender：recommend 返回 ≤3 且不含当前类型、setWeight/getWeight 持久化
//
// 测试框架：catch2_minimal（TEST_CASE 静态注册，REQUIRE/CHECK 宏）
// 说明：OutputConflictDetector 的 dupAlias / multiInputAmbiguity 依赖算子元数据的
//       outputs/inputs（含 alias），故通过 OperatorDescriptorsGuard 注入自定义算子元数据；
//       typeMismatch 依赖 ToolFactory 端口类型，直接注入 BranchControl→Caliper 节点即可。
// ============================================================================
#include "../catch2/catch2_minimal.hpp"
#include "UI/EditViewBridge.h"
#include "UI/PortBindingManager.h"
#include "UI/OutputConflictDetector.h"
#include "UI/OperatorRecommender.h"
#include "UI/OperatorDescriptors.h"

#include <QApplication>
#include <QTemporaryFile>
#include <QVariant>
#include <QVariantMap>
#include <QVariantList>
#include <QStringList>

// === QApplication 单例（与 test_main.cpp 的全局实例复用） ===
static int argc = 0;
static QApplication* app() {
    return qobject_cast<QApplication*>(QCoreApplication::instance());
}
inline void ensureApp() { app(); }

// === RAII guard：测试期间加载自定义算子元数据，作用域结束自动 reset ===
class OperatorDescriptorsGuard {
public:
    explicit OperatorDescriptorsGuard(const QString& jsonContent) {
        m_tmp.setFileTemplate(QStringLiteral("qdv_opdesc_outconn_XXXXXX.json"));
        m_tmp.setAutoRemove(true);
        REQUIRE(m_tmp.open());
        m_tmp.write(jsonContent.toUtf8());
        m_tmp.close();
        QString err;
        m_loaded = QDV::UI::OperatorDescriptors::loadFromJson(m_tmp.fileName(), &err);
        m_error = err;
    }
    ~OperatorDescriptorsGuard() { QDV::UI::OperatorDescriptors::reset(); }
    bool loaded() const { return m_loaded; }
    QString error() const { return m_error; }
private:
    QTemporaryFile m_tmp;
    bool m_loaded = false;
    QString m_error;
};

// === 辅助：向 bridge 注入节点 / 连接（复用 public currentNodesRef/connectionsRef） ===
static void addNode(EditViewBridge& b, const QString& id, const QString& type) {
    QVariantMap n;
    n["id"]     = id;
    n["type"]   = type;
    n["x"]      = 0.0;
    n["y"]      = 0.0;
    n["params"] = QVariantMap();
    b.currentNodesRef().append(n);
    b.notifyCurrentNodesChanged();
}

static void addConn(EditViewBridge& b, const QString& fromId, const QString& fromPort,
                    const QString& toId, const QString& toPort) {
    QVariantMap c;
    c["fromId"]   = fromId;
    c["fromPort"] = fromPort;
    c["toId"]     = toId;
    c["toPort"]   = toPort;
    b.connectionsRef().append(c);
    b.notifyConnectionsChanged();
}

// 判断冲突列表是否包含指定 kind
static bool hasConflictKind(const QVariantList& conflicts, const QString& kind) {
    for (const QVariant& v : conflicts) {
        if (v.toMap().value("kind").toString() == kind) return true;
    }
    return false;
}

// ============================================================================
// PortBindingManager
// ============================================================================

// --- 测试 1：bindingsForInput 多输入返回多条上游 ---
TEST_CASE("PortBindingManager bindingsForInput multi-upstream", "[outputconn][binding]") {
    ensureApp();
    EditViewBridge bridge;
    const QString a = bridge.addOperator(QStringLiteral("Threshold"), 0, 0);
    const QString b = bridge.addOperator(QStringLiteral("EdgeDetect"), 0, 0);
    const QString c = bridge.addOperator(QStringLiteral("Threshold"), 200, 0);
    REQUIRE_FALSE(a.isEmpty());
    REQUIRE_FALSE(b.isEmpty());
    REQUIRE_FALSE(c.isEmpty());

    // 注入 a→c:in、b→c:in 两条连接（同一下游输入端口）
    addConn(bridge, a, QStringLiteral("out"), c, QStringLiteral("in"));
    addConn(bridge, b, QStringLiteral("out"), c, QStringLiteral("in"));
    REQUIRE(bridge.connections().size() == 2);

    auto* pbm = qobject_cast<PortBindingManager*>(bridge.portBindingManager());
    REQUIRE(pbm != nullptr);

    const auto bindings = pbm->bindingsForInput(c, QStringLiteral("in"));
    REQUIRE(bindings.size() == 2);
    // 两个上游均命中
    bool hasA = false, hasB = false;
    for (const auto& bd : bindings) {
        if (bd.upstreamToolId == a) hasA = true;
        if (bd.upstreamToolId == b) hasB = true;
    }
    REQUIRE(hasA);
    REQUIRE(hasB);

    // 不存在该输入端口时返回空
    REQUIRE(pbm->bindingsForInput(c, QStringLiteral("nonexistentPort")).isEmpty());
}

// --- 测试 2：consumersOfOutput 多输出扇出返回多个下游 ---
TEST_CASE("PortBindingManager consumersOfOutput fan-out", "[outputconn][binding]") {
    ensureApp();
    EditViewBridge bridge;
    const QString a = bridge.addOperator(QStringLiteral("Threshold"), 0, 0);
    const QString c = bridge.addOperator(QStringLiteral("EdgeDetect"), 200, 0);
    const QString d = bridge.addOperator(QStringLiteral("BlobDetect"), 400, 0);
    REQUIRE_FALSE(a.isEmpty() || c.isEmpty() || d.isEmpty());

    // a → c、a → d（单输出扇出两个下游）
    addConn(bridge, a, QStringLiteral("out"), c, QStringLiteral("in"));
    addConn(bridge, a, QStringLiteral("out"), d, QStringLiteral("in"));

    auto* pbm = qobject_cast<PortBindingManager*>(bridge.portBindingManager());
    REQUIRE(pbm != nullptr);

    const auto consumers = pbm->consumersOfOutput(a, QStringLiteral("out"));
    REQUIRE(consumers.size() == 2);
    bool hasC = false, hasD = false;
    for (const auto& bd : consumers) {
        if (bd.downstreamToolId == c) hasC = true;
        if (bd.downstreamToolId == d) hasD = true;
    }
    REQUIRE(hasC);
    REQUIRE(hasD);
}

// --- 测试 3：validateBinding 对类型不兼容返回 false ---
// BranchControl.conditionMet(Bool) → Caliper.image(Image) 类型不兼容
TEST_CASE("PortBindingManager validateBinding type-incompatible returns false", "[outputconn][binding]") {
    ensureApp();
    EditViewBridge bridge;
    addNode(bridge, QStringLiteral("bb"), QStringLiteral("BranchControl"));
    addNode(bridge, QStringLiteral("cp"), QStringLiteral("Caliper"));

    auto* pbm = qobject_cast<PortBindingManager*>(bridge.portBindingManager());
    REQUIRE(pbm != nullptr);

    QString reason;
    REQUIRE_FALSE(pbm->validateBinding(QStringLiteral("bb"), QStringLiteral("conditionMet"),
                                       QStringLiteral("cp"), QStringLiteral("image"), &reason));
    REQUIRE_FALSE(reason.isEmpty());
}

// --- 测试 4：validateBinding 端口类型兼容返回 true ---
// BranchControl.image(Image) → Caliper.image(Image) 类型兼容
TEST_CASE("PortBindingManager validateBinding compatible returns true", "[outputconn][binding]") {
    ensureApp();
    EditViewBridge bridge;
    addNode(bridge, QStringLiteral("fc"), QStringLiteral("FlowJoin"));
    addNode(bridge, QStringLiteral("bb"), QStringLiteral("BranchControl"));

    auto* pbm = qobject_cast<PortBindingManager*>(bridge.portBindingManager());
    REQUIRE(pbm != nullptr);

    QString reason;
    REQUIRE(pbm->validateBinding(QStringLiteral("fc"), QStringLiteral("image"),
                                 QStringLiteral("bb"), QStringLiteral("image"), &reason));
}

// --- 测试 5：isOutputEnabled 对未启用输出返回 false ---
TEST_CASE("PortBindingManager isOutputEnabled disabled output returns false", "[outputconn][binding]") {
    ensureApp();
    EditViewBridge bridge;
    const QString ai = bridge.addOperator(QStringLiteral("AiClassify"), 0, 0);
    REQUIRE_FALSE(ai.isEmpty());

    auto* pbm = qobject_cast<PortBindingManager*>(bridge.portBindingManager());
    REQUIRE(pbm != nullptr);

    // 默认启用
    REQUIRE(pbm->isOutputEnabled(ai, QStringLiteral("classId")));

    // 禁用 classId 输出
    QVariantMap oc = bridge.getOutputConfig(ai);
    REQUIRE(oc.size() == 5);
    QVariantMap classId = oc.value(QStringLiteral("classId")).toMap();
    classId[QStringLiteral("enabled")] = false;
    oc[QStringLiteral("classId")] = classId;
    bridge.updateOutputConfig(ai, oc);

    REQUIRE_FALSE(pbm->isOutputEnabled(ai, QStringLiteral("classId")));
    // 其他输出不受影响
    REQUIRE(pbm->isOutputEnabled(ai, QStringLiteral("className")));
}

// ============================================================================
// OutputConflictDetector
// ============================================================================

// --- 测试 6：dupAlias —— 同一节点两个输出别名相同 ---
// 通过自定义算子元数据注入两个 alias 相同的输出项
static const char* kDupAliasJson = R"({
    "operators": [
        {
            "type": "AiClassify",
            "cnName": "AI 分类",
            "category": "AI",
            "params": [],
            "outputs": [
                {"name": "out1", "cnName": "输出1", "typeName": "int", "alias": "结果"},
                {"name": "out2", "cnName": "输出2", "typeName": "int", "alias": "结果"}
            ]
        }
    ]
})";

TEST_CASE("OutputConflictDetector detectAll dupAlias", "[outputconn][conflict]") {
    ensureApp();
    OperatorDescriptorsGuard guard(QString::fromLatin1(kDupAliasJson));
    REQUIRE(guard.loaded());

    EditViewBridge bridge;
    addNode(bridge, QStringLiteral("n1"), QStringLiteral("AiClassify"));

    auto* cd = qobject_cast<OutputConflictDetector*>(bridge.conflictDetector());
    REQUIRE(cd != nullptr);

    const QVariantList conflicts = cd->detectAll();
    REQUIRE(hasConflictKind(conflicts, QStringLiteral("dupAlias")));
    REQUIRE(cd->hasConflicts());
}

// --- 测试 7：typeMismatch —— 端口类型不兼容的连接 ---
TEST_CASE("OutputConflictDetector detectAll typeMismatch", "[outputconn][conflict]") {
    ensureApp();
    EditViewBridge bridge;
    addNode(bridge, QStringLiteral("bb"), QStringLiteral("BranchControl"));
    addNode(bridge, QStringLiteral("cp"), QStringLiteral("Caliper"));
    // BranchControl.conditionMet(Bool) → Caliper.image(Image) 不兼容
    addConn(bridge, QStringLiteral("bb"), QStringLiteral("conditionMet"),
            QStringLiteral("cp"), QStringLiteral("image"));

    auto* cd = qobject_cast<OutputConflictDetector*>(bridge.conflictDetector());
    REQUIRE(cd != nullptr);

    const QVariantList conflicts = cd->detectAll();
    REQUIRE(hasConflictKind(conflicts, QStringLiteral("typeMismatch")));
}

// --- 测试 8：multiInputAmbiguity —— 下游输入被多个上游绑定且输出别名相同 ---
static const char* kMultiInputJson = R"({
    "operators": [
        {
            "type": "AiClassify",
            "cnName": "AI 分类",
            "category": "AI",
            "params": [],
            "inputs": [
                {"name": "inA", "cnName": "输入A", "typeName": "int"}
            ],
            "outputs": [
                {"name": "out1", "cnName": "输出1", "typeName": "int", "alias": "X"},
                {"name": "out2", "cnName": "输出2", "typeName": "int", "alias": "Y"}
            ]
        }
    ]
})";

TEST_CASE("OutputConflictDetector detectAll multiInputAmbiguity", "[outputconn][conflict]") {
    ensureApp();
    OperatorDescriptorsGuard guard(QString::fromLatin1(kMultiInputJson));
    REQUIRE(guard.loaded());

    EditViewBridge bridge;
    addNode(bridge, QStringLiteral("up1"), QStringLiteral("AiClassify"));
    addNode(bridge, QStringLiteral("up2"), QStringLiteral("AiClassify"));
    addNode(bridge, QStringLiteral("dn"),  QStringLiteral("AiClassify"));
    // 两个上游都绑定到 dn.inA，且输出别名均为 X → 语义歧义
    addConn(bridge, QStringLiteral("up1"), QStringLiteral("out1"),
            QStringLiteral("dn"), QStringLiteral("inA"));
    addConn(bridge, QStringLiteral("up2"), QStringLiteral("out1"),
            QStringLiteral("dn"), QStringLiteral("inA"));

    auto* cd = qobject_cast<OutputConflictDetector*>(bridge.conflictDetector());
    REQUIRE(cd != nullptr);

    const QVariantList conflicts = cd->detectAll();
    REQUIRE(hasConflictKind(conflicts, QStringLiteral("multiInputAmbiguity")));
}

// ============================================================================
// OperatorRecommender
// ============================================================================

// --- 测试 9：recommend 返回 ≤3 且不含当前类型 ---
TEST_CASE("OperatorRecommender recommend returns top3 excluding current type", "[outputconn][recommend]") {
    ensureApp();
    EditViewBridge bridge;
    const QString id = bridge.addOperator(QStringLiteral("Threshold"), 0, 0);
    REQUIRE_FALSE(id.isEmpty());

    auto* rec = qobject_cast<OperatorRecommender*>(bridge.recommender());
    REQUIRE(rec != nullptr);

    const QVariantList recs = rec->recommend(id);
    // 兜底保证：新方案无连线/无使用记录时，推荐非空（避免推荐区空白）
    REQUIRE(recs.size() > 0);
    REQUIRE(recs.size() <= 3);
    for (const QVariant& v : recs) {
        REQUIRE(v.toMap().value("type").toString() != QStringLiteral("Threshold"));
    }
}

// --- 测试 10：setWeight/getWeight 持久化读写 ---
TEST_CASE("OperatorRecommender setWeight getWeight roundtrip", "[outputconn][recommend]") {
    ensureApp();
    EditViewBridge bridge;
    auto* rec = qobject_cast<OperatorRecommender*>(bridge.recommender());
    REQUIRE(rec != nullptr);

    // 修改并读取三类权重
    rec->setWeight(QStringLiteral("typeCompat"), 0.6);
    rec->setWeight(QStringLiteral("structure"), 0.25);
    rec->setWeight(QStringLiteral("frequency"), 0.15);
    REQUIRE(qFuzzyCompare(rec->getWeight(QStringLiteral("typeCompat")), 0.6));
    REQUIRE(qFuzzyCompare(rec->getWeight(QStringLiteral("structure")), 0.25));
    REQUIRE(qFuzzyCompare(rec->getWeight(QStringLiteral("frequency")), 0.15));

    // 未知 key 忽略，getWeight 返回 0.0
    rec->setWeight(QStringLiteral("unknown"), 5.0);
    REQUIRE(qFuzzyCompare(rec->getWeight(QStringLiteral("unknown")), 0.0));

    // 恢复默认权重，避免影响其他测试
    rec->setWeight(QStringLiteral("typeCompat"), 0.5);
    rec->setWeight(QStringLiteral("structure"), 0.3);
    rec->setWeight(QStringLiteral("frequency"), 0.2);
}

// --- 测试 11：recommendRecent 排除自身类型且 ≤3 ---
TEST_CASE("OperatorRecommender recommendRecent excludes current type", "[outputconn][recommend]") {
    ensureApp();
    EditViewBridge bridge;
    auto* rec = qobject_cast<OperatorRecommender*>(bridge.recommender());
    REQUIRE(rec != nullptr);

    const QVariantList recs = rec->recommendRecent(QStringLiteral("Threshold"));
    REQUIRE(recs.size() <= 3);
    for (const QVariant& v : recs) {
        REQUIRE(v.toMap().value("type").toString() != QStringLiteral("Threshold"));
        REQUIRE(v.toMap().value("reason").toString() == QStringLiteral("最近使用"));
    }
}

// --- 测试 12：多连接支持（验证3）同一输出端口可建立多条不同目标连接 ---
TEST_CASE("EditViewBridge allows second connection from same output to different target", "[outputconn][connect]") {
    ensureApp();
    EditViewBridge bridge;
    const QString a = bridge.addOperator(QStringLiteral("Threshold"), 0, 0);
    const QString b = bridge.addOperator(QStringLiteral("EdgeDetect"), 300, 0);
    const QString c = bridge.addOperator(QStringLiteral("Threshold"), 600, 0);
    REQUIRE_FALSE(a.isEmpty());
    REQUIRE_FALSE(b.isEmpty());
    REQUIRE_FALSE(c.isEmpty());

    // 第一条连接：A输出 → B输入
    bridge.connectNodes(a, QStringLiteral("output"), b, QStringLiteral("input"));
    REQUIRE(bridge.connections().size() == 1);

    // 第二条连接：A输出 → C输入（不同目标，应被允许）
    bridge.connectNodes(a, QStringLiteral("output"), c, QStringLiteral("input"));
    REQUIRE(bridge.connections().size() == 2);

    // 完全重复的连线（A输出 → B输入）应被阻断，连接数保持 2
    bridge.connectNodes(a, QStringLiteral("output"), b, QStringLiteral("input"));
    REQUIRE(bridge.connections().size() == 2);
}