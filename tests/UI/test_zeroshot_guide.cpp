// ============================================================================
// ZeroShotGuide 单元测试（零样本检测"新手引导"模块）
// ----------------------------------------------------------------------------
// 覆盖范围：
//   1. isGuideStepDone：四步骤完成判定（步骤①恒完成）
//   2. nextPendingStep：下一个待完成步骤的推导
//   3. buildGuideHintHtml：引导 HTML 生成（✔/▶/○ 标记与状态色）
//   4. buildEmptyStateHint：空状态引导文本非空且含步骤指引
//   5. missingPrerequisiteHint：缺模型/缺图像/双缺/就绪 四种组合
//   6. modelTypePlainHelp：四种模型 + 未实现类型 + 未知类型
//   7. termPlainHelp：常用术语通俗解释 + 未收录术语返回空
//   8. guideStepText / guideStepDetail：步骤文案
//
// 测试策略：
//   - 纯逻辑测试：不实例化任何 QWidget，无需 QApplication
//   - 直接验证函数返回值，与 UI 显示解耦，保证引导逻辑可回归
// ============================================================================
#include "../catch2/catch2_minimal.hpp"
#include "UI/ZeroShotGuide.h"

#include <QString>
#include <QStringList>

using namespace QDVMini;

// ============================================================================
// 1. 步骤完成判定
// ============================================================================
TEST_CASE("ZeroShotGuide: 初始状态仅步骤①完成", "[zeroshot][guide]") {
    ZeroShotGuideState s;  // modelLoaded/imageLoaded/hasResult 均为 false
    REQUIRE(isGuideStepDone(ZeroShotGuideStep::SelectModelType, s));
    REQUIRE_FALSE(isGuideStepDone(ZeroShotGuideStep::LoadModel, s));
    REQUIRE_FALSE(isGuideStepDone(ZeroShotGuideStep::LoadImage, s));
    REQUIRE_FALSE(isGuideStepDone(ZeroShotGuideStep::RunInference, s));
}

TEST_CASE("ZeroShotGuide: 模型加载后步骤②完成", "[zeroshot][guide]") {
    ZeroShotGuideState s;
    s.modelLoaded = true;
    REQUIRE(isGuideStepDone(ZeroShotGuideStep::LoadModel, s));
    REQUIRE_FALSE(isGuideStepDone(ZeroShotGuideStep::LoadImage, s));
}

TEST_CASE("ZeroShotGuide: 图像加载后步骤③完成", "[zeroshot][guide]") {
    ZeroShotGuideState s;
    s.imageLoaded = true;
    REQUIRE(isGuideStepDone(ZeroShotGuideStep::LoadImage, s));
    REQUIRE_FALSE(isGuideStepDone(ZeroShotGuideStep::RunInference, s));
}

TEST_CASE("ZeroShotGuide: 有结果后四步全部完成", "[zeroshot][guide]") {
    ZeroShotGuideState s;
    s.modelLoaded = true;
    s.imageLoaded = true;
    s.hasResult = true;
    REQUIRE(isGuideStepDone(ZeroShotGuideStep::SelectModelType, s));
    REQUIRE(isGuideStepDone(ZeroShotGuideStep::LoadModel, s));
    REQUIRE(isGuideStepDone(ZeroShotGuideStep::LoadImage, s));
    REQUIRE(isGuideStepDone(ZeroShotGuideStep::RunInference, s));
}

// ============================================================================
// 2. 下一个待完成步骤
// ============================================================================
TEST_CASE("ZeroShotGuide: nextPendingStep 推导", "[zeroshot][guide]") {
    ZeroShotGuideState s;
    // 初始：下一步为② 加载模型
    REQUIRE_EQUAL(nextPendingStep(s), 2);

    s.modelLoaded = true;
    REQUIRE_EQUAL(nextPendingStep(s), 3);   // 下一步为③ 加载图像

    s.imageLoaded = true;
    REQUIRE_EQUAL(nextPendingStep(s), 4);   // 下一步为④ 开始推理

    s.hasResult = true;
    REQUIRE_EQUAL(nextPendingStep(s), 0);   // 全部完成
}

// ============================================================================
// 3. 引导 HTML 生成
// ============================================================================
TEST_CASE("ZeroShotGuide: 引导 HTML 含步骤标记与状态", "[zeroshot][guide]") {
    // 初始状态：② 为当前待完成步骤（▶），③④ 未完成（○）
    ZeroShotGuideState s;
    const QString html = buildGuideHintHtml(s);

    REQUIRE(html.contains(QStringLiteral("新手引导")));
    REQUIRE(html.contains(QStringLiteral("① 选择模型类型")));
    REQUIRE(html.contains(QStringLiteral("② 加载模型")));
    REQUIRE(html.contains(QStringLiteral("③ 加载图像")));
    REQUIRE(html.contains(QStringLiteral("④ 开始推理")));
    // 步骤①已完成（✔），步骤②待完成（▶），步骤③④未完成（○）
    REQUIRE(html.contains(QStringLiteral("✔")));
    REQUIRE(html.contains(QStringLiteral("▶")));
    REQUIRE(html.contains(QStringLiteral("○")));
    // 下一步提示文案
    REQUIRE(html.contains(QStringLiteral("下一步")));
}

