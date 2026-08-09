#include "AI/DatasetValidator.h"
#include "Core/Logger.h"
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QRegularExpression>
#include <opencv2/opencv.hpp>
#include <algorithm>
#include <limits>

using namespace QDV;

// ============================================================
// 辅助：枚举转字符串（用于 JSON 序列化与日志）
// ============================================================
static const char* severityToString(Severity s) {
    switch (s) {
        case Severity::Info:     return "Info";
        case Severity::Warning:  return "Warning";
        case Severity::Error:    return "Error";
        case Severity::Critical: return "Critical";
    }
    return "Unknown";
}

static const char* categoryToString(ValidationCategory c) {
    switch (c) {
        case ValidationCategory::Pairing:       return "Pairing";
        case ValidationCategory::Format:        return "Format";
        case ValidationCategory::Compatibility: return "Compatibility";
        case ValidationCategory::Distribution:  return "Distribution";
    }
    return "Unknown";
}

// ============================================================
// DatasetValidator 构造/析构
// ============================================================
DatasetValidator::DatasetValidator(QObject* parent)
    : QObject(parent)
{
}

DatasetValidator::~DatasetValidator() {
}

// ============================================================
// 主入口：执行四维校验
// ============================================================
ValidationReport DatasetValidator::validate(const ValidationRequest& request) {
    ValidationReport report;

    QDV::Logger::info(QString("DatasetValidator: 开始校验，图像=%1，标签=%2")
                 .arg(request.imagePaths.size()).arg(request.labelPaths.size()));

    // 维度1：配对完整性
    emit validationProgress(1, 4, QStringLiteral("校验配对完整性 (Pairing)..."));
    validatePairing(request, report);

    // 维度2：格式坐标
    emit validationProgress(2, 4, QStringLiteral("校验格式坐标 (Format)..."));
    validateFormat(request, report);

    // 维度3：模型兼容
    emit validationProgress(3, 4, QStringLiteral("校验模型兼容 (Compatibility)..."));
    validateCompatibility(request, report);

    // 维度4：分布统计
    emit validationProgress(4, 4, QStringLiteral("校验分布统计 (Distribution)..."));
    validateDistribution(request, report);

    // 评分计算
    calculateScore(report);

    QDV::Logger::info(QString("DatasetValidator: 校验完成，评分=%1，Critical=%2，Error=%3，Warning=%4")
                 .arg(report.score).arg(report.criticalCount)
                 .arg(report.errorCount).arg(report.warningCount));

    emit validationCompleted(report);
    return report;
}

