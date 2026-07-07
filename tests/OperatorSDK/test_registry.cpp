#include "../catch2/catch2_minimal.hpp"
#include "OperatorSDK/IOperatorRegistry.h"
#include "OperatorSDK/IOperator.h"

using namespace QDV;

// 测试用桩算子：实现 IOperator 接口，用于注册表测试
namespace {
class StubOperatorA : public IOperator {
public:
    QString type() const override { return "StubA"; }
    QString version() const override { return "1.0.0"; }
    IOperator* clone() const override { return new StubOperatorA(*this); }
};
class StubOperatorB : public IOperator {
public:
    QString type() const override { return "StubB"; }
    QString version() const override { return "1.0.0"; }
    IOperator* clone() const override { return new StubOperatorB(*this); }
};
} // namespace

// RT-005：空注册表查询（注：单例全局共享，仅验证不崩溃 + 返回合法值）
TEST_CASE("OperatorRegistry: availableTypes 不崩溃", "[OperatorSDK][RT-005]") {
    QStringList types = IOperatorRegistry::instance().availableTypes();
    // 不崩溃即通过；可能为空或含已有注册项
    REQUIRE(types.size() >= 0);
}

TEST_CASE("OperatorRegistry: 未注册 type 创建返回 nullptr", "[OperatorSDK][RT-005]") {
    IOperator* op = IOperatorRegistry::instance().createOperator("DefinitelyNotRegistered_XYZ");
    REQUIRE(op == nullptr);
}

TEST_CASE("OperatorRegistry: 未注册 type 查询返回空", "[OperatorSDK]") {
    REQUIRE_FALSE(IOperatorRegistry::instance().isRegistered("DefinitelyNotRegistered_XYZ"));
    REQUIRE(IOperatorRegistry::instance().versionOf("DefinitelyNotRegistered_XYZ").isEmpty());
}

// RT-001：算子重复注册（使用唯一 type 名避免与其他测试干扰）
TEST_CASE("OperatorRegistry: 重复注册返回 false 不覆盖 (RT-001)", "[OperatorSDK][RT-001]") {
    const QString uniqueType = "StubForRT001_" + QString::number(__LINE__);
    bool first = IOperatorRegistry::instance().registerOperator(
        uniqueType, "1.0.0", []() -> IOperator* { return new StubOperatorA(); });
    REQUIRE(first);

    // 第二次注册同 type 必须失败，不覆盖
    bool second = IOperatorRegistry::instance().registerOperator(
        uniqueType, "2.0.0", []() -> IOperator* { return new StubOperatorB(); });
    REQUIRE_FALSE(second);

    // 版本仍是首次注册的 1.0.0
    REQUIRE(IOperatorRegistry::instance().versionOf(uniqueType) == "1.0.0");

    // 创建出来的应是 StubOperatorA（type()==StubA）
    IOperator* op = IOperatorRegistry::instance().createOperator(uniqueType);
    REQUIRE(op != nullptr);
    REQUIRE(op->type() == "StubA");
    delete op;
}

TEST_CASE("OperatorRegistry: 不同 type 可分别注册并创建", "[OperatorSDK]") {
    const QString typeA = "StubA_" + QString::number(__LINE__);
    const QString typeB = "StubB_" + QString::number(__LINE__);

    REQUIRE(IOperatorRegistry::instance().registerOperator(
        typeA, "1.0.0", []() -> IOperator* { return new StubOperatorA(); }));
    REQUIRE(IOperatorRegistry::instance().registerOperator(
        typeB, "1.0.0", []() -> IOperator* { return new StubOperatorB(); }));

    REQUIRE(IOperatorRegistry::instance().isRegistered(typeA));
    REQUIRE(IOperatorRegistry::instance().isRegistered(typeB));

    IOperator* a = IOperatorRegistry::instance().createOperator(typeA);
    IOperator* b = IOperatorRegistry::instance().createOperator(typeB);
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    REQUIRE(a->type() == "StubA");
    REQUIRE(b->type() == "StubB");
    delete a;
    delete b;
}
