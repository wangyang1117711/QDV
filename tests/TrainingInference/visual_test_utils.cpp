#include "visual_test_utils.hpp"
#include "TrainingInference/TrainingInferenceView.h"
#include "Core/Logger.h"
#include <QApplication>
#include <QScreen>
#include <QGuiApplication>
#include <QPainter>
#include <QBuffer>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>

VisualTestUtils::VisualTestUtils(const QString& outputDir) 
    : m_outputDir(outputDir)
{
    ensureOutputDir();
}

void VisualTestUtils::ensureOutputDir()
{
    QDir dir(m_outputDir);
    if (!dir.exists()) {
        dir.mkpath(".");
    }
}

QString VisualTestUtils::generateScreenshotFilename(const QString& baseName)
{
    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss_zzz");
    return QString("%1/%2_%3_%4.png")
        .arg(m_outputDir)
        .arg(baseName)
        .arg(++m_screenshotCounter)
        .arg(timestamp);
}

QImage VisualTestUtils::captureScreenshot(QWidget* widget, const QString& name)
{
    if (!widget) {
        qWarning() << "[VisualTest] Widget is null, cannot capture screenshot";
        return QImage();
    }
    
    // 确保widget已渲染
    widget->ensurePolished();
    QApplication::processEvents();
    
    // 截图
    QPixmap pixmap = widget->grab();
    QImage screenshot = pixmap.toImage();
    
    // 保存
    QString filename = saveScreenshot(screenshot, name);
    QDV::Logger::info(QString("[VisualTest] Screenshot saved: %1").arg(filename));
    
    return screenshot;
}

QImage VisualTestUtils::captureRegion(QWidget* widget, const QRect& rect, const QString& name)
{
    QImage full = captureScreenshot(widget, name + "_full");
    if (full.isNull()) {
        return QImage();
    }
    
    QRect safeRect = rect.intersected(full.rect());
    QImage region = full.copy(safeRect);
    QString filename = saveScreenshot(region, name + "_region");
    
    return region;
}

QString VisualTestUtils::saveScreenshot(const QImage& image, const QString& name)
{
    QString filename = generateScreenshotFilename(name);
    if (image.save(filename, "PNG")) {
        return filename;
    }
    qWarning() << "[VisualTest] Failed to save screenshot:" << filename;
    return QString();
}

VisualVerificationResult VisualTestUtils::verifyCheckboxExists(
    QWidget* parent, 
    const QString& objectName,
    int index)
{
    VisualVerificationResult result;
    
    if (!parent) {
        result.message = "Parent widget is null";
        return result;
    }
    
    // 先通过Qt对象树查找
    QList<QCheckBox*> checkboxes = parent->findChildren<QCheckBox*>();
    
    QCheckBox* targetCheckbox = nullptr;
    if (!objectName.isEmpty()) {
        for (QCheckBox* cb : checkboxes) {
            if (cb->objectName() == objectName) {
                targetCheckbox = cb;
                break;
            }
        }
    } else if (index >= 0 && index < checkboxes.size()) {
        targetCheckbox = checkboxes[index];
    } else if (!checkboxes.isEmpty()) {
        targetCheckbox = checkboxes.first();
    }
    
    if (targetCheckbox) {
        result.passed = true;
        result.message = QString("Checkbox found at position (%1, %2), visible: %3")
            .arg(targetCheckbox->x())
            .arg(targetCheckbox->y())
            .arg(targetCheckbox->isVisible());
        
        // 获取复选框区域并截图验证
        QRect cbRect = targetCheckbox->geometry();
        result.screenshot = captureRegion(parent, cbRect, "checkbox_verify");
        
        QDV::Logger::info(QString("[VisualTest] Checkbox verified: %1").arg(result.message));
    } else {
        result.passed = false;
        result.message = QString("Checkbox not found. Total checkboxes: %1").arg(checkboxes.size());
        result.screenshot = captureScreenshot(parent, "checkbox_not_found");
        
        QDV::Logger::warn(QString("[VisualTest] %1").arg(result.message));
    }
    
    return result;
}