TEST_CASE("ZeroShotGuide: 全部完成后提示完成", "[zeroshot][guide]") {
    ZeroShotGuideState s;
    s.modelLoaded = true;
    s.imageLoaded = true;
    s.hasResult = true;
    const QString html = buildGuideHintHtml(s);
    REQUIRE(html.contains(QStringLiteral("全部完成")));
    REQUIRE_FALSE(html.contains(QStringLiteral("▶")));  // 无待完成步骤
}

// ============================================================================
// 4. 空状态引导文本
// ============================================================================
TEST_CASE("ZeroShotGuide: 空状态引导包含完整步骤指引", "[zeroshot][guide]") {
    const QString hint = buildEmptyStateHint();
    REQUIRE_FALSE(hint.isEmpty());
    REQUIRE(hint.contains(QStringLiteral("选择模型类型")));
    REQUIRE(hint.contains(QStringLiteral("加载模型")));
    REQUIRE(hint.contains(QStringLiteral("加载图像")));
    REQUIRE(hint.contains(QStringLiteral("推理当前")));
    REQUIRE(hint.contains(QStringLiteral("UserGuide")));
}

// ============================================================================
// 5. 缺失前置条件提示
// ============================================================================
TEST_CASE("ZeroShotGuide: 前置条件提示组合", "[zeroshot][guide]") {
    // 双缺：提示先加载模型再加载图像
    const QString both = missingPrerequisiteHint(false, false);
    REQUIRE_FALSE(both.isEmpty());
    REQUIRE(both.contains(QStringLiteral("加载模型")));
    REQUIRE(both.contains(QStringLiteral("加载图像")));

    // 仅缺模型
    const QString noModel = missingPrerequisiteHint(false, true);
    REQUIRE_FALSE(noModel.isEmpty());
    REQUIRE(noModel.contains(QStringLiteral("模型尚未加载")));

    // 仅缺图像
    const QString noImage = missingPrerequisiteHint(true, false);
    REQUIRE_FALSE(noImage.isEmpty());
    REQUIRE(noImage.contains(QStringLiteral("图像")));

    // 全部就绪 → 返回空串
    REQUIRE(missingPrerequisiteHint(true, true).isEmpty());
}

// ============================================================================
// 6. 模型类型通俗解释
// ============================================================================
TEST_CASE("ZeroShotGuide: 模型类型通俗解释", "[zeroshot][guide]") {
    REQUIRE_FALSE(modelTypePlainHelp(QStringLiteral("AnomalyCLIP")).isEmpty());
    REQUIRE_FALSE(modelTypePlainHelp(QStringLiteral("GroundingDINO")).isEmpty());
    REQUIRE_FALSE(modelTypePlainHelp(QStringLiteral("Grounding DINO")).isEmpty());  // 带空格写法
    REQUIRE_FALSE(modelTypePlainHelp(QStringLiteral("MobileSAM")).isEmpty());
    REQUIRE_FALSE(modelTypePlainHelp(QStringLiteral("PatchCore")).isEmpty());
    // 未实现类型：给出明确提示
    REQUIRE(modelTypePlainHelp(QStringLiteral("LocateAnything")).contains(QStringLiteral("未实现")));
    // 未知类型：返回空
    REQUIRE(modelTypePlainHelp(QStringLiteral("Whatever")).isEmpty());
}

// ============================================================================
// 7. 术语通俗解释
// ============================================================================
TEST_CASE("ZeroShotGuide: 常用术语通俗解释", "[zeroshot][guide]") {
    REQUIRE_FALSE(termPlainHelp(QStringLiteral("异常阈值")).isEmpty());
    REQUIRE_FALSE(termPlainHelp(QStringLiteral("检测阈值")).isEmpty());
    REQUIRE_FALSE(termPlainHelp(QStringLiteral("NMS")).isEmpty());
    REQUIRE_FALSE(termPlainHelp(QStringLiteral("量化模型")).isEmpty());
    REQUIRE_FALSE(termPlainHelp(QStringLiteral("多次推理取稳定值")).isEmpty());
    REQUIRE_FALSE(termPlainHelp(QStringLiteral("人工复核")).isEmpty());
    REQUIRE_FALSE(termPlainHelp(QStringLiteral("提示词")).isEmpty());
    REQUIRE_FALSE(termPlainHelp(QStringLiteral("正常样本")).isEmpty());
    // 术语解释必须包含调节方向建议（可操作）
    REQUIRE(termPlainHelp(QStringLiteral("异常阈值")).contains(QStringLiteral("调高")));
    // 未收录术语返回空
    REQUIRE(termPlainHelp(QStringLiteral("量子纠缠")).isEmpty());
}

// ============================================================================
// 8. 步骤文案
// ============================================================================
TEST_CASE("ZeroShotGuide: 步骤文案与详细说明", "[zeroshot][guide]") {
    REQUIRE_EQUAL(guideStepText(1), QStringLiteral("① 选择模型类型"));
    REQUIRE_EQUAL(guideStepText(2), QStringLiteral("② 加载模型"));
    REQUIRE_EQUAL(guideStepText(3), QStringLiteral("③ 加载图像"));
    REQUIRE_EQUAL(guideStepText(4), QStringLiteral("④ 开始推理"));
    REQUIRE(guideStepText(0).isEmpty());
    REQUIRE(guideStepText(5).isEmpty());

    // 每个步骤的详细说明都非空
    for (int s = 1; s <= 4; ++s) {
        REQUIRE_FALSE(guideStepDetail(s).isEmpty());
    }
    REQUIRE(guideStepDetail(0).isEmpty());
}
