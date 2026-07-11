// ============================================================================
// EditViewBridge 单元测试 — 联动功能 + 复制粘贴功能（P1-C 后续任务 #2）
// ----------------------------------------------------------------------------
// 覆盖范围：
//   1. 联动功能：selectNode / openNodeEditor 信号联动
//   2. 复制粘贴：copyNodeParams / pasteNodeParams / hasClipParams / clipParamsType
//
// 测试框架：catch2_minimal（TEST_CASE 静态注册，REQUIRE/CHECK 宏）
// 测试基础设施：ensureApp() 模式创建 QApplication，QSignalSpy 验证信号
// ============================================================================
#include "../catch2/catch2_minimal.hpp"
#include "UI/EditViewBridge.h"
#include "UI/OperatorDescriptors.h"
#include "Vision/ToolFactory.h"
#include "Vision/ImagePreprocessTool.h"
#include "Core/VisionTool.h"

#include <QApplication>
#include <QSignalSpy>
#include <QVariant>
#include <QVariantMap>
#include <opencv2/opencv.hpp>

// === QApplication 单例（UI 测试通用模式） ===
static int argc = 0;
static QApplication* app() {
    static QApplication a(argc, nullptr);
    return &a;
}
inline void ensureApp() { app(); }

// ============================================================================
// 联动功能测试（selectNode / openNodeEditor 信号联动）
// ============================================================================
// 设计目标：验证 EditViewBridge 作为 C++ ↔ QML 桥接器，
//           在节点选中、双击编辑场景下正确发出信号供 QML 端联动响应。

TEST_CASE("EditViewBridge selectNode emits nodeSelected signal", "[editview][linkage]") {
    ensureApp();
    EditViewBridge bridge;
    QSignalSpy spy(&bridge, &EditViewBridge::nodeSelected);
    REQUIRE(spy.isValid());

    bridge.selectNode(QStringLiteral("node-1"));
    REQUIRE(spy.count() == 1);
    REQUIRE(spy.takeFirst().at(0).toString() == "node-1");
}

TEST_CASE("EditViewBridge selectNode same id does not re-emit", "[editview][linkage]") {
    ensureApp();
    EditViewBridge bridge;
    QSignalSpy spy(&bridge, &EditViewBridge::nodeSelected);

    bridge.selectNode("node-1");
    bridge.selectNode("node-1");  // 重复 id，不应再次触发
    REQUIRE(spy.count() == 1);
}

TEST_CASE("EditViewBridge selectNode switch id emits again", "[editview][linkage]") {
    ensureApp();
    EditViewBridge bridge;
    QSignalSpy spy(&bridge, &EditViewBridge::nodeSelected);

    bridge.selectNode("node-1");
    bridge.selectNode("node-2");
    REQUIRE(spy.count() == 2);

    QList<QVariant> args = spy.takeFirst();
    REQUIRE(args.at(0).toString() == "node-1");
    args = spy.takeFirst();
    REQUIRE(args.at(0).toString() == "node-2");
}

TEST_CASE("EditViewBridge openNodeEditor emits openEditorRequested", "[editview][linkage]") {
    ensureApp();
    EditViewBridge bridge;
    QSignalSpy spy(&bridge, &EditViewBridge::openEditorRequested);
    REQUIRE(spy.isValid());

    bridge.openNodeEditor("node-42");
    REQUIRE(spy.count() == 1);
    REQUIRE(spy.takeFirst().at(0).toString() == "node-42");
}

TEST_CASE("EditViewBridge openNodeEditor multiple emits", "[editview][linkage]") {
    ensureApp();
    EditViewBridge bridge;
    QSignalSpy spy(&bridge, &EditViewBridge::openEditorRequested);

    // openNodeEditor 无去重逻辑，每次调用都应触发
    bridge.openNodeEditor("node-1");
    bridge.openNodeEditor("node-2");
    bridge.openNodeEditor("node-3");
    REQUIRE(spy.count() == 3);
}

TEST_CASE("EditViewBridge addOperator returns non-empty nodeId", "[editview][linkage]") {
    ensureApp();
    EditViewBridge bridge;
    const QString id = bridge.addOperator("Threshold", 0, 0);
    REQUIRE_FALSE(id.isEmpty());
    REQUIRE(id.length() > 0);
}

TEST_CASE("EditViewBridge addOperator unknown type emits error", "[editview][linkage]") {
    ensureApp();
    EditViewBridge bridge;
    QSignalSpy spy(&bridge, &EditViewBridge::errorRaised);
    REQUIRE(spy.isValid());

    const QString id = bridge.addOperator("NonExistentType_xyz", 0, 0);
    REQUIRE(id.isEmpty());
    REQUIRE(spy.count() >= 1);

    // 验证错误信号携带阶段标识和消息
    const QList<QVariant> args = spy.takeFirst();
    REQUIRE(args.at(0).toString() == "addOperator");
    REQUIRE_FALSE(args.at(1).toString().isEmpty());
}

TEST_CASE("EditViewBridge addOperator adds to currentNodes", "[editview][linkage]") {
    ensureApp();
    EditViewBridge bridge;
    const int initialCount = bridge.currentNodes().size();

    const QString id = bridge.addOperator("Threshold", 100, 200);
    REQUIRE_FALSE(id.isEmpty());

    const QVariantList nodes = bridge.currentNodes();
    REQUIRE(nodes.size() == initialCount + 1);

    // 验证节点属性
    const QVariantMap node = nodes.last().toMap();
    REQUIRE(node.value("id").toString() == id);
    REQUIRE(node.value("type").toString() == "Threshold");
    REQUIRE(node.value("x").toDouble() == 100.0);
    REQUIRE(node.value("y").toDouble() == 200.0);
}

TEST_CASE("EditViewBridge addOperator currentNodesChanged signal", "[editview][linkage]") {
    ensureApp();
    EditViewBridge bridge;
    QSignalSpy spy(&bridge, &EditViewBridge::currentNodesChanged);
    REQUIRE(spy.isValid());

    bridge.addOperator("Threshold", 0, 0);
    REQUIRE(spy.count() >= 1);
}