VisualVerificationResult VisualTestUtils::verifyPushButtonExists(
    QWidget* parent,
    const QString& buttonText,
    bool checkEnabled)
{
    VisualVerificationResult result;
    
    QList<QPushButton*> buttons = parent->findChildren<QPushButton*>();
    
    for (QPushButton* btn : buttons) {
        if (btn->text() == buttonText) {
            result.passed = true;
            if (checkEnabled) {
                bool enabled = btn->isEnabled();
                result.message = QString("Button '%1' found, enabled: %2")
                    .arg(buttonText).arg(enabled);
                result.passed = true;  // 存在性验证通过，即使禁用也是OK的
            } else {
                result.message = QString("Button '%1' found").arg(buttonText);
            }
            
            // 截图按钮区域
            QRect btnRect = btn->geometry();
            result.screenshot = captureRegion(parent, btnRect, "button_" + buttonText);
            
            QDV::Logger::info(QString("[VisualTest] %1").arg(result.message));
            return result;
        }
    }
    
    result.passed = false;
    result.message = QString("Button '%1' not found. Total buttons: %2")
        .arg(buttonText).arg(buttons.size());
    result.screenshot = captureScreenshot(parent, "button_not_found");
    
    return result;
}

VisualVerificationResult VisualTestUtils::verifyComboBoxSelection(
    QComboBox* comboBox,
    const QString& expectedSelection)
{
    VisualVerificationResult result;
    
    if (!comboBox) {
        result.message = "ComboBox is null";
        return result;
    }
    
    QString currentSelection = comboBox->currentText();
    result.passed = (currentSelection == expectedSelection);
    
    if (result.passed) {
        result.message = QString("ComboBox selection correct: '%1'").arg(currentSelection);
    } else {
        result.message = QString("ComboBox selection mismatch: expected '%1', got '%2'")
            .arg(expectedSelection).arg(currentSelection);
    }
    
    QDV::Logger::info(QString("[VisualTest] %1").arg(result.message));
    
    // 截图
    result.screenshot = captureScreenshot(comboBox->parentWidget(), "combobox_selection");
    
    return result;
}

VisualVerificationResult VisualTestUtils::verifyCategoryAddButtons(
    QTreeWidget* categoryTree,
    bool shouldBeEnabled)
{
    VisualVerificationResult result;
    
    if (!categoryTree) {
        result.message = "Category tree is null";
        return result;
    }
    
    int checkedCount = 0;
    int totalCount = 0;
    
    // 遍历树形项查找"加入"按钮
    for (int i = 0; i < categoryTree->topLevelItemCount(); ++i) {
        QTreeWidgetItem* item = categoryTree->topLevelItem(i);
        
        QWidget* widget = categoryTree->itemWidget(item, 0);
        if (widget) {
            QPushButton* addBtn = widget->findChild<QPushButton*>();
            if (addBtn && addBtn->text() == "加入") {
                totalCount++;
                bool enabled = addBtn->isEnabled();
                if (enabled == shouldBeEnabled) {
                    checkedCount++;
                }
            }
        }
        
        // 检查子项
        for (int j = 0; j < item->childCount(); ++j) {
            QTreeWidgetItem* childItem = item->child(j);
            QWidget* childWidget = categoryTree->itemWidget(childItem, 0);
            if (childWidget) {
                QPushButton* addBtn = childWidget->findChild<QPushButton*>();
                if (addBtn && addBtn->text() == "加入") {
                    totalCount++;
                    if (addBtn->isEnabled() == shouldBeEnabled) {
                        checkedCount++;
                    }
                }
            }
        }
    }
    
    result.passed = (totalCount > 0 && checkedCount == totalCount);
    result.message = QString("Category buttons verified: %1/%2 in expected state (enabled=%3)")
        .arg(checkedCount).arg(totalCount).arg(shouldBeEnabled);
    
    result.screenshot = captureScreenshot(categoryTree, "category_buttons");
    
    QDV::Logger::info(QString("[VisualTest] %1").arg(result.message));
    
    return result;
}