// ============================================================
// 维度1：Pairing（配对完整性）
// ============================================================
void DatasetValidator::validatePairing(const ValidationRequest& request, ValidationReport& report) {
    // --- 1.1 检查图像文件是否存在 ---
    {
        QStringList missing;
        for (const QString& imgPath : request.imagePaths) {
            if (!fileExists(imgPath)) {
                missing << imgPath;
            }
        }
        if (!missing.isEmpty()) {
            ValidationItem item;
            item.severity = Severity::Critical;
            item.category = ValidationCategory::Pairing;
            item.title = QStringLiteral("图像文件缺失");
            item.description = QStringLiteral("以下图像文件不存在：\n") + missing.join("\n");
            item.suggestion = QStringLiteral("请检查数据集路径，补齐缺失的图像文件或从列表中移除。");
            item.affectedCount = missing.size();
            item.affectedFile = missing.first();
            report.items.append(item);
        }
    }

    // --- 1.2 检查标签文件是否存在 ---
    {
        QStringList missing;
        for (const QString& lblPath : request.labelPaths) {
            if (!fileExists(lblPath)) {
                missing << lblPath;
            }
        }
        if (!missing.isEmpty()) {
            ValidationItem item;
            item.severity = Severity::Critical;
            item.category = ValidationCategory::Pairing;
            item.title = QStringLiteral("标签文件缺失");
            item.description = QStringLiteral("以下标签文件不存在：\n") + missing.join("\n");
            item.suggestion = QStringLiteral("请检查数据集路径，补齐缺失的标签文件或从列表中移除。");
            item.affectedCount = missing.size();
            item.affectedFile = missing.first();
            report.items.append(item);
        }
    }

    // --- 1.3 构建基名映射，用于图像-标签配对 ---
    // 基名 = 去掉扩展名的文件名（不包含路径）
    QSet<QString> labelBasenames;
    for (const QString& lblPath : request.labelPaths) {
        QFileInfo fi(lblPath);
        labelBasenames.insert(fi.completeBaseName());
    }

    // --- 1.4 检查无标注的图像（Warning） ---
    {
        QStringList unlabeled;
        for (const QString& imgPath : request.imagePaths) {
            QFileInfo fi(imgPath);
            if (!labelBasenames.contains(fi.completeBaseName())) {
                unlabeled << imgPath;
            }
        }
        if (!unlabeled.isEmpty()) {
            ValidationItem item;
            item.severity = Severity::Warning;
            item.category = ValidationCategory::Pairing;
            item.title = QStringLiteral("存在无标注的图像");
            item.description = QStringLiteral("以下图像没有对应的标签文件（共 %1 张）：\n%2")
                               .arg(unlabeled.size())
                               .arg(unlabeled.mid(0, 20).join("\n") +
                                    (unlabeled.size() > 20 ? QStringLiteral("\n...") : QString()));
            item.suggestion = QStringLiteral("为这些图像创建空标签文件（背景样本），或从数据集中移除。");
            item.affectedCount = unlabeled.size();
            item.affectedFile = unlabeled.first();
            report.items.append(item);
        }
    }

    // --- 1.5 检查类别引用是否在 expectedLabels 中 ---
    if (!request.expectedLabels.isEmpty()) {
        int maxClassId = request.expectedLabels.size() - 1;
        QStringList invalidRefs;
        int totalInvalid = 0;

        for (const QString& lblPath : request.labelPaths) {
            if (!fileExists(lblPath)) continue;
            QStringList lines = parseLabelFile(lblPath);
            for (const QString& line : lines) {
                QString trimmed = line.trimmed();
                if (trimmed.isEmpty()) continue;
                // 第一个 token 是 class_id
                bool ok = false;
                int classId = trimmed.section(QRegularExpression("\\s+"), 0, 0).toInt(&ok);
                if (!ok || classId < 0 || classId > maxClassId) {
                    invalidRefs << QStringLiteral("%1: class_id=%2")
                                   .arg(QFileInfo(lblPath).fileName())
                                   .arg(trimmed.section(QRegularExpression("\\s+"), 0, 0));
                    totalInvalid++;
                }
            }
        }

        if (totalInvalid > 0) {
            ValidationItem item;
            item.severity = Severity::Error;
            item.category = ValidationCategory::Pairing;
            item.title = QStringLiteral("类别引用超出范围");
            item.description = QStringLiteral("发现 %1 处类别 ID 引用超出期望类别列表范围（0~%2）：\n%3")
                               .arg(totalInvalid).arg(maxClassId)
                               .arg(invalidRefs.mid(0, 20).join("\n") +
                                    (invalidRefs.size() > 20 ? QStringLiteral("\n...") : QString()));
            item.suggestion = QStringLiteral("请修正标签文件中的 class_id，或更新期望类别列表 expectedLabels。");
            item.affectedCount = totalInvalid;
            item.affectedFile = invalidRefs.isEmpty() ? QString() : invalidRefs.first();
            report.items.append(item);
        }
    }
}

