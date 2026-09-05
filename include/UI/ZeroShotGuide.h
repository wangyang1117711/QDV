#ifndef ZEROSHOT_GUIDE_H
#define ZEROSHOT_GUIDE_H

// ============================================================================
// ZeroShotGuide.h — 零样本检测模块"新手引导"纯逻辑模块（header-only）
//
// 设计目标：把"分步引导状态计算"与"通俗化文案"从 UI 类中剥离，
// 作为无 QWidget 依赖的纯函数模块，便于单元测试与后续复用。
//
// 使用方式：
//   1. UI 层（ZeroShotDetectView / ZeroShotResultPanel / ZeroShotPanel）
//      收集当前状态（模型是否已加载 / 图像是否已加载 / 是否已有结果），
//      调用本模块生成引导 HTML 或提示文案，更新界面。
//   2. 单元测试直接调用本模块的纯函数验证逻辑，无需实例化任何控件。
//
// 依赖：仅 Qt Core（QString / QStringList），不依赖 Widgets / OpenCV。
// ============================================================================

#include <QString>
#include <QStringList>

namespace QDVMini {

// ----------------------------------------------------------------------------
// 分步引导状态：零样本检测的四个关键步骤
//   ① 选择模型类型（模型类型下拉恒有默认值，视为恒完成）
//   ② 加载模型（modelLoaded）
//   ③ 加载图像（imageLoaded，单张或批量任一就绪即可）
//   ④ 开始推理（hasResult，已有推理结果）
// ----------------------------------------------------------------------------
struct ZeroShotGuideState {
    bool modelLoaded = false;   // 第 2 步：模型已成功加载
    bool imageLoaded = false;   // 第 3 步：已加载图像（单张或批量）
    bool hasResult   = false;   // 第 4 步：已有推理结果
};

// 步骤编号（1..4），与界面显示顺序一致
enum class ZeroShotGuideStep : int {
    SelectModelType = 1,  // ① 选择模型类型
    LoadModel       = 2,  // ② 加载模型
    LoadImage       = 3,  // ③ 加载图像
    RunInference    = 4   // ④ 开始推理
};

/// 判断某一步骤是否已完成
inline bool isGuideStepDone(ZeroShotGuideStep step, const ZeroShotGuideState& state) {
    switch (step) {
    case ZeroShotGuideStep::SelectModelType: return true;  // 下拉恒有默认值
    case ZeroShotGuideStep::LoadModel:       return state.modelLoaded;
    case ZeroShotGuideStep::LoadImage:       return state.imageLoaded;
    case ZeroShotGuideStep::RunInference:    return state.hasResult;
    }
    return false;
}

/// 返回下一个"待完成"的步骤号（1..4）；全部完成时返回 0
inline int nextPendingStep(const ZeroShotGuideState& state) {
    for (int s = 1; s <= 4; ++s) {
        if (!isGuideStepDone(static_cast<ZeroShotGuideStep>(s), state)) {
            return s;
        }
    }
    return 0;  // 全部完成
}

/// 各步骤的显示文案（简短）
inline QString guideStepText(int step) {
    switch (step) {
    case 1: return QStringLiteral("① 选择模型类型");
    case 2: return QStringLiteral("② 加载模型");
    case 3: return QStringLiteral("③ 加载图像");
    case 4: return QStringLiteral("④ 开始推理");
    default: return QString();
    }
}

/// 各步骤的通俗说明（引导条"下一步"提示用）
inline QString guideStepDetail(int step) {
    switch (step) {
    case 1: return QStringLiteral("根据检测目标选择模型：判断“有没有缺陷”用 AnomalyCLIP，定位“缺陷在哪、是什么”用 Grounding DINO。");
    case 2: return QStringLiteral("在“具体模型”下拉直接选择，或点击【浏览...】选择模型目录，然后点击【加载模型】。");
    case 3: return QStringLiteral("点击顶部【加载图像】选择单张图片，或用【批量目录】选择整个文件夹。");
    case 4: return QStringLiteral("点击左侧【推理当前】检测当前图片，或【推理全部】检测整个目录。");
    default: return QString();
    }
}

/// 生成分步引导 HTML（顶部引导条用）
/// 已完成的步骤显示 ✔ 绿色，当前待完成步骤 ▶ 琥珀高亮，后续步骤 ○ 置灰
inline QString buildGuideHintHtml(const ZeroShotGuideState& state) {
    const int pending = nextPendingStep(state);

    QStringList parts;
    for (int s = 1; s <= 4; ++s) {
        const bool done   = isGuideStepDone(static_cast<ZeroShotGuideStep>(s), state);
        const bool active = (pending == s);

        QString color;
        QString prefix;
        if (done) {
            color  = QStringLiteral("#4ec9b0");  // 绿：已完成
            prefix = QStringLiteral("✔ ");
        } else if (active) {
            color  = QStringLiteral("#ffd27f");  // 琥珀：下一步
            prefix = QStringLiteral("▶ ");
        } else {
            color  = QStringLiteral("#777777");  // 灰：未完成
            prefix = QStringLiteral("○ ");
        }

        const QString text = guideStepText(s).toHtmlEscaped();
        parts << QStringLiteral("<span style='color:%1; font-weight:%2;'>%3%4</span>")
                     .arg(color,
                          (done || active) ? QStringLiteral("bold") : QStringLiteral("normal"),
                          prefix, text);
    }

    QString hint;
    if (pending == 0) {
        hint = QStringLiteral(
            "<span style='color:#4ec9b0;'>(全部完成：可导出结果，或更换图片继续检测)</span>");
    } else {
        hint = QStringLiteral(
            "<span style='color:#888888;'>下一步：</span>"
            "<span style='color:#ffd27f;'>%1</span>")
                   .arg(guideStepDetail(pending).toHtmlEscaped());
    }

    return QStringLiteral("<b>新手引导</b>　") + parts.join(QStringLiteral("　"))
         + QStringLiteral("<br/>") + hint;
}

/// 结果面板空状态引导文本（纯文本，多行，用于结果面板空状态页）
inline QString buildEmptyStateHint() {
    return QStringLiteral(
        "还没有检测结果。\n\n"
        "完成以下 4 步即可开始检测：\n"
        "1. 在左侧选择模型类型（如 AnomalyCLIP）\n"
        "2. 点击【加载模型】\n"
        "3. 点击顶部【加载图像】选择图片\n"
        "4. 点击左侧【推理当前】\n\n"
        "详细教程见 docs/UserGuide.md（新手操作手册）。");
}

/// 点击"推理"时缺少前置条件的提示语（用于弹窗/状态栏）；均满足时返回空串
inline QString missingPrerequisiteHint(bool modelLoaded, bool imageLoaded) {
    if (!modelLoaded && !imageLoaded) {
        return QStringLiteral(
            "检测前需要两步准备：\n"
            "1. 先【加载模型】（左侧面板）\n"
            "2. 再【加载图像】（顶部工具栏）\n"
            "完成后再点击推理。");
    }
    if (!modelLoaded) {
        return QStringLiteral(
            "模型尚未加载：\n"
            "请在左侧确认模型类型与路径后，点击【加载模型】。");
    }
    if (!imageLoaded) {
        return QStringLiteral(
            "还没有待检测的图像：\n"
            "请点击顶部【加载图像】选择单张图片，或【批量目录】选择文件夹。");
    }
    return QString();
}

// ----------------------------------------------------------------------------
// 通俗化文案：模型类型 / 专业术语 → 大白话解释
// ----------------------------------------------------------------------------

/// 模型类型 → 通俗解释（用于下拉悬停提示与文档）
inline QString modelTypePlainHelp(const QString& modelTypeStr) {
    const QString t = modelTypeStr.trimmed();
    if (t.compare(QStringLiteral("AnomalyCLIP"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("适合“判断产品有没有缺陷”：给出一句“正常”和“缺陷”的描述，即可输出异常分数（≥阈值判定为异常）。");
    }
    if (t.compare(QStringLiteral("GroundingDINO"), Qt::CaseInsensitive) == 0 ||
        t.compare(QStringLiteral("Grounding DINO"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("适合“找出缺陷在哪、是什么”：用英文类别名描述要检测的目标（如 scratch . dent），输出检测框与类别。");
    }
    if (t.compare(QStringLiteral("MobileSAM"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("适合“把目标轮廓精确抠出来”：输出分割掩码，通常与检测模型配合使用。");
    }
    if (t.compare(QStringLiteral("PatchCore"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("适合“只给正常样本、自动学习正常标准”：添加若干张合格品图片作为样本，即可检测与样本差异大的异常。");
    }
    if (t.compare(QStringLiteral("LocateAnything"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("（未实现）该模型类型暂不支持，请选择其他类型。");
    }
    return QString();
}

/// 专业术语 → 通俗解释（供 tooltip / FAQ 使用）；未收录的术语返回空串
inline QString termPlainHelp(const QString& term) {
    const QString t = term.trimmed();
    if (t.compare(QStringLiteral("异常阈值"), Qt::CaseInsensitive) == 0 ||
        t.compare(QStringLiteral("anomaly threshold"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("判定“缺陷”的门槛（0~1）：异常分数超过该值就判为异常。调高 → 更宽松（漏报变多、误报变少）；调低 → 更严格（误报变多、漏报变少）。建议从 0.5 开始微调。");
    }
    if (t.compare(QStringLiteral("检测阈值"), Qt::CaseInsensitive) == 0 ||
        t.compare(QStringLiteral("detection threshold"), Qt::CaseInsensitive) == 0 ||
        t.compare(QStringLiteral("检测框阈值"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("检测框置信度门槛：只有置信度 ≥ 该值的检测框才会保留显示。调低显示更多（可能含误检），调高更少（可能漏检）。");
    }
    if (t.compare(QStringLiteral("NMS"), Qt::CaseInsensitive) == 0 ||
        t.compare(QStringLiteral("NMS IoU"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("“去重”设置：同一目标被重复框出时自动合并为一个框。数值越大越宽松（保留更多重叠框）。建议保持默认 0.45。");
    }
    if (t.compare(QStringLiteral("量化"), Qt::CaseInsensitive) == 0 ||
        t.compare(QStringLiteral("量化模型"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("模型压缩方式：占用内存更小、速度更快，但精度可能略有下降。显存/内存紧张时建议开启。");
    }
    if (t.compare(QStringLiteral("多次推理"), Qt::CaseInsensitive) == 0 ||
        t.compare(QStringLiteral("多次推理取稳定值"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("同一张图重复推理多次，取“投票”结果，降低随机波动。追求稳定结果时开启，但会成倍增加耗时。");
    }
    if (t.compare(QStringLiteral("人工复核"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("推理完成后由人逐张确认/拒绝结果。被拒绝的样本会自动记录，用于后续优化模型。");
    }
    if (t.compare(QStringLiteral("提示词"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("用一句话（或几个词）描述你想检测的目标。多个目标用“ . ”或“ ; ”分隔。写英文效果更好。");
    }
    if (t.compare(QStringLiteral("正常样本"), Qt::CaseInsensitive) == 0 ||
        t.compare(QStringLiteral("memory bank"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("一批“合格品”图片。PatchCore 会从这些图片学习“正常长什么样”，再拿它与新图片比较，找出异常。");
    }
    if (t.compare(QStringLiteral("渐进式切换阈值"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("样本数量达到该值时，自动从“提示词检测”切换到“PatchCore 正常样本检测”。建议保持默认。");
    }
    return QString();
}

} // namespace QDVMini

#endif // ZEROSHOT_GUIDE_H
