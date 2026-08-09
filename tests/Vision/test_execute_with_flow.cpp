// ============================================================================
// ToolChainExecutor::executeWithFlow 单元测试（spec：editor-output-connection-optimization，Task 4/13）
// ----------------------------------------------------------------------------
// 覆盖范围：
//   1. executeWithFlow 多输入：两个图像预处理算子 → 双输入合并且算子，端口绑定后
//      执行成功（executeWithFlow 返回 true）
//   2. flowInputsFor 能取到各上游的 typed 数据（按下游端口名合并）
//   3. 拓扑排序：多上游依赖下，下游仅在其全部上游执行后运行
//   4. 空初始输入 / 无绑定的回退行为
//
// 测试框架：catch2_minimal（TEST_CASE 静态注册，REQUIRE/CHECK 宏）
// 设计原则（AGENTS.md 红队测试）：每条关键路径至少 1 条对抗性用例（空输入、无绑定）
// ============================================================================
#include "../catch2/catch2_minimal.hpp"
#include "Vision/ToolChainExecutor.h"
#include "Vision/ToolFactory.h"
#include "Core/VisionTool.h"
#include <opencv2/core/mat.hpp>
#include <opencv2/imgproc.hpp>
#include <QList>
#include <QVariantMap>
#include <QApplication>

using namespace QDV;

// === QApplication 单例（与 test_main.cpp 的全局实例复用） ===
static int argc = 0;
static QApplication* app() {
    return qobject_cast<QApplication*>(QCoreApplication::instance());
}
inline void ensureApp() { app(); }

// ============================================================================
// 用例 1：多输入 executeWithFlow 执行成功 + flowInputsFor 取到各上游数据
// 结构：EdgeDetect(src1) ─┐
//                         ├─ ImageMerge(merge)
//       Threshold(src2) ──┘
// 说明：EdgeDetect 写入 result.data["edgeCount"]，Threshold 写入
//       result.data["whitePixels"]；通过端口绑定把这两个 named 数据映射到
//       merge 的 in1 / in2 输入端口，验证 flowInputsFor 能取回上游数据。
// ============================================================================
TEST_CASE("ToolChainExecutor executeWithFlow multi-input success", "[toolchain][flow]") {
    ensureApp();
    ToolChainExecutor executor;

    VisionTool* src1 = ToolFactory::instance()->createTool(QStringLiteral("EdgeDetect"));
    VisionTool* src2 = ToolFactory::instance()->createTool(QStringLiteral("Threshold"));
    VisionTool* merge = ToolFactory::instance()->createTool(QStringLiteral("ImageMerge"));
    REQUIRE(src1 != nullptr);
    REQUIRE(src2 != nullptr);
    REQUIRE(merge != nullptr);

    // 配置合并且算子
    QJsonObject mergeParams;
    mergeParams["mergeType"] = QStringLiteral("horizontal");
    REQUIRE(merge->configure(mergeParams));

    QList<VisionTool*> tools{src1, src2, merge};
    executor.setTools(tools);

    // 端口绑定：src1[edgeCount] → merge[in1]，src2[whitePixels] → merge[in2]
    QList<ToolChainExecutor::FlowBinding> bindings;
    bindings.append({src1->id(), QStringLiteral("edgeCount"), merge->id(), QStringLiteral("in1")});
    bindings.append({src2->id(), QStringLiteral("whitePixels"), merge->id(), QStringLiteral("in2")});
    executor.setFlowBindings(bindings);

    cv::Mat testMat(100, 100, CV_8UC3, cv::Scalar(64, 64, 64));
    const bool ok = executor.executeWithFlow(testMat);
    REQUIRE(ok == true);

    // 下游算子成功执行
    const ToolResult mergeRes = executor.getResult(merge->id());
    REQUIRE(mergeRes.ok == true);

    // flowInputsFor 能取到各上游的 typed 数据（按下游端口名合并）
    const QVariantMap inputs = executor.flowInputsFor(merge->id());
    REQUIRE(inputs.contains("in1"));
    REQUIRE(inputs.contains("in2"));

    delete src1;
    delete src2;
    delete merge;
}

