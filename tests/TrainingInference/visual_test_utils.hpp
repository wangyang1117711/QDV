#pragma once

#include <QWidget>
#include <QImage>
#include <QPainter>
#include <QColor>
#include <QDir>
#include <QDateTime>
#include <QFileInfo>
#include <QRect>
#include <QPoint>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include <QTreeWidget>
#include <QListWidget>
#include <QComboBox>
#include <optional>
#include <vector>
#include <string>

// 视觉验证结果结构
struct VisualVerificationResult {
    bool passed = false;
    QString message;
    QImage screenshot;  // 截图保存
    std::vector<QRect> detectedRegions;  // 检测到的区域
    double confidence = 0.0;  // 置信度
};

// UI元素描述结构
struct UIElementDescriptor {
    QString elementType;  // "checkbox", "button", "label", etc.
    QString objectName;   // Qt objectName
    QColor expectedColor; // 期望颜色
    bool expectedVisible = true;
    QString expectedText;
    QRect approxPosition; // 大致位置
};

class VisualTestUtils {
public:
    // 构造函数
    explicit VisualTestUtils(const QString& outputDir = "test_output");
    
    // 截图捕获
    QImage captureScreenshot(QWidget* widget, const QString& name = "screenshot");
    QImage captureRegion(QWidget* widget, const QRect& rect, const QString& name = "region");
    
    // 保存截图
    QString saveScreenshot(const QImage& image, const QString& name);
    
    // 视觉检测功能
    VisualVerificationResult verifyCheckboxExists(
        QWidget* parent, 
        const QString& objectName = "",
        int index = -1
    );
    
    VisualVerificationResult verifyPushButtonExists(
        QWidget* parent,
        const QString& buttonText,
        bool checkEnabled = true
    );
    
    VisualVerificationResult verifyComboBoxSelection(
        QComboBox* comboBox,
        const QString& expectedSelection
    );
    
    VisualVerificationResult verifyCategoryAddButtons(
        QTreeWidget* categoryTree,
        bool shouldBeEnabled = false
    );
    
    // 复选框状态验证 - 通过QCheckBox对象
    VisualVerificationResult verifyCheckboxState(
        QCheckBox* checkbox,
        bool expectedChecked
    );
    
    // 通过图像分析验证复选框存在
    VisualVerificationResult verifyCheckboxByVisualAnalysis(
        QWidget* container,
        const QPoint& approximatePosition,
        double tolerance = 0.3
    );
    
    // 全选/取消全选按钮验证
    VisualVerificationResult verifySelectAllButtonState(
        QPushButton* button,
        const QString& expectedText
    );
    
    // 选择计数验证
    VisualVerificationResult verifySelectionCount(
        QLabel* countLabel,
        const QString& expectedPattern  // e.g., "已选: 2/5"
    );
    
    // 颜色检测辅助
    bool detectColorPresence(const QImage& image, QColor color, QRect searchArea, double threshold = 0.05);
    QRect findElementByColor(const QImage& image, QColor targetColor, int minSize = 10);
    
    // 文本检测（OCR模拟 - 通过Qt分析）
    bool verifyTextOnImage(const QImage& image, const QString& expectedText, const QRect& region);
    
    // 综合验证 - 复选框功能完整流程
    struct CheckboxFlowVerification {
        VisualVerificationResult beforeCheck;
        VisualVerificationResult afterCheck;
        VisualVerificationResult dataConsistency;
        bool overallPassed = false;
        QString summary;
    };
    
    CheckboxFlowVerification verifyCheckboxFunctionality(
        QCheckBox* checkbox,
        QWidget* parentWidget,
        const std::function<void()>& setDataChecked,
        const std::function<bool()>& getDataChecked
    );
    
    // 生成验证报告
    void generateVerificationReport(const QString& testName, const VisualVerificationResult& result);
    
    // 获取输出目录
    QString getOutputDir() const { return m_outputDir; }
    
private:
    QString m_outputDir;
    int m_screenshotCounter = 0;
    
    void ensureOutputDir();
    QString generateScreenshotFilename(const QString& baseName);
    QColor getWidgetPixelColor(QWidget* widget, const QPoint& pos);
    bool isColorSimilar(QColor c1, QColor c2, int tolerance = 30);
};