// ============================================================
// 维度2：Format（格式坐标）
// ============================================================
void DatasetValidator::validateFormat(const ValidationRequest& request, ValidationReport& report) {
    int sizeMismatchCount = 0;
    int bboxOobCount = 0;       // 越界
    int bboxZeroAreaCount = 0;  // 面积为 0
    int bboxRatioIssueCount = 0;// 占比异常
    int polygonBadCount = 0;    // polygon 顶点不足
    QStringList sizeMismatchFiles;

    bool checkInputSize = request.expectedInputSize.width() > 0 && request.expectedInputSize.height() > 0;

    int imgIndex = 0;
    int imgTotal = request.imagePaths.size();
    for (const QString& imgPath : request.imagePaths) {
        imgIndex++;

        // --- 2.1 读取图像尺寸，检查是否与 expectedInputSize 匹配 ---
        QSize imgSize = readImageSize(imgPath);
        if (checkInputSize && imgSize.width() > 0 && imgSize.height() > 0) {
            if (imgSize != request.expectedInputSize) {
                sizeMismatchCount++;
                if (sizeMismatchFiles.size() < 20) {
                    sizeMismatchFiles << QStringLiteral("%1 (%2x%3)")
                                         .arg(QFileInfo(imgPath).fileName())
                                         .arg(imgSize.width()).arg(imgSize.height());
                }
            }
        }

        // --- 2.2~2.5 解析对应标签文件，检查 bbox / polygon ---
        QFileInfo fi(imgPath);
        // 查找同基名的标签文件
        QString matchedLabel;
        for (const QString& lblPath : request.labelPaths) {
            if (QFileInfo(lblPath).completeBaseName() == fi.completeBaseName()) {
                matchedLabel = lblPath;
                break;
            }
        }
        if (matchedLabel.isEmpty() || !fileExists(matchedLabel)) continue;

        QStringList lines = parseLabelFile(matchedLabel);
        for (const QString& line : lines) {
            QString trimmed = line.trimmed();
            if (trimmed.isEmpty()) continue;

            QStringList tokens = trimmed.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
            if (tokens.size() < 5) continue;  // 至少 class_id + 4 坐标

            bool ok = false;
            tokens[0].toInt(&ok);  // class_id（这里不校验，Pairing 维度已处理）
            if (!ok) continue;

            if (tokens.size() == 5) {
                // bbox 模式：class_id cx cy w h（归一化 [0,1]）
                double cx = tokens[1].toDouble(&ok);
                if (!ok) continue;
                double cy = tokens[2].toDouble(&ok);
                if (!ok) continue;
                double w = tokens[3].toDouble(&ok);
                if (!ok) continue;
                double h = tokens[4].toDouble(&ok);
                if (!ok) continue;

                // 2.2 bbox 越界检查（cx-w/2 < 0 或 cx+w/2 > 1 等）
                double x1 = cx - w / 2.0;
                double y1 = cy - h / 2.0;
                double x2 = cx + w / 2.0;
                double y2 = cy + h / 2.0;
                // 允许微小浮点误差
                const double eps = 1e-6;
                if (x1 < -eps || y1 < -eps || x2 > 1.0 + eps || y2 > 1.0 + eps) {
                    bboxOobCount++;
                }

                // 2.3 bbox 面积为 0
                double area = w * h;
                if (area <= eps) {
                    bboxZeroAreaCount++;
                }

                // 2.4 bbox 占比过小（<1%）或过大（>95%）
                if (area < 0.01 || area > 0.95) {
                    bboxRatioIssueCount++;
                }
            } else {
                // polygon 模式：class_id x1 y1 x2 y2 ...
                int coordCount = tokens.size() - 1;  // 去掉 class_id
                int vertexCount = coordCount / 2;
                if (vertexCount < 3) {
                    polygonBadCount++;
                }
            }
        }
    }

    // --- 汇总 Format 维度的校验项 ---

    // 图像尺寸不匹配（Warning）
    if (sizeMismatchCount > 0) {
        ValidationItem item;
        item.severity = Severity::Warning;
        item.category = ValidationCategory::Format;
        item.title = QStringLiteral("图像尺寸与期望输入尺寸不匹配");
        item.description = QStringLiteral("共 %1 张图像尺寸与期望尺寸 %2x%3 不一致：\n%4")
                           .arg(sizeMismatchCount)
                           .arg(request.expectedInputSize.width())
                           .arg(request.expectedInputSize.height())
                           .arg(sizeMismatchFiles.join("\n"));
        item.suggestion = QStringLiteral("建议在训练前将图像统一缩放到期望输入尺寸，或使用数据增强进行自适应。");
        item.affectedCount = sizeMismatchCount;
        report.items.append(item);
    }

    // bbox 越界（Error）
    if (bboxOobCount > 0) {
        ValidationItem item;
        item.severity = Severity::Error;
        item.category = ValidationCategory::Format;
        item.title = QStringLiteral("bbox 坐标越界");
        item.description = QStringLiteral("发现 %1 个 bbox 坐标超出图像边界 [0,1]。").arg(bboxOobCount);
        item.suggestion = QStringLiteral("请检查标签文件，将 bbox 坐标裁剪到 [0,1] 范围内。");
        item.affectedCount = bboxOobCount;
        report.items.append(item);
    }

    // bbox 面积为 0（Error）
    if (bboxZeroAreaCount > 0) {
        ValidationItem item;
        item.severity = Severity::Error;
        item.category = ValidationCategory::Format;
        item.title = QStringLiteral("bbox 面积为零");
        item.description = QStringLiteral("发现 %1 个 bbox 的宽或高为 0，面积为零。").arg(bboxZeroAreaCount);
        item.suggestion = QStringLiteral("请修正或删除这些无效标注。");
        item.affectedCount = bboxZeroAreaCount;
        report.items.append(item);
    }

    // bbox 占比异常（Warning）
    if (bboxRatioIssueCount > 0) {
        ValidationItem item;
        item.severity = Severity::Warning;
        item.category = ValidationCategory::Format;
        item.title = QStringLiteral("bbox 占比异常");
        item.description = QStringLiteral("发现 %1 个 bbox 占图像面积比例过小（<1%）或过大（>95%）。").arg(bboxRatioIssueCount);
        item.suggestion = QStringLiteral("占比过小可能导致训练困难，过大可能标注有误，建议复核。");
        item.affectedCount = bboxRatioIssueCount;
        report.items.append(item);
    }

    // polygon 顶点不足（Error）
    if (polygonBadCount > 0) {
        ValidationItem item;
        item.severity = Severity::Error;
        item.category = ValidationCategory::Format;
        item.title = QStringLiteral("polygon 顶点数不足");
        item.description = QStringLiteral("发现 %1 个 polygon 标注顶点数少于 3，无法构成多边形。").arg(polygonBadCount);
        item.suggestion = QStringLiteral("polygon 至少需要 3 个顶点（6 个坐标值），请补全或删除该标注。");
        item.affectedCount = polygonBadCount;
        report.items.append(item);
    }
}