// ============================================================================
// 用例 2：拓扑排序 —— 下游仅在全部上游执行后运行
// 验证：executeWithFlow 内所有算子均执行（executed == 3），且下游结果可用
// ============================================================================
TEST_CASE("ToolChainExecutor executeWithFlow all nodes executed", "[toolchain][flow]") {
    ensureApp();
    ToolChainExecutor executor;

    VisionTool* a = ToolFactory::instance()->createTool(QStringLiteral("EdgeDetect"));
    VisionTool* b = ToolFactory::instance()->createTool(QStringLiteral("Threshold"));
    VisionTool* c = ToolFactory::instance()->createTool(QStringLiteral("ImageMerge"));
    REQUIRE(a && b && c);

    QJsonObject cp;
    cp["mergeType"] = QStringLiteral("vertical");
    REQUIRE(c->configure(cp));

    executor.setTools({a, b, c});
    QList<ToolChainExecutor::FlowBinding> bindings;
    bindings.append({a->id(), QStringLiteral("edgeCount"), c->id(), QStringLiteral("in1")});
    bindings.append({b->id(), QStringLiteral("whitePixels"), c->id(), QStringLiteral("in2")});
    executor.setFlowBindings(bindings);

    int executed = 0;
    QObject::connect(&executor, &ToolChainExecutor::toolExecuted,
                     [&](const QString&, const ToolResult&) { executed++; });

    cv::Mat testMat(100, 100, CV_8UC3, cv::Scalar(128, 128, 128));
    REQUIRE(executor.executeWithFlow(testMat) == true);
    REQUIRE(executed == 3);

    // 下游（merge）结果已就绪
    REQUIRE(executor.getResult(c->id()).ok == true);

    delete a;
    delete b;
    delete c;
}

// ============================================================================
// 用例 3：对抗性 —— 空初始输入应返回 false（不崩溃）
// ============================================================================
TEST_CASE("ToolChainExecutor executeWithFlow empty primary input fails", "[toolchain][flow]") {
    ensureApp();
    ToolChainExecutor executor;
    VisionTool* t = ToolFactory::instance()->createTool(QStringLiteral("EdgeDetect"));
    REQUIRE(t != nullptr);
    executor.setTools({t});

    cv::Mat emptyMat;  // 空矩阵
    REQUIRE_FALSE(executor.executeWithFlow(emptyMat));
    delete t;
}

// ============================================================================
// 用例 4：对抗性 —— 端口绑定构成环时应返回 false（拓扑排序检测到环）
// 结构：a → b，b → a（互相依赖）
// ============================================================================
TEST_CASE("ToolChainExecutor executeWithFlow cycle binding returns false", "[toolchain][flow]") {
    ensureApp();
    ToolChainExecutor executor;
    VisionTool* a = ToolFactory::instance()->createTool(QStringLiteral("EdgeDetect"));
    VisionTool* b = ToolFactory::instance()->createTool(QStringLiteral("Threshold"));
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    executor.setTools({a, b});

    // 环：a 依赖 b，b 依赖 a
    QList<ToolChainExecutor::FlowBinding> bindings;
    bindings.append({a->id(), QStringLiteral("edgeCount"), b->id(), QStringLiteral("in")});
    bindings.append({b->id(), QStringLiteral("whitePixels"), a->id(), QStringLiteral("in")});
    executor.setFlowBindings(bindings);

    cv::Mat testMat(100, 100, CV_8UC3, cv::Scalar(64, 64, 64));
    REQUIRE_FALSE(executor.executeWithFlow(testMat));

    delete a;
    delete b;
}

// ============================================================================
// 用例 5：无绑定时 executeWithFlow 退化为 execute() 语义（返回 true）
// ============================================================================
TEST_CASE("ToolChainExecutor executeWithFlow no binding falls back to execute", "[toolchain][flow]") {
    ensureApp();
    ToolChainExecutor executor;
    VisionTool* t = ToolFactory::instance()->createTool(QStringLiteral("EdgeDetect"));
    REQUIRE(t != nullptr);
    executor.setTools({t});
    // 未设置任何绑定

    cv::Mat testMat(100, 100, CV_8UC3, cv::Scalar(0, 0, 0));
    REQUIRE(executor.executeWithFlow(testMat) == true);
    delete t;
}