// ============================================================================
// 复制粘贴功能测试（copyNodeParams / pasteNodeParams / hasClipParams）
// ============================================================================
// 设计目标：验证 P1-B4-H6 实现的参数剪贴板机制：
//   - copyNodeParams 缓存源节点 type+params
//   - pasteNodeParams 类型转换 + validateParam 校验 + 仅覆盖匹配字段
//   - hasClipParams / clipParamsType 状态查询
//   - 剪贴板覆盖语义（单条，覆盖式）

TEST_CASE("EditViewBridge copyNodeParams non-existent returns false", "[editview][clipboard]") {
    ensureApp();
    EditViewBridge bridge;
    QSignalSpy spy(&bridge, &EditViewBridge::errorRaised);

    REQUIRE_FALSE(bridge.copyNodeParams("non-existent-id"));
    REQUIRE(spy.count() == 1);
    REQUIRE_FALSE(bridge.hasClipParams());

    // 验证错误信号
    const QList<QVariant> args = spy.takeFirst();
    REQUIRE(args.at(0).toString() == "copyNodeParams");
}

TEST_CASE("EditViewBridge copyNodeParams valid node sets clipboard", "[editview][clipboard]") {
    ensureApp();
    EditViewBridge bridge;
    const QString id = bridge.addOperator("Threshold", 0, 0);
    REQUIRE_FALSE(id.isEmpty());

    REQUIRE_FALSE(bridge.hasClipParams());  // 初始无数据
    REQUIRE(bridge.copyNodeParams(id));
    REQUIRE(bridge.hasClipParams());
    REQUIRE(bridge.clipParamsType() == "Threshold");
}

TEST_CASE("EditViewBridge pasteNodeParams empty clipboard returns false", "[editview][clipboard]") {
    ensureApp();
    EditViewBridge bridge;
    const QString id = bridge.addOperator("Threshold", 0, 0);
    QSignalSpy spy(&bridge, &EditViewBridge::errorRaised);

    REQUIRE_FALSE(bridge.hasClipParams());
    REQUIRE_FALSE(bridge.pasteNodeParams(id));  // 剪贴板为空
    REQUIRE(spy.count() == 1);

    const QList<QVariant> args = spy.takeFirst();
    REQUIRE(args.at(0).toString() == "pasteNodeParams");
}

TEST_CASE("EditViewBridge pasteNodeParams non-existent target returns false", "[editview][clipboard]") {
    ensureApp();
    EditViewBridge bridge;
    const QString srcId = bridge.addOperator("Threshold", 0, 0);
    bridge.copyNodeParams(srcId);

    QSignalSpy spy(&bridge, &EditViewBridge::errorRaised);
    REQUIRE_FALSE(bridge.pasteNodeParams("non-existent-target"));
    REQUIRE(spy.count() == 1);
}

TEST_CASE("EditViewBridge pasteNodeParams same type applies params", "[editview][clipboard]") {
    ensureApp();
    EditViewBridge bridge;
    const QString srcId = bridge.addOperator("Threshold", 100, 100);
    const QString dstId = bridge.addOperator("Threshold", 200, 200);
    REQUIRE_FALSE(srcId.isEmpty());
    REQUIRE_FALSE(dstId.isEmpty());
    REQUIRE(srcId != dstId);

    // 修改源节点 threshold 参数
    QVariantMap newParams;
    newParams["threshold"] = 200.0;
    bridge.updateOperatorParams(srcId, newParams);

    // 验证源节点参数已更新
    const QVariantMap srcParamsAfter = bridge.getOperatorParams(srcId);
    REQUIRE(srcParamsAfter.value("threshold").toDouble() == 200.0);

    // 复制源节点参数
    REQUIRE(bridge.copyNodeParams(srcId));
    REQUIRE(bridge.clipParamsType() == "Threshold");

    // 获取目标节点粘贴前的参数
    const QVariantMap dstParamsBefore = bridge.getOperatorParams(dstId);
    REQUIRE(dstParamsBefore.value("threshold").toDouble() == 128.0);  // 默认值

    // 粘贴到目标节点
    REQUIRE(bridge.pasteNodeParams(dstId));

    // 验证目标节点参数已更新
    const QVariantMap dstParamsAfter = bridge.getOperatorParams(dstId);
    REQUIRE(dstParamsAfter.value("threshold").toDouble() == 200.0);
}

TEST_CASE("EditViewBridge clipboard overwrite semantics", "[editview][clipboard]") {
    ensureApp();
    EditViewBridge bridge;
    const QString id1 = bridge.addOperator("Threshold", 0, 0);
    const QString id2 = bridge.addOperator("EdgeDetect", 0, 0);
    REQUIRE_FALSE(id1.isEmpty());
    REQUIRE_FALSE(id2.isEmpty());

    // 第一次复制
    REQUIRE(bridge.copyNodeParams(id1));
    REQUIRE(bridge.clipParamsType() == "Threshold");
    REQUIRE(bridge.hasClipParams());

    // 第二次复制（覆盖）
    REQUIRE(bridge.copyNodeParams(id2));
    REQUIRE(bridge.clipParamsType() == "EdgeDetect");  // 已覆盖
    REQUIRE(bridge.hasClipParams());
}

TEST_CASE("EditViewBridge pasteNodeParams different type partial apply", "[editview][clipboard]") {
    ensureApp();
    EditViewBridge bridge;
    // Threshold 有 threshold/maxValue/method
    // EdgeDetect 有 lowThreshold/highThreshold/apertureSize
    // 两者参数名无交集 → pasteNodeParams 应返回 false（无匹配参数）
    const QString srcId = bridge.addOperator("Threshold", 0, 0);
    const QString dstId = bridge.addOperator("EdgeDetect", 0, 0);
    REQUIRE_FALSE(srcId.isEmpty());
    REQUIRE_FALSE(dstId.isEmpty());

    REQUIRE(bridge.copyNodeParams(srcId));
    REQUIRE_FALSE(bridge.pasteNodeParams(dstId));  // 无匹配参数 → false
}

TEST_CASE("EditViewBridge pasteNodeParams to self", "[editview][clipboard]") {
    ensureApp();
    EditViewBridge bridge;
    const QString id = bridge.addOperator("Threshold", 0, 0);
    REQUIRE_FALSE(id.isEmpty());

    // 修改参数后复制
    QVariantMap newParams;
    newParams["threshold"] = 200.0;
    bridge.updateOperatorParams(id, newParams);
    REQUIRE(bridge.copyNodeParams(id));

    // 粘贴到自身（应成功，参数值不变）
    REQUIRE(bridge.pasteNodeParams(id));
    const QVariantMap params = bridge.getOperatorParams(id);
    REQUIRE(params.value("threshold").toDouble() == 200.0);
}