// ============================================================
// 维度3：Compatibility（模型兼容）
// ============================================================
void DatasetValidator::validateCompatibility(const ValidationRequest& request, ValidationReport& report) {
    // --- 3.1 检查模型文件是否存在（Critical） ---
    if (!request.modelPath.isEmpty()) {
        if (!fileExists(request.modelPath)) {
            ValidationItem item;
            item.severity = Severity::Critical;
            item.category = ValidationCategory::Compatibility;
            item.title = QStringLiteral("模型文件不存在");
            item.description = QStringLiteral("模型文件不存在：%1").arg(request.modelPath);
            item.suggestion = QStringLiteral("请检查模型路径，或重新导入/训练模型。");
            item.affectedCount = 1;
            item.affectedFile = request.modelPath;
            report.items.append(item);
        }
    }

    // --- 3.2 检查图像尺寸是否与模型输入尺寸匹配（Warning） ---
    if (request.expectedInputSize.width() > 0 && request.expectedInputSize.height() > 0
        && !request.imagePaths.isEmpty()) {
        int mismatchCount = 0;
        QStringList sampleFiles;
        for (const QString& imgPath : request.imagePaths) {
            QSize imgSize = readImageSize(imgPath);
            if (imgSize.width() > 0 && imgSize.height() > 0
                && imgSize != request.expectedInputSize) {
                mismatchCount++;
                if (sampleFiles.size() < 10) {
                    sampleFiles << QStringLiteral("%1 (%2x%3)")
                                   .arg(QFileInfo(imgPath).fileName())
                                   .arg(imgSize.width()).arg(imgSize.height());
                }
            }
        }
        if (mismatchCount > 0) {
            ValidationItem item;
            item.severity = Severity::Warning;
            item.category = ValidationCategory::Compatibility;
            item.title = QStringLiteral("图像尺寸与模型输入尺寸不匹配");
            item.description = QStringLiteral("共 %1 张图像尺寸与模型期望输入 %2x%3 不匹配：\n%4")
                               .arg(mismatchCount)
                               .arg(request.expectedInputSize.width())
                               .arg(request.expectedInputSize.height())
                               .arg(sampleFiles.join("\n"));
            item.suggestion = QStringLiteral("推理前需将图像 resize/letterbox 到模型输入尺寸，否则可能影响精度。");
            item.affectedCount = mismatchCount;
            report.items.append(item);
        }
    }

    // --- 3.3 检查类别数是否与模型输出匹配（Error） ---
    if (request.expectedNumClasses > 0 && !request.expectedLabels.isEmpty()) {
        int labelCount = request.expectedLabels.size();
        if (labelCount != request.expectedNumClasses) {
            ValidationItem item;
            item.severity = Severity::Error;
            item.category = ValidationCategory::Compatibility;
            item.title = QStringLiteral("类别数与模型输出不匹配");
            item.description = QStringLiteral("期望类别列表含 %1 个类别，但模型期望输出 %2 个类别。")
                               .arg(labelCount).arg(request.expectedNumClasses);
            item.suggestion = QStringLiteral("请确认类别列表与模型训练时的类别数一致。");
            item.affectedCount = 1;
            report.items.append(item);
        }
    }
}

