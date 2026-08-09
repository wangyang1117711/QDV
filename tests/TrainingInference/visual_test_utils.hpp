#pragma once

// ============================================================================
// visual_test_utils.hpp - 视觉测试工具头文件
// 提供 VisualTestUtils 类和相关验证结果结构体声明
// ============================================================================

#include <QString>
#include <QImage>
#include <QColor>
#include <QRect>
#include <QWidget>
#include <QCheckBox>
#include <QPushButton>
#include <QComboBox>
#include <QTreeWidget>
#include <QLabel>
#include <functional>
#include <vector>

// 视觉验证结果结构体
struct VisualVerificationResult {
    bool passed = false;
    QString message;
    QImage screenshot;
    double confidence = 1.0;
    std::vector<QRect> detectedRegions;
};

// 复选框功能验证结果（前向声明，完整定义在 VisualTestUtils 类内）
// 视觉测试工具类
class VisualTestUtils {
public:
    // 复选框功能验证结果（嵌套类型，匹配 .cpp 中的 VisualTestUtils::CheckboxFlowVerification）
    struct CheckboxFlowVerification {
        VisualVerificationResult beforeCheck;
        VisualVerificationResult afterCheck;
        VisualVerificationResult dataConsistency;
        bool overallPassed = false;
        QString summary;
    };

    explicit VisualTestUtils(const QString& outputDir);

    void ensureOutputDir();
    QString generateScreenshotFilename(const QString& baseName);

    // 截图
    QImage captureScreenshot(QWidget* widget, const QString& name);
    QImage captureRegion(QWidget* widget, const QRect& rect, const QString& name);
    QString saveScreenshot(const QImage& image, const QString& name);

    // 控件存在性验证
    VisualVerificationResult verifyCheckboxExists(QWidget* parent,
                                                   const QString& objectName,
                                                   int index);
    VisualVerificationResult verifyPushButtonExists(QWidget* parent,
                                                     const QString& buttonText,
                                                     bool checkEnabled);
    VisualVerificationResult verifyComboBoxSelection(QComboBox* comboBox,
                                                      const QString& expectedSelection);
    VisualVerificationResult verifyCategoryAddButtons(QTreeWidget* categoryTree,
                                                       bool shouldBeEnabled);
    VisualVerificationResult verifyCheckboxState(QCheckBox* checkbox,
                                                  bool expectedChecked);
    VisualVerificationResult verifySelectAllButtonState(QPushButton* button,
                                                         const QString& expectedText);
    VisualVerificationResult verifySelectionCount(QLabel* countLabel,
                                                   const QString& expectedPattern);

    // 颜色检测
    bool isColorSimilar(QColor c1, QColor c2, int tolerance = 30);
    bool detectColorPresence(const QImage& image,
                             QColor targetColor,
                             QRect searchArea,
                             double threshold);
    QColor getWidgetPixelColor(QWidget* widget, const QPoint& pos);
    QRect findElementByColor(const QImage& image,
                             QColor targetColor,
                             int minSize);

    // 复选框功能完整验证
    CheckboxFlowVerification verifyCheckboxFunctionality(
        QCheckBox* checkbox,
        QWidget* parentWidget,
        const std::function<void()>& setDataChecked,
        const std::function<bool()>& getDataChecked);

    // 生成验证报告
    void generateVerificationReport(const QString& testName,
                                     const VisualVerificationResult& result);

private:
    QString m_outputDir;
    int m_screenshotCounter = 0;
};