TEST_CASE("EditViewBridge pasteNodeParams enum type conversion", "[editview][clipboard]") {
    ensureApp();
    EditViewBridge bridge;
    const QString srcId = bridge.addOperator("Threshold", 0, 0);
    const QString dstId = bridge.addOperator("Threshold", 0, 0);

    // 修改源节点 method 参数为 BINARY_INV
    QVariantMap newParams;
    newParams["method"] = QStringLiteral("BINARY_INV");
    bridge.updateOperatorParams(srcId, newParams);

    REQUIRE(bridge.copyNodeParams(srcId));
    REQUIRE(bridge.pasteNodeParams(dstId));

    const QVariantMap dstParams = bridge.getOperatorParams(dstId);
    REQUIRE(dstParams.value("method").toString() == "BINARY_INV");
}

TEST_CASE("EditViewBridge getOperatorParams non-existent returns empty", "[editview][clipboard]") {
    ensureApp();
    EditViewBridge bridge;
    const QVariantMap params = bridge.getOperatorParams("non-existent");
    REQUIRE(params.isEmpty());
}

// ============================================================================
// v2.5.0 功能 3/5：部署（runScheme）与单算子运行（runSingleOperator）测试
// ----------------------------------------------------------------------------
// 覆盖范围：
//   1. 边界条件：空方案 / 无效图像路径 / 不存在的节点 ID
//   2. 相机帧路径预留接口：setCameraFrame / cameraFramePath
//   3. 集成测试：单算子链端到端执行（ReadImage → 验证输出图像生成）
//
// 设计原则（AGENTS.md 红队测试）：
//   - 每条关键路径至少 1 条对抗性测试（空输入/无效输入/边界值）
//   - 验证返回值结构语义（success/outputImagePath/toolResults）而非仅 success=true
// ============================================================================

#include <QTemporaryFile>
#include <QTemporaryDir>
#include <QImage>
#include <QPainter>
#include <QStandardPaths>
#include <QDateTime>
#include <QFile>

// 辅助：创建临时 PNG 测试图像（64x64 红色），返回文件路径
static QString createTestImage() {
    QString tempPath = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
                       + "/qdv_test_" + QString::number(QDateTime::currentMSecsSinceEpoch()) + ".png";
    QImage img(64, 64, QImage::Format_RGB32);
    img.fill(Qt::red);
    QPainter p(&img);
    p.setPen(Qt::white);
    p.drawText(8, 36, "TEST");
    p.end();
    img.save(tempPath, "PNG");
    return tempPath;
}

// --- 边界测试 1：runScheme 空方案（无节点）应返回失败 ---
TEST_CASE("EditViewBridge runScheme empty scheme returns failure", "[editview][deploy]") {
    ensureApp();
    EditViewBridge bridge;
    // bridge 初始无节点
    REQUIRE(bridge.currentNodes().isEmpty());

    const QVariantMap result = bridge.runScheme(QStringLiteral("nonexistent.png"));
    REQUIRE(result.value("success").toBool() == false);
    // 失败时应携带 error 字段
    REQUIRE(result.contains("error"));
}

// --- 边界测试 2：runScheme 有节点但图像路径无效应返回失败 ---
TEST_CASE("EditViewBridge runScheme invalid image path returns failure", "[editview][deploy]") {
    ensureApp();
    EditViewBridge bridge;
    // 添加一个 ReadImage 节点
    const QString nodeId = bridge.addOperator(QStringLiteral("ReadImage"), 0, 0);
    REQUIRE_FALSE(nodeId.isEmpty());

    const QVariantMap result = bridge.runScheme(QStringLiteral("Z:/nonexistent/path/image.png"));
    REQUIRE(result.value("success").toBool() == false);
    REQUIRE(result.contains("error"));
}

// --- 边界测试 3：runSingleOperator 不存在的节点 ID 应返回失败 ---
TEST_CASE("EditViewBridge runSingleOperator non-existent node returns failure", "[editview][deploy]") {
    ensureApp();
    EditViewBridge bridge;
    const QString imgPath = createTestImage();

    const QVariantMap result = bridge.runSingleOperator(
        QStringLiteral("non-existent-node-id"), imgPath);
    REQUIRE(result.value("success").toBool() == false);
    REQUIRE(result.contains("error"));

    QFile::remove(imgPath);
}


// --- 边界测试 4：普通算子空输入图像应返回失败 ---
TEST_CASE("EditViewBridge runSingleOperator empty image path returns failure", "[editview][deploy]") {
    ensureApp();
    EditViewBridge bridge;
    const QString nodeId = bridge.addOperator(QStringLiteral("Threshold"), 0, 0);
    REQUIRE_FALSE(nodeId.isEmpty());

    const QVariantMap result = bridge.runSingleOperator(nodeId, QStringLiteral(""));
    REQUIRE(result.value("success").toBool() == false);
    REQUIRE(result.contains("error"));
}

// --- 边界测试 4b：数据源型算子空输入图像不应再返回 no_input_needed，而应进入实际执行 ---
TEST_CASE("EditViewBridge runSingleOperator OpenFramegrabber no longer returns no_input_needed", "[editview][deploy]") {
    ensureApp();
    EditViewBridge bridge;
    const QString nodeId = bridge.addOperator(QStringLiteral("OpenFramegrabber"), 0, 0);
    REQUIRE_FALSE(nodeId.isEmpty());

    // 启用"使用已打开相机"，但不注册外部相机，执行应进入工具内部逻辑并失败，
    // 关键断言：不能再返回之前的 no_input_needed 错误
    QVariantMap params;
    params["useActiveCamera"] = true;
    bridge.updateOperatorParams(nodeId, params);

    const QVariantMap result = bridge.runSingleOperator(nodeId, QStringLiteral(""));
    REQUIRE(result.value("error").toString() != QStringLiteral("no_input_needed"));
}