// ============================================================
// 维度4：Distribution（分布统计）
// ============================================================
void DatasetValidator::validateDistribution(const ValidationRequest& request, ValidationReport& report) {
    // --- 统计每个类别的样本数和 bbox 面积分布 ---
    QMap<int, int> classSampleCount;      // class_id -> 样本数（含该类别的图像数）
    QMap<int, QList<double>> classAreas;  // class_id -> bbox 面积列表

    for (const QString& lblPath : request.labelPaths) {
        if (!fileExists(lblPath)) continue;
        QStringList lines = parseLabelFile(lblPath);
        QSet<int> classesInThisFile;  // 同一文件中同一类别只计一次样本

        for (const QString& line : lines) {
            QString trimmed = line.trimmed();
            if (trimmed.isEmpty()) continue;

            QStringList tokens = trimmed.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
            if (tokens.size() < 5) continue;

            bool ok = false;
            int classId = tokens[0].toInt(&ok);
            if (!ok) continue;

            classesInThisFile.insert(classId);

            // 记录 bbox 面积（仅 bbox 模式）
            if (tokens.size() == 5) {
                double w = tokens[3].toDouble(&ok);
                if (!ok) continue;
                double h = tokens[4].toDouble(&ok);
                if (!ok) continue;
                classAreas[classId].append(w * h);
            }
        }

        for (int cid : classesInThisFile) {
            classSampleCount[cid]++;
        }
    }

    // --- 4.1 类别无样本（Critical） ---
    if (!request.expectedLabels.isEmpty()) {
        QStringList emptyClasses;
        for (int i = 0; i < request.expectedLabels.size(); ++i) {
            if (classSampleCount.value(i, 0) == 0) {
                emptyClasses << QStringLiteral("%1 (id=%2)").arg(request.expectedLabels[i]).arg(i);
            }
        }
        if (!emptyClasses.isEmpty()) {
            ValidationItem item;
            item.severity = Severity::Critical;
            item.category = ValidationCategory::Distribution;
            item.title = QStringLiteral("类别无样本");
            item.description = QStringLiteral("以下类别没有任何样本：\n%1").arg(emptyClasses.join("\n"));
            item.suggestion = QStringLiteral("请为这些类别补充标注样本，或从类别列表中移除。");
            item.affectedCount = emptyClasses.size();
            report.items.append(item);
        }
    }

    // --- 4.2 类别样本数 < 10（Warning） ---
    {
        QStringList lowSampleClasses;
        for (auto it = classSampleCount.constBegin(); it != classSampleCount.constEnd(); ++it) {
            if (it.value() < 10) {
                QString className = (it.key() < request.expectedLabels.size())
                                    ? request.expectedLabels[it.key()]
                                    : QStringLiteral("class_%1").arg(it.key());
                lowSampleClasses << QStringLiteral("%1 (id=%2): %3 张")
                                    .arg(className).arg(it.key()).arg(it.value());
            }
        }
        if (!lowSampleClasses.isEmpty()) {
            ValidationItem item;
            item.severity = Severity::Warning;
            item.category = ValidationCategory::Distribution;
            item.title = QStringLiteral("类别样本数不足");
            item.description = QStringLiteral("以下类别的样本数少于 10（建议每类至少 10 张）：\n%1")
                               .arg(lowSampleClasses.join("\n"));
            item.suggestion = QStringLiteral("样本过少会导致训练过拟合，建议补充数据或使用数据增强。");
            item.affectedCount = lowSampleClasses.size();
            report.items.append(item);
        }
    }

    // --- 4.3 类别不平衡（最多/最少 > 10:1）（Warning） ---
    if (classSampleCount.size() >= 2) {
        int maxCount = 0;
        int minCount = std::numeric_limits<int>::max();
        QString maxClass;
        QString minClass;
        for (auto it = classSampleCount.constBegin(); it != classSampleCount.constEnd(); ++it) {
            if (it.value() > maxCount) {
                maxCount = it.value();
                maxClass = (it.key() < request.expectedLabels.size())
                           ? request.expectedLabels[it.key()]
                           : QStringLiteral("class_%1").arg(it.key());
            }
            if (it.value() < minCount) {
                minCount = it.value();
                minClass = (it.key() < request.expectedLabels.size())
                           ? request.expectedLabels[it.key()]
                           : QStringLiteral("class_%1").arg(it.key());
            }
        }
        if (minCount > 0 && static_cast<double>(maxCount) / minCount > 10.0) {
            ValidationItem item;
            item.severity = Severity::Warning;
            item.category = ValidationCategory::Distribution;
            item.title = QStringLiteral("类别分布不平衡");
            item.description = QStringLiteral("类别样本数差异过大：最多类别 %1=%2 张，最少类别 %3=%4 张，比值 %5:1。")
                               .arg(maxClass).arg(maxCount).arg(minClass).arg(minCount)
                               .arg(static_cast<double>(maxCount) / minCount, 0, 'f', 1);
            item.suggestion = QStringLiteral("建议使用过采样/欠采样或类别加权损失缓解不平衡问题。");
            item.affectedCount = classSampleCount.size();
            report.items.append(item);
        }
    }

    // --- 4.4 尺寸变异过大（最大/最小面积 > 5:1）（Warning） ---
    {
        QStringList highVariationClasses;
        for (auto it = classAreas.constBegin(); it != classAreas.constEnd(); ++it) {
            const QList<double>& areas = it.value();
            if (areas.size() < 2) continue;
            double maxArea = *std::max_element(areas.begin(), areas.end());
            double minArea = *std::min_element(areas.begin(), areas.end());
            if (minArea > 1e-9 && maxArea / minArea > 5.0) {
                QString className = (it.key() < request.expectedLabels.size())
                                    ? request.expectedLabels[it.key()]
                                    : QStringLiteral("class_%1").arg(it.key());
                highVariationClasses << QStringLiteral("%1: 最大面积=%2, 最小面积=%3, 比值=%4:1")
                                        .arg(className)
                                        .arg(maxArea, 0, 'f', 4)
                                        .arg(minArea, 0, 'f', 4)
                                        .arg(maxArea / minArea, 0, 'f', 1);
            }
        }
        if (!highVariationClasses.isEmpty()) {
            ValidationItem item;
            item.severity = Severity::Warning;
            item.category = ValidationCategory::Distribution;
            item.title = QStringLiteral("目标尺寸变异过大");
            item.description = QStringLiteral("以下类别的 bbox 面积最大/最小比值超过 5:1：\n%1")
                               .arg(highVariationClasses.join("\n"));
            item.suggestion = QStringLiteral("尺寸变异过大会影响检测精度，建议使用多尺度训练或图像金字塔。");
            item.affectedCount = highVariationClasses.size();
            report.items.append(item);
        }
    }
}