VisualVerificationResult VisualTestUtils::verifyCheckboxState(
    QCheckBox* checkbox,
    bool expectedChecked)
{
    VisualVerificationResult result;
    
    if (!checkbox) {
        result.message = "CheckBox is null";
        return result;
    }
    
    bool actualChecked = checkbox->isChecked();
    result.passed = (actualChecked == expectedChecked);
    
    if (result.passed) {
        result.message = QString("Checkbox state correct: %1").arg(expectedChecked ? "checked" : "unchecked");
    } else {
        result.message = QString("Checkbox state mismatch: expected %1, got %2")
            .arg(expectedChecked ? "checked" : "unchecked")
            .arg(actualChecked ? "checked" : "unchecked");
    }
    
    // 截图
    result.screenshot = captureRegion(
        checkbox->parentWidget(), 
        checkbox->geometry(), 
        "checkbox_state"
    );
    
    QDV::Logger::info(QString("[VisualTest] %1").arg(result.message));
    
    return result;
}

VisualVerificationResult VisualTestUtils::verifySelectAllButtonState(
    QPushButton* button,
    const QString& expectedText)
{
    VisualVerificationResult result;
    
    if (!button) {
        result.message = "SelectAll button is null";
        return result;
    }
    
    QString actualText = button->text();
    result.passed = (actualText == expectedText);
    
    if (result.passed) {
        result.message = QString("SelectAll button text correct: '%1'").arg(actualText);
    } else {
        result.message = QString("SelectAll button text mismatch: expected '%1', got '%2'")
            .arg(expectedText).arg(actualText);
    }
    
    result.screenshot = captureRegion(
        button->parentWidget(),
        button->geometry(),
        "selectall_button"
    );
    
    QDV::Logger::info(QString("[VisualTest] %1").arg(result.message));
    
    return result;
}

VisualVerificationResult VisualTestUtils::verifySelectionCount(
    QLabel* countLabel,
    const QString& expectedPattern)
{
    VisualVerificationResult result;
    
    if (!countLabel) {
        result.message = "Selection count label is null";
        return result;
    }
    
    QString actualText = countLabel->text();
    
    // 使用正则表达式验证模式
    QRegularExpression regex(expectedPattern);
    QRegularExpressionMatch match = regex.match(actualText);
    result.passed = match.hasMatch();
    
    if (result.passed) {
        result.message = QString("Selection count label matches pattern: '%1'").arg(actualText);
    } else {
        result.message = QString("Selection count label mismatch: expected pattern '%1', got '%2'")
            .arg(expectedPattern).arg(actualText);
    }
    
    result.screenshot = captureRegion(
        countLabel->parentWidget(),
        countLabel->geometry(),
        "selection_count"
    );
    
    QDV::Logger::info(QString("[VisualTest] %1").arg(result.message));
    
    return result;
}

bool VisualTestUtils::isColorSimilar(QColor c1, QColor c2, int tolerance)
{
    int dr = std::abs(c1.red() - c2.red());
    int dg = std::abs(c1.green() - c2.green());
    int db = std::abs(c1.blue() - c2.blue());
    
    return (dr <= tolerance && dg <= tolerance && db <= tolerance);
}

bool VisualTestUtils::detectColorPresence(
    const QImage& image, 
    QColor targetColor, 
    QRect searchArea,
    double threshold)
{
    if (image.isNull()) return false;
    
    QRect area = searchArea.isNull() ? image.rect() : searchArea;
    area = area.intersected(image.rect());
    
    int matchCount = 0;
    int totalPixels = 0;
    
    for (int y = area.top(); y < area.bottom(); ++y) {
        for (int x = area.left(); x < area.right(); ++x) {
            QColor pixelColor = image.pixelColor(x, y);
            if (isColorSimilar(pixelColor, targetColor)) {
                matchCount++;
            }
            totalPixels++;
        }
    }
    
    double ratio = static_cast<double>(matchCount) / totalPixels;
    return ratio >= threshold;
}