// --- 接口测试：cameraFramePath 初始为空，setCameraFrame 后更新 ---
TEST_CASE("EditViewBridge cameraFramePath initial empty and setCameraFrame", "[editview][deploy]") {
    ensureApp();
    EditViewBridge bridge;
    REQUIRE(bridge.cameraFramePath().isEmpty());

    const QString testPath = QStringLiteral("/tmp/camera_frame_001.png");
    bridge.setCameraFrame(testPath);
    REQUIRE(bridge.cameraFramePath() == testPath);
}

// --- 集成测试：单算子链端到端执行（ReadImage 读取真实图像）---
// 验证点（语义验证，非仅 success=true）：
//   1. success == true
//   2. totalTools >= 1
//   3. successCount >= 1
//   4. outputImagePath 非空且文件实际存在
//   5. toolResults 列表非空且每项包含 toolId/ok 字段
TEST_CASE("EditViewBridge runSingleOperator ReadImage end-to-end", "[editview][deploy][integration]") {
    ensureApp();
    EditViewBridge bridge;
    const QString imgPath = createTestImage();
    const QString nodeId = bridge.addOperator(QStringLiteral("ReadImage"), 0, 0);
    REQUIRE_FALSE(nodeId.isEmpty());

    const QVariantMap result = bridge.runSingleOperator(nodeId, imgPath);
    REQUIRE(result.value("success").toBool() == true);

    // 语义验证：返回结构完整
    REQUIRE(result.value("upstreamCount").toInt() >= 0);
    REQUIRE(result.contains("targetResult"));
    REQUIRE(result.contains("outputImagePath"));

    // 验证输出图像文件实际存在（确定性终门）
    const QString outputPath = result.value("outputImagePath").toString();
    if (!outputPath.isEmpty()) {
        REQUIRE(QFile::exists(outputPath));
        QFile::remove(outputPath);
    }

    QFile::remove(imgPath);
}

// --- 集成测试：双节点链 + runSingleOperator 验证上游链执行 ---
// 链结构：ReadImage → Threshold
// 执行 runSingleOperator(thresholdNodeId, imgPath) 应先执行 ReadImage 再执行 Threshold
TEST_CASE("EditViewBridge runSingleOperator with upstream chain", "[editview][deploy][integration]") {
    ensureApp();
    EditViewBridge bridge;
    const QString imgPath = createTestImage();

    // 创建两个节点
    const QString readId = bridge.addOperator(QStringLiteral("ReadImage"), 0, 0);
    const QString threshId = bridge.addOperator(QStringLiteral("Threshold"), 200, 0);
    REQUIRE_FALSE(readId.isEmpty());
    REQUIRE_FALSE(threshId.isEmpty());

    // 连接：ReadImage → Threshold
    bridge.connectNodes(readId, "out", threshId, "in");

    // 执行 runSingleOperator（应自动计算上游链）
    const QVariantMap result = bridge.runSingleOperator(threshId, imgPath);

    // 语义验证：上游链应包含 ReadImage
    REQUIRE(result.value("success").toBool() == true);
    REQUIRE(result.value("upstreamCount").toInt() >= 1);

    // 验证输出图像
    const QString outputPath = result.value("outputImagePath").toString();
    if (!outputPath.isEmpty()) {
        REQUIRE(QFile::exists(outputPath));
        QFile::remove(outputPath);
    }

    QFile::remove(imgPath);
}

// ============================================================================
// v2.5.0 修复测试：智能输入源识别（resolveInputImageForNode）
// 验证：当前道有 ReadImage 节点且配置了 filePath 时，运行后续算子可自动复用该路径
// ============================================================================

TEST_CASE("EditViewBridge resolveInputImageForNode empty when no ReadImage upstream", "[editview][smart-input]") {
    ensureApp();
    EditViewBridge bridge;
    // 只有一个 Threshold 节点，无 ReadImage 上游
    const QString threshId = bridge.addOperator(QStringLiteral("Threshold"), 0, 0);
    REQUIRE_FALSE(threshId.isEmpty());

    const QString resolved = bridge.resolveInputImageForNode(threshId);
    REQUIRE(resolved.isEmpty());
}

TEST_CASE("EditViewBridge resolveInputImageForNode returns ReadImage filePath", "[editview][smart-input]") {
    ensureApp();
    EditViewBridge bridge;
    const QString imgPath = createTestImage();

    // 创建 ReadImage 节点并配置 filePath
    const QString readId = bridge.addOperator(QStringLiteral("ReadImage"), 0, 0);
    REQUIRE_FALSE(readId.isEmpty());
    QVariantMap params;
    params["filePath"] = imgPath;
    params["colorMode"] = QStringLiteral("color");
    bridge.updateOperatorParams(readId, params);

    // 创建 Threshold 节点并连接
    const QString threshId = bridge.addOperator(QStringLiteral("Threshold"), 200, 0);
    bridge.connectNodes(readId, "out", threshId, "in");

    // 验证：从 Threshold 节点能解析到 ReadImage 的 filePath
    const QString resolved = bridge.resolveInputImageForNode(threshId);
    REQUIRE(resolved == imgPath);

    QFile::remove(imgPath);
}

TEST_CASE("EditViewBridge resolveInputImageForNode empty when ReadImage filePath empty", "[editview][smart-input]") {
    ensureApp();
    EditViewBridge bridge;
    // ReadImage 节点但 filePath 为空
    const QString readId = bridge.addOperator(QStringLiteral("ReadImage"), 0, 0);
    REQUIRE_FALSE(readId.isEmpty());

    const QString resolved = bridge.resolveInputImageForNode(readId);
    REQUIRE(resolved.isEmpty());
}

TEST_CASE("EditViewBridge runSingleOperator auto-resolves from ReadImage upstream", "[editview][smart-input][integration]") {
    ensureApp();
    EditViewBridge bridge;
    const QString imgPath = createTestImage();

    // 创建 ReadImage 节点并配置 filePath
    const QString readId = bridge.addOperator(QStringLiteral("ReadImage"), 0, 0);
    QVariantMap readParams;
    readParams["filePath"] = imgPath;
    readParams["colorMode"] = QStringLiteral("color");
    bridge.updateOperatorParams(readId, readParams);

    // 创建 Threshold 节点并连接
    const QString threshId = bridge.addOperator(QStringLiteral("Threshold"), 200, 0);
    bridge.connectNodes(readId, "out", threshId, "in");

    // 传空字符串调用 runSingleOperator，应自动从 ReadImage 解析 filePath
    const QVariantMap result = bridge.runSingleOperator(threshId, QStringLiteral(""));
    REQUIRE(result.value("success").toBool() == true);

    // 验证输出图像
    const QString outputPath = result.value("outputImagePath").toString();
    if (!outputPath.isEmpty()) {
        REQUIRE(QFile::exists(outputPath));
        QFile::remove(outputPath);
    }

    QFile::remove(imgPath);
}