// ============================================================
// 评分计算
// ============================================================
void DatasetValidator::calculateScore(ValidationReport& report) {
    // 统计各级别数量
    report.criticalCount = 0;
    report.errorCount = 0;
    report.warningCount = 0;
    report.infoCount = 0;

    for (const ValidationItem& item : report.items) {
        switch (item.severity) {
            case Severity::Critical: report.criticalCount++; break;
            case Severity::Error:    report.errorCount++;    break;
            case Severity::Warning:  report.warningCount++;  break;
            case Severity::Info:     report.infoCount++;     break;
        }
    }

    // 评分：满分 100，Critical -30 / Error -10 / Warning -2，最低 0
    int score = 100;
    score -= report.criticalCount * 30;
    score -= report.errorCount * 10;
    score -= report.warningCount * 2;
    report.score = std::max(0, score);
}

// ============================================================
// ValidationReport 方法实现
// ============================================================
QList<ValidationItem> ValidationReport::pairingItems() const {
    QList<ValidationItem> result;
    for (const ValidationItem& item : items) {
        if (item.category == ValidationCategory::Pairing) {
            result.append(item);
        }
    }
    return result;
}

QList<ValidationItem> ValidationReport::formatItems() const {
    QList<ValidationItem> result;
    for (const ValidationItem& item : items) {
        if (item.category == ValidationCategory::Format) {
            result.append(item);
        }
    }
    return result;
}