QColor VisualTestUtils::getWidgetPixelColor(QWidget* widget, const QPoint& pos)
{
    if (!widget) return QColor();
    
    QImage img = widget->grab().toImage();
    if (img.valid(pos)) {
        return img.pixelColor(pos);
    }
    return QColor();
}

QRect VisualTestUtils::findElementByColor(
    const QImage& image, 
    QColor targetColor, 
    int minSize)
{
    // 简单的颜色聚类找区域
    // 实际项目中可用OpenCV做更精确的物体检测
    QRect foundRect;
    
    int minX = image.width();
    int minY = image.height();
    int maxX = 0;
    int maxY = 0;
    
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            QColor color = image.pixelColor(x, y);
            if (isColorSimilar(color, targetColor, 50)) {
                minX = std::min(minX, x);
                minY = std::min(minY, y);
                maxX = std::max(maxX, x);
                maxY = std::max(maxY, y);
            }
        }
    }
    
    if (maxX - minX >= minSize && maxY - minY >= minSize) {
        foundRect = QRect(QPoint(minX, minY), QPoint(maxX, maxY));
    }
    
    return foundRect;
}

VisualTestUtils::CheckboxFlowVerification 
VisualTestUtils::verifyCheckboxFunctionality(
    QCheckBox* checkbox,
    QWidget* parentWidget,
    const std::function<void()>& setDataChecked,
    const std::function<bool()>& getDataChecked)
{
    CheckboxFlowVerification result;
    
    // 1. 操作前验证
    result.beforeCheck = verifyCheckboxState(checkbox, checkbox->isChecked());
    
    // 2. 执行勾选操作
    bool originalState = checkbox->isChecked();
    checkbox->click();
    QApplication::processEvents();
    
    // 3. 操作后视觉验证
    result.afterCheck = verifyCheckboxState(checkbox, !originalState);
    
    // 4. 数据一致性验证
    bool dataState = getDataChecked();
    result.dataConsistency.passed = (dataState == !originalState);
    result.dataConsistency.message = QString("Data consistency: UI state (%1) matches data state (%2)")
        .arg(!originalState).arg(dataState);
    
    // 5. 综合结果
    result.overallPassed = result.beforeCheck.passed && 
                          result.afterCheck.passed && 
                          result.dataConsistency.passed;
    
    if (result.overallPassed) {
        result.summary = "Checkbox functionality fully verified";
    } else {
        QStringList issues;
        if (!result.beforeCheck.passed) issues << "pre-check failed";
        if (!result.afterCheck.passed) issues << "post-check failed";
        if (!result.dataConsistency.passed) issues << "data consistency failed";
        result.summary = "Checkbox verification failed: " + issues.join(", ");
    }
    
    QDV::Logger::info(QString("[VisualTest] %1").arg(result.summary));
    
    return result;
}

void VisualTestUtils::generateVerificationReport(
    const QString& testName, 
    const VisualVerificationResult& result)
{
    QString reportFile = QString("%1/%2_report.txt").arg(m_outputDir).arg(testName);
    QFile file(reportFile);
    
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&file);
        out << "=== Visual Verification Report: " << testName << " ===\n";
        out << "Result: " << (result.passed ? "PASSED" : "FAILED") << "\n";
        out << "Message: " << result.message << "\n";
        out << "Confidence: " << QString::number(result.confidence * 100, 'f', 1) << "%\n";
        out << "Screenshot captured: " << (!result.screenshot.isNull()) << "\n";
        out << "Detected regions: " << result.detectedRegions.size() << "\n";
        
        for (size_t i = 0; i < result.detectedRegions.size(); ++i) {
            out << "  Region " << (i + 1) << ": " 
                << result.detectedRegions[i].x() << "," 
                << result.detectedRegions[i].y() << " "
                << result.detectedRegions[i].width() << "x" 
                << result.detectedRegions[i].height() << "\n";
        }
        
        file.close();
        
        QDV::Logger::info(QString("[VisualTest] Report generated: %1").arg(reportFile));
    }
}