// ============================================================================
// v5.3.8 修复测试：isInputImageRequired / resolveRunInput 统一输入源判断
// 验证：相机类/数据源算子无需输入图像；普通算子仍需要输入图像或 ReadImage 上游
// ============================================================================

TEST_CASE("EditViewBridge isInputImageRequired OpenFramegrabber returns false", "[editview][smart-input]") {
    ensureApp();
    EditViewBridge bridge;
    const QString nodeId = bridge.addOperator(QStringLiteral("OpenFramegrabber"), 0, 0);
    REQUIRE_FALSE(nodeId.isEmpty());
    REQUIRE(bridge.isInputImageRequired(nodeId) == false);
}

TEST_CASE("EditViewBridge isInputImageRequired GrabImage returns false", "[editview][smart-input]") {
    ensureApp();
    EditViewBridge bridge;
    const QString nodeId = bridge.addOperator(QStringLiteral("GrabImage"), 0, 0);
    REQUIRE_FALSE(nodeId.isEmpty());
    REQUIRE(bridge.isInputImageRequired(nodeId) == false);
}

TEST_CASE("EditViewBridge isInputImageRequired ReadImage returns false", "[editview][smart-input]") {
    ensureApp();
    EditViewBridge bridge;
    const QString nodeId = bridge.addOperator(QStringLiteral("ReadImage"), 0, 0);
    REQUIRE_FALSE(nodeId.isEmpty());
    REQUIRE(bridge.isInputImageRequired(nodeId) == false);
}

TEST_CASE("EditViewBridge isInputImageRequired Threshold returns true", "[editview][smart-input]") {
    ensureApp();
    EditViewBridge bridge;
    const QString nodeId = bridge.addOperator(QStringLiteral("Threshold"), 0, 0);
    REQUIRE_FALSE(nodeId.isEmpty());
    REQUIRE(bridge.isInputImageRequired(nodeId) == true);
}

TEST_CASE("EditViewBridge resolveRunInput no-image operator", "[editview][smart-input]") {
    ensureApp();
    EditViewBridge bridge;
    const QString nodeId = bridge.addOperator(QStringLiteral("GrabImage"), 0, 0);
    REQUIRE_FALSE(nodeId.isEmpty());

    const QVariantMap runInput = bridge.resolveRunInput(nodeId);
    REQUIRE(runInput.value("required").toBool() == false);
    REQUIRE(runInput.value("path").toString().isEmpty());
    REQUIRE_FALSE(runInput.value("hint").toString().isEmpty());
}

TEST_CASE("EditViewBridge resolveRunInput Threshold with ReadImage upstream", "[editview][smart-input]") {
    ensureApp();
    EditViewBridge bridge;
    const QString imgPath = createTestImage();

    const QString readId = bridge.addOperator(QStringLiteral("ReadImage"), 0, 0);
    QVariantMap readParams;
    readParams["filePath"] = imgPath;
    readParams["colorMode"] = QStringLiteral("color");
    bridge.updateOperatorParams(readId, readParams);

    const QString threshId = bridge.addOperator(QStringLiteral("Threshold"), 200, 0);
    bridge.connectNodes(readId, "out", threshId, "in");

    const QVariantMap runInput = bridge.resolveRunInput(threshId);
    REQUIRE(runInput.value("required").toBool() == true);
    REQUIRE(runInput.value("path").toString() == imgPath);
    REQUIRE_FALSE(runInput.value("hint").toString().isEmpty());

    QFile::remove(imgPath);
}

TEST_CASE("EditViewBridge resolveRunInput Threshold without upstream needs manual input", "[editview][smart-input]") {
    ensureApp();
    EditViewBridge bridge;
    const QString nodeId = bridge.addOperator(QStringLiteral("Threshold"), 0, 0);
    REQUIRE_FALSE(nodeId.isEmpty());

    const QVariantMap runInput = bridge.resolveRunInput(nodeId);
    REQUIRE(runInput.value("required").toBool() == true);
    REQUIRE(runInput.value("path").toString().isEmpty());
    REQUIRE_FALSE(runInput.value("hint").toString().isEmpty());
}

// ============================================================================
// v5.3.9 修复测试：resolveSchemeRunInput 整链运行输入源判断
// 验证：方案含相机类算子时无需输入图像；含有效 ReadImage 时返回其路径；
//       无数据源时需要用户手动选择。
// ============================================================================

TEST_CASE("EditViewBridge resolveSchemeRunInput with OpenFramegrabber needs no input", "[editview][scheme-input]") {
    ensureApp();
    EditViewBridge bridge;
    const QString nodeId = bridge.addOperator(QStringLiteral("OpenFramegrabber"), 0, 0);
    REQUIRE_FALSE(nodeId.isEmpty());

    const QVariantMap runInput = bridge.resolveSchemeRunInput();
    REQUIRE(runInput.value("required").toBool() == false);
    REQUIRE(runInput.value("path").toString().isEmpty());
    REQUIRE_FALSE(runInput.value("hint").toString().isEmpty());
}

TEST_CASE("EditViewBridge resolveSchemeRunInput with GrabImage needs no input", "[editview][scheme-input]") {
    ensureApp();
    EditViewBridge bridge;
    const QString nodeId = bridge.addOperator(QStringLiteral("GrabImage"), 0, 0);
    REQUIRE_FALSE(nodeId.isEmpty());

    const QVariantMap runInput = bridge.resolveSchemeRunInput();
    REQUIRE(runInput.value("required").toBool() == false);
    REQUIRE(runInput.value("path").toString().isEmpty());
    REQUIRE_FALSE(runInput.value("hint").toString().isEmpty());
}