QList<ValidationItem> ValidationReport::compatibilityItems() const {
    QList<ValidationItem> result;
    for (const ValidationItem& item : items) {
        if (item.category == ValidationCategory::Compatibility) {
            result.append(item);
        }
    }
    return result;
}

QList<ValidationItem> ValidationReport::distributionItems() const {
    QList<ValidationItem> result;
    for (const ValidationItem& item : items) {
        if (item.category == ValidationCategory::Distribution) {
            result.append(item);
        }
    }
    return result;
}

bool ValidationReport::isHealthy() const {
    // 无 Critical/Error
    return criticalCount == 0 && errorCount == 0;
}

bool ValidationReport::canTrain() const {
    // 无 Critical 且 Error<=5
    return criticalCount == 0 && errorCount <= 5;
}

QJsonObject ValidationReport::toJson() const {
    QJsonObject root;

    // 评分与计数
    root["score"] = score;
    root["criticalCount"] = criticalCount;
    root["errorCount"] = errorCount;
    root["warningCount"] = warningCount;
    root["infoCount"] = infoCount;

    // 门禁状态
    root["isHealthy"] = isHealthy();
    root["canTrain"] = canTrain();

    // 所有校验项
    QJsonArray itemsArray;
    for (const ValidationItem& item : items) {
        QJsonObject itemObj;
        itemObj["severity"] = QString::fromLatin1(severityToString(item.severity));
        itemObj["category"] = QString::fromLatin1(categoryToString(item.category));
        itemObj["title"] = item.title;
        itemObj["description"] = item.description;
        itemObj["suggestion"] = item.suggestion;
        itemObj["affectedFile"] = item.affectedFile;
        itemObj["affectedCount"] = item.affectedCount;
        itemsArray.append(itemObj);
    }
    root["items"] = itemsArray;

    // 按维度分组（每组为一个 JSON 数组）
    QJsonObject grouped;
    QJsonArray pairingArr;
    for (const ValidationItem& item : pairingItems()) {
        QJsonObject itemObj;
        itemObj["severity"] = QString::fromLatin1(severityToString(item.severity));
        itemObj["category"] = QString::fromLatin1(categoryToString(item.category));
        itemObj["title"] = item.title;
        itemObj["description"] = item.description;
        itemObj["suggestion"] = item.suggestion;
        itemObj["affectedFile"] = item.affectedFile;
        itemObj["affectedCount"] = item.affectedCount;
        pairingArr.append(itemObj);
    }
    grouped["pairing"] = pairingArr;

    QJsonArray formatArr;
    for (const ValidationItem& item : formatItems()) {
        QJsonObject itemObj;
        itemObj["severity"] = QString::fromLatin1(severityToString(item.severity));
        itemObj["category"] = QString::fromLatin1(categoryToString(item.category));
        itemObj["title"] = item.title;
        itemObj["description"] = item.description;
        itemObj["suggestion"] = item.suggestion;
        itemObj["affectedFile"] = item.affectedFile;
        itemObj["affectedCount"] = item.affectedCount;
        formatArr.append(itemObj);
    }
    grouped["format"] = formatArr;

    QJsonArray compatibilityArr;
    for (const ValidationItem& item : compatibilityItems()) {
        QJsonObject itemObj;
        itemObj["severity"] = QString::fromLatin1(severityToString(item.severity));
        itemObj["category"] = QString::fromLatin1(categoryToString(item.category));
        itemObj["title"] = item.title;
        itemObj["description"] = item.description;
        itemObj["suggestion"] = item.suggestion;
        itemObj["affectedFile"] = item.affectedFile;
        itemObj["affectedCount"] = item.affectedCount;
        compatibilityArr.append(itemObj);
    }
    grouped["compatibility"] = compatibilityArr;

    QJsonArray distributionArr;
    for (const ValidationItem& item : distributionItems()) {
        QJsonObject itemObj;
        itemObj["severity"] = QString::fromLatin1(severityToString(item.severity));
        itemObj["category"] = QString::fromLatin1(categoryToString(item.category));
        itemObj["title"] = item.title;
        itemObj["description"] = item.description;
        itemObj["suggestion"] = item.suggestion;
        itemObj["affectedFile"] = item.affectedFile;
        itemObj["affectedCount"] = item.affectedCount;
        distributionArr.append(itemObj);
    }
    grouped["distribution"] = distributionArr;

    root["grouped"] = grouped;

    return root;
}

// ============================================================
// 辅助方法
// ============================================================
bool DatasetValidator::fileExists(const QString& path) const {
    return QFileInfo::exists(path);
}

QSize DatasetValidator::readImageSize(const QString& path) const {
    // 使用 QFile + cv::imdecode 读取（兼容中文路径）
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QDV::Logger::warn("DatasetValidator: cannot open image: " + path);
        return QSize();
    }
    QByteArray data = file.readAll();
    file.close();

    // 仅读取头部信息即可获取尺寸（IMREAD_UNCHANGED 避免额外转换开销）
    cv::Mat image = cv::imdecode(cv::Mat(1, data.size(), CV_8UC1, data.data()), cv::IMREAD_UNCHANGED);
    if (image.empty()) {
        QDV::Logger::warn("DatasetValidator: imdecode failed: " + path);
        return QSize();
    }
    return QSize(image.cols, image.rows);
}

QStringList DatasetValidator::parseLabelFile(const QString& path) const {
    // 使用 QFile 读取标签文件（兼容中文路径）
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QDV::Logger::warn("DatasetValidator: cannot open label: " + path);
        return QStringList();
    }
    QString content = QString::fromUtf8(file.readAll());
    file.close();
    return content.split('\n', Qt::SkipEmptyParts);
}