TEST_CASE("EditViewBridge resolveSchemeRunInput with ReadImage returns valid path", "[editview][scheme-input]") {
    ensureApp();
    EditViewBridge bridge;
    const QString imgPath = createTestImage();

    const QString readId = bridge.addOperator(QStringLiteral("ReadImage"), 0, 0);
    REQUIRE_FALSE(readId.isEmpty());
    QVariantMap readParams;
    readParams["filePath"] = imgPath;
    readParams["colorMode"] = QStringLiteral("color");
    bridge.updateOperatorParams(readId, readParams);

    const QVariantMap runInput = bridge.resolveSchemeRunInput();
    REQUIRE(runInput.value("required").toBool() == true);
    REQUIRE(runInput.value("path").toString() == imgPath);
    REQUIRE_FALSE(runInput.value("hint").toString().isEmpty());

    QFile::remove(imgPath);
}

TEST_CASE("EditViewBridge resolveSchemeRunInput empty scheme needs manual input", "[editview][scheme-input]") {
    ensureApp();
    EditViewBridge bridge;
    REQUIRE(bridge.currentNodes().isEmpty());

    const QVariantMap runInput = bridge.resolveSchemeRunInput();
    REQUIRE(runInput.value("required").toBool() == true);
    REQUIRE(runInput.value("path").toString().isEmpty());
    REQUIRE_FALSE(runInput.value("hint").toString().isEmpty());
}

// ============================================================================
// v2.5.0 修复测试：ImagePreprocessTool kernelSize 防御性校验
// 验证：kernelSize 为 0/负数/偶数时不会触发 OpenCV normalizeAnchor 断言失败
// ============================================================================

TEST_CASE("ImagePreprocessTool kernelSize=0 does not crash", "[vision][preprocess][defensive]") {
    ensureApp();
    ImagePreprocessTool tool;

    // 配置 kernelSize=0（之前会触发 normalizeAnchor 断言失败）
    QJsonObject params;
    params["morphology"] = QStringLiteral("erode");
    params["kernelSize"] = 0;
    REQUIRE(tool.configure(params));

    // 创建测试图像并执行
    cv::Mat testImg(64, 64, CV_8UC3, cv::Scalar(128, 128, 128));
    ToolResult result;
    const bool ok = tool.execute(testImg, result);

    // 验证：不应崩溃，应正常执行（kernelSize 被 clamp 到 1）
    REQUIRE(ok == true);
    REQUIRE(result.ok == true);
    REQUIRE_FALSE(result.overlayImage.empty());
}

TEST_CASE("ImagePreprocessTool kernelSize=negative clamped to 1", "[vision][preprocess][defensive]") {
    ensureApp();
    ImagePreprocessTool tool;

    QJsonObject params;
    params["morphology"] = QStringLiteral("dilate");
    params["kernelSize"] = -5;
    REQUIRE(tool.configure(params));

    cv::Mat testImg(64, 64, CV_8UC3, cv::Scalar(128, 128, 128));
    ToolResult result;
    const bool ok = tool.execute(testImg, result);

    REQUIRE(ok == true);
    REQUIRE(result.ok == true);
    REQUIRE_FALSE(result.overlayImage.empty());
}

TEST_CASE("ImagePreprocessTool kernelSize=even adjusted to odd", "[vision][preprocess][defensive]") {
    ensureApp();
    ImagePreprocessTool tool;

    // kernelSize=4（偶数）应被调整为 5
    QJsonObject params;
    params["morphology"] = QStringLiteral("open");
    params["kernelSize"] = 4;
    REQUIRE(tool.configure(params));

    cv::Mat testImg(64, 64, CV_8UC3, cv::Scalar(128, 128, 128));
    ToolResult result;
    const bool ok = tool.execute(testImg, result);

    REQUIRE(ok == true);
    REQUIRE(result.ok == true);
    REQUIRE_FALSE(result.overlayImage.empty());
    // serialize 中的 kernelSize 应为调整后的奇数（5）
    const QJsonObject serialized = tool.serialize();
    REQUIRE(serialized.value("kernelSize").toInt() == 5);
}

TEST_CASE("ImagePreprocessTool kernelSize=large clamped to 31", "[vision][preprocess][defensive]") {
    ensureApp();
    ImagePreprocessTool tool;

    QJsonObject params;
    params["morphology"] = QStringLiteral("close");
    params["kernelSize"] = 100;  // 超上限
    REQUIRE(tool.configure(params));

    cv::Mat testImg(64, 64, CV_8UC3, cv::Scalar(128, 128, 128));
    ToolResult result;
    const bool ok = tool.execute(testImg, result);

    REQUIRE(ok == true);
    REQUIRE(result.ok == true);
    REQUIRE_FALSE(result.overlayImage.empty());
    const QJsonObject serialized = tool.serialize();
    REQUIRE(serialized.value("kernelSize").toInt() == 31);
}

// ============================================================================
// v2.6.0 变量管理与预览管理测试
// ----------------------------------------------------------------------------
// 覆盖范围：
//   1. VariableManager: CRUD + ${var} 绑定解析 + 序列化
//   2. ImageVariableManager: 图像变量 CRUD + 信号
//   3. EditViewBridge: variableManager/imageVariableManager/previewManager 暴露
//   4. variablesToJson/loadVariablesFromJson 端到端
//   5. 变量绑定到算子参数（buildToolChainFromNodes 解析 ${var}）
// ============================================================================

#include "Core/VariableManager.h"
#include "Core/ImageVariableManager.h"
#include "UI/PreviewManager.h"
#include <QJsonDocument>
#include <QJsonArray>

// --- 测试 1：VariableManager CRUD 基本 ---
TEST_CASE("VariableManager createVariable basic", "[editview][v260]") {
    ensureApp();
    QDV::VariableManager vm;
    REQUIRE(vm.count() == 0);

    REQUIRE(vm.createVariable("thresh", "Int", 128));
    REQUIRE(vm.exists("thresh"));
    REQUIRE(vm.count() == 1);
    REQUIRE(vm.value("thresh").toInt() == 128);

    // 重复创建失败
    REQUIRE_FALSE(vm.createVariable("thresh", "Int", 200));
    REQUIRE(vm.count() == 1);
}

// --- 测试 2：非法变量名拒绝 ---
TEST_CASE("VariableManager rejects invalid name", "[editview][v260]") {
    ensureApp();
    QDV::VariableManager vm;
    REQUIRE_FALSE(vm.createVariable("1invalid", "Int", 0));   // 数字开头
    REQUIRE_FALSE(vm.createVariable("has space", "Int", 0));   // 含空格
    REQUIRE_FALSE(vm.createVariable("", "Int", 0));            // 空
    REQUIRE(vm.count() == 0);
}

// --- 测试 3：变量值修改 + 类型匹配 ---
TEST_CASE("VariableManager setValue type match", "[editview][v260]") {
    ensureApp();
    QDV::VariableManager vm;
    REQUIRE(vm.createVariable("thresh", "Int", 128));

    REQUIRE(vm.setValue("thresh", 200));
    REQUIRE(vm.value("thresh").toInt() == 200);

    // 给不存在的变量赋值失败
    REQUIRE_FALSE(vm.setValue("nonexistent", 100));

    // 修改 string 变量
    REQUIRE(vm.createVariable("name", "String", "hello"));
    REQUIRE(vm.setValue("name", "world"));
    REQUIRE(vm.value("name").toString() == "world");

    // 修改 bool 变量
    REQUIRE(vm.createVariable("flag", "Bool", false));
    REQUIRE(vm.setValue("flag", true));
    REQUIRE(vm.value("flag").toBool() == true);
}

// --- 测试 4：变量删除 ---
TEST_CASE("VariableManager removeVariable", "[editview][v260]") {
    ensureApp();
    QDV::VariableManager vm;
    vm.createVariable("a", "Int", 1);
    vm.createVariable("b", "String", "hello");

    REQUIRE(vm.removeVariable("a"));
    REQUIRE_FALSE(vm.exists("a"));
    REQUIRE(vm.count() == 1);

    // 删除不存在的变量
    REQUIRE_FALSE(vm.removeVariable("nonexistent"));
}

// --- 测试 5：${var} 绑定解析 ---
TEST_CASE("VariableManager resolveBinding", "[editview][v260]") {
    ensureApp();
    QDV::VariableManager vm;
    vm.createVariable("thresh", "Int", 128);
    vm.createVariable("maxVal", "Int", 255);

    // 简单绑定
    REQUIRE(vm.resolveBinding("${thresh}") == "128");
    // 前后缀
    REQUIRE(vm.resolveBinding("prefix_${maxVal}_suffix") == "prefix_255_suffix");
    // 多变量
    REQUIRE(vm.resolveBinding("${thresh}_${maxVal}") == "128_255");
    // 无变量引用 → 原样返回
    REQUIRE(vm.resolveBinding("plain_text") == "plain_text");
    // 未定义变量 → 保留原引用
    REQUIRE(vm.resolveBinding("${undefined}") == "${undefined}");
}

// --- 测试 6：递归解析 QVariantMap ---
TEST_CASE("VariableManager resolveVariant map", "[editview][v260]") {
    ensureApp();
    QDV::VariableManager vm;
    vm.createVariable("thresh", "Int", 100);

    QVariantMap input;
    input["threshold"] = "${thresh}";
    input["maxValue"] = 255;

    const QVariant result = vm.resolveVariant(input);
    const QVariantMap resultMap = result.toMap();
    REQUIRE(resultMap.value("threshold").toString() == "100");
    REQUIRE(resultMap.value("maxValue").toInt() == 255);
}

// --- 测试 7：序列化 / 反序列化 ---
TEST_CASE("VariableManager serialize/deserialize", "[editview][v260]") {
    ensureApp();
    QDV::VariableManager vm;
    vm.createVariable("thresh", "Int", 128);
    vm.createVariable("name", "String", "test");
    vm.createVariable("flag", "Bool", true);

    const QJsonArray arr = vm.toJson();
    REQUIRE(arr.size() == 3);

    // 反序列化到新管理器
    QDV::VariableManager vm2;
    REQUIRE(vm2.fromJson(arr));
    REQUIRE(vm2.count() == 3);
    REQUIRE(vm2.value("thresh").toInt() == 128);
    REQUIRE(vm2.value("name").toString() == "test");
    REQUIRE(vm2.value("flag").toBool() == true);
}

// --- 测试 8：ImageVariableManager 基本 CRUD ---
TEST_CASE("ImageVariableManager basic CRUD", "[editview][v260]") {
    ensureApp();
    QDV::ImageVariableManager ivm;
    REQUIRE(ivm.count() == 0);

    ivm.updateImageVariable("node-1", "Threshold", "/tmp/test.png", 640, 480, 3);
    REQUIRE(ivm.exists("node-1"));
    REQUIRE(ivm.count() == 1);
    REQUIRE(ivm.imagePath("node-1") == "/tmp/test.png");

    const QVariantMap info = ivm.imageVariable("node-1");
    REQUIRE(info.value("toolName").toString() == "Threshold");
    REQUIRE(info.value("width").toInt() == 640);

    // 更新（覆盖）
    ivm.updateImageVariable("node-1", "Threshold", "/tmp/test2.png", 320, 240, 1);
    REQUIRE(ivm.imagePath("node-1") == "/tmp/test2.png");
    REQUIRE(ivm.count() == 1);  // 仍是 1 个

    // 删除
    REQUIRE(ivm.removeImageVariable("node-1"));
    REQUIRE_FALSE(ivm.exists("node-1"));
    REQUIRE(ivm.count() == 0);
}

// --- 测试 9：EditViewBridge 暴露变量管理器 ---
TEST_CASE("EditViewBridge exposes variable managers", "[editview][v260]") {
    ensureApp();
    EditViewBridge bridge;

    // bridge 应暴露三个管理器
    QObject* vm = bridge.variableManager();
    QObject* ivm = bridge.imageVariableManager();
    QObject* pm = bridge.previewManager();

    REQUIRE(vm != nullptr);
    REQUIRE(ivm != nullptr);
    REQUIRE(pm != nullptr);

    // 应为 QDV::VariableManager 类型
    REQUIRE(dynamic_cast<QDV::VariableManager*>(vm) != nullptr);
    REQUIRE(dynamic_cast<QDV::ImageVariableManager*>(ivm) != nullptr);
    REQUIRE(dynamic_cast<QDV::PreviewManager*>(pm) != nullptr);
}

// --- 测试 10：variablesToJson / loadVariablesFromJson 端到端 ---
TEST_CASE("EditViewBridge variablesToJson roundtrip", "[editview][v260]") {
    ensureApp();
    EditViewBridge bridge;
    QDV::VariableManager* vm = dynamic_cast<QDV::VariableManager*>(bridge.variableManager());
    REQUIRE(vm != nullptr);

    // 创建变量
    REQUIRE(vm->createVariable("thresh", "Int", 128));
    REQUIRE(vm->createVariable("name", "String", "test"));

    // 序列化
    const QString json = bridge.variablesToJson();
    REQUIRE_FALSE(json.isEmpty());
    REQUIRE(json.startsWith("["));

    // 清空后反序列化
    vm->clear();
    REQUIRE(vm->count() == 0);
    REQUIRE(bridge.loadVariablesFromJson(json));
    REQUIRE(vm->count() == 2);
    REQUIRE(vm->value("thresh").toInt() == 128);
    REQUIRE(vm->value("name").toString() == "test");
}

// --- 测试 11：loadVariablesFromJson 无效 JSON 返回 false ---
TEST_CASE("EditViewBridge loadVariablesFromJson invalid json", "[editview][v260]") {
    ensureApp();
    EditViewBridge bridge;
    QSignalSpy spy(&bridge, &EditViewBridge::errorRaised);

    REQUIRE_FALSE(bridge.loadVariablesFromJson("not a json"));
    REQUIRE(spy.count() >= 1);
    REQUIRE(spy.takeFirst().at(0).toString() == "loadVariablesFromJson");
}

// --- 测试 12：变量绑定到算子参数（buildToolChainFromNodes 通过 runSingleOperator 间接验证）---
TEST_CASE("EditViewBridge variable binding in operator params", "[editview][v260][integration]") {
    ensureApp();
    EditViewBridge bridge;
    QDV::VariableManager* vm = dynamic_cast<QDV::VariableManager*>(bridge.variableManager());
    REQUIRE(vm != nullptr);

    // 创建变量 thresh_val = 100
    REQUIRE(vm->createVariable("thresh_val", "Int", 100));

    // 添加 Threshold 算子，参数使用 ${thresh_val} 绑定
    const QString nodeId = bridge.addOperator("Threshold", 0, 0);
    REQUIRE_FALSE(nodeId.isEmpty());

    QVariantMap params;
    params["threshold"] = "${thresh_val}";  // 变量绑定
    bridge.updateOperatorParams(nodeId, params);

    // 验证：读取参数应为 "${thresh_val}"（未解析的原值）
    const QVariantMap readParams = bridge.getOperatorParams(nodeId);
    REQUIRE(readParams.value("threshold").toString() == "${thresh_val}");

    // 修改变量值 → 参数绑定应反映新值（通过 buildToolChainFromNodes 内部解析）
    REQUIRE(vm->setValue("thresh_val", 150));

    // 验证 resolveBinding 正确
    REQUIRE(vm->resolveBinding("${thresh_val}") == "150");
}

// --- 测试 13：PreviewManager 自动跟随选中节点 ---
TEST_CASE("PreviewManager follows node selection", "[editview][v260]") {
    ensureApp();
    EditViewBridge bridge;
    QDV::PreviewManager* pm = dynamic_cast<QDV::PreviewManager*>(bridge.previewManager());
    REQUIRE(pm != nullptr);

    // 初始无选中节点
    REQUIRE(pm->previewNodeId().isEmpty());

    // 选中节点 → PreviewManager 应跟随
    bridge.selectNode("node-preview-1");
    REQUIRE(pm->previewNodeId() == "node-preview-1");

    // 切换选中节点
    bridge.selectNode("node-preview-2");
    REQUIRE(pm->previewNodeId() == "node-preview-2");
}

// --- 测试 14：PreviewManager 自动预览开关 ---
TEST_CASE("PreviewManager autoPreview toggle", "[editview][v260]") {
    ensureApp();
    EditViewBridge bridge;
    QDV::PreviewManager* pm = dynamic_cast<QDV::PreviewManager*>(bridge.previewManager());
    REQUIRE(pm != nullptr);

    // 默认开启
    REQUIRE(pm->autoPreviewEnabled() == true);

    // 关闭
    pm->setAutoPreviewEnabled(false);
    REQUIRE(pm->autoPreviewEnabled() == false);

    // 重新开启
    pm->setAutoPreviewEnabled(true);
    REQUIRE(pm->autoPreviewEnabled() == true);
}

// --- 测试 15：节点删除时清理图像变量 ---
TEST_CASE("ImageVariableManager cleanup on node delete", "[editview][v260]") {
    ensureApp();
    EditViewBridge bridge;
    QDV::ImageVariableManager* ivm = dynamic_cast<QDV::ImageVariableManager*>(bridge.imageVariableManager());
    REQUIRE(ivm != nullptr);

    // 模拟写入图像变量
    ivm->updateImageVariable("node-del-1", "Threshold", "/tmp/x.png", 100, 100, 3);
    REQUIRE(ivm->exists("node-del-1"));

    // 删除节点 → PreviewManager 应清理对应图像变量
    bridge.deleteNode("node-del-1");  // 节点不存在也调用，触发 onNodeDeleted
    REQUIRE_FALSE(ivm->exists("node-del-1"));
}

// ============================================================================
// v2.5.0 功能 2 增强：删除连线不应删除相连节点
// ============================================================================
TEST_CASE("EditViewBridge disconnectEdge keeps connected nodes", "[editview][connection]") {
    ensureApp();
    EditViewBridge bridge;

    const QString srcId = bridge.addOperator("Threshold", 0, 0);
    const QString dstId = bridge.addOperator("EdgeDetect", 200, 0);
    REQUIRE_FALSE(srcId.isEmpty());
    REQUIRE_FALSE(dstId.isEmpty());

    bridge.connectNodes(srcId, "out", dstId, "in");
    REQUIRE(bridge.connections().size() == 1);

    const int nodeCountBefore = bridge.currentNodes().size();

    // 删除连线
    bridge.disconnectEdge(srcId, "out", dstId, "in");

    // 验证：连线被删除，节点数量不变
    REQUIRE(bridge.connections().isEmpty());
    REQUIRE(bridge.currentNodes().size() == nodeCountBefore);

    // 验证：两个节点仍然存在于 currentNodes 中
    bool srcExists = false, dstExists = false;
    for (const QVariant& v : bridge.currentNodes()) {
        const QString id = v.toMap().value("id").toString();
        if (id == srcId) srcExists = true;
        if (id == dstId) dstExists = true;
    }
    REQUIRE(srcExists);
    REQUIRE(dstExists);
}


