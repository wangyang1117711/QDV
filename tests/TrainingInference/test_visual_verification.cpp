#include "../catch2/catch2_minimal.hpp"
#include "TrainingInference/TrainingInferenceView.h"
#include "TrainingInference/ImageManager.h"
#include "TrainingInference/CategoryPanel.h"
#include "TrainingInference/CategoryManager.h"
#include "Core/Logger.h"
#include "visual_test_utils.hpp"
#include <QApplication>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QDir>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include <QComboBox>
#include <QTreeWidget>
#include <QElapsedTimer>

void ensureApp();

static QString createTempPng(QTemporaryDir& dir, const QString& name, int w, int h, QColor color)
{
    QImage img(w, h, QImage::Format_RGB32);
    img.fill(color);
    QString path = dir.filePath(name);
    img.save(path, "PNG");
    return path;
}

TEST_CASE("视觉验证 - 图像列表复选框存在性检查", "[visual:checkbox_exists]")
{
    ensureApp();
    
    VisualTestUtils visualUtils("test_visual_output");
    auto* mgr = ImageManager::instance();
    mgr->clearImages();
    
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    
    // 创建测试图片
    QStringList paths;
    for (int i = 0; i < 3; ++i) {
        paths.append(createTempPng(dir, QString("test_%1.png").arg(i), 
                                   64, 64, QColor(50 + i * 70, 100, 200)));
    }
    mgr->importImages(paths);
    REQUIRE_EQUAL(mgr->imageCount(), 3);
    
    TrainingInferenceView* view = new TrainingInferenceView();
    view->show();
    QApplication::processEvents();
    
    // 视觉验证复选框
    SECTION("验证前3个图像复选框存在")
    {
        QListWidget* list = view->findChild<QListWidget*>();
        REQUIRE(list != nullptr);
        
        for (int i = 0; i < std::min(3, list->count()); ++i) {
            QListWidgetItem* item = list->item(i);
            QWidget* itemWidget = list->itemWidget(item);
            REQUIRE(itemWidget != nullptr);
            
            QCheckBox* checkbox = itemWidget->findChild<QCheckBox*>();
            
            VisualVerificationResult result = visualUtils.verifyCheckboxExists(
                itemWidget, "", i
            );
            
            CHECK(result.passed);
            CHECK(!result.screenshot.isNull());
            
            visualUtils.generateVerificationReport(
                QString("checkbox_%1_exists").arg(i), 
                result
            );
        }
    }
    
    delete view;
    mgr->clearImages();
}

TEST_CASE("视觉验证 - 复选框状态变化完整流程", "[visual:checkbox_flow]")
{
    ensureApp();
    
    VisualTestUtils visualUtils("test_visual_output");
    auto* mgr = ImageManager::instance();
    mgr->clearImages();
    
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    
    QString path = createTempPng(dir, "flow_test.png", 64, 64, QColor(200, 100, 100));
    mgr->importImages({path});
    REQUIRE_EQUAL(mgr->imageCount(), 1);
    
    TrainingInferenceView* view = new TrainingInferenceView();
    view->show();
    QApplication::processEvents();
    
    QListWidget* list = view->findChild<QListWidget*>();
    REQUIRE(list != nullptr);
    REQUIRE_EQUAL(list->count(), 1);
    
    QListWidgetItem* item = list->item(0);
    QWidget* itemWidget = list->itemWidget(item);
    QCheckBox* checkbox = itemWidget->findChild<QCheckBox*>();
    REQUIRE(checkbox != nullptr);
    
    SECTION("完整流程验证：勾选 -> 状态更新 -> 取消勾选")
    {
        // 初始状态验证
        VisualVerificationResult initial = visualUtils.verifyCheckboxState(checkbox, false);
        CHECK(initial.passed);
        
        // 执行勾选并验证
        checkbox->click();
        QApplication::processEvents();
        
        VisualVerificationResult afterCheck = visualUtils.verifyCheckboxState(checkbox, true);
        CHECK(afterCheck.passed);
        CHECK(mgr->isSelected(path) == true);
        
        visualUtils.generateVerificationReport("after_check", afterCheck);
        
        // 取消勾选并验证
        checkbox->click();
        QApplication::processEvents();
        
        VisualVerificationResult afterUncheck = visualUtils.verifyCheckboxState(checkbox, false);
        CHECK(afterUncheck.passed);
        CHECK(mgr->isSelected(path) == false);
        
        visualUtils.generateVerificationReport("after_uncheck", afterUncheck);
    }
    
    delete view;
    mgr->clearImages();
}

TEST_CASE("视觉验证 - 全选/取消全选按钮功能", "[visual:select_all]")
{
    ensureApp();
    
    VisualTestUtils visualUtils("test_visual_output");
    auto* mgr = ImageManager::instance();
    mgr->clearImages();
    
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    
    QStringList paths;
    for (int i = 0; i < 5; ++i) {
        paths.append(createTempPng(dir, QString("sel_%1.png").arg(i), 
                                   64, 64, QColor(100, i * 40, 150)));
    }
    mgr->importImages(paths);
    
    TrainingInferenceView* view = new TrainingInferenceView();
    view->show();
    QApplication::processEvents();
    
    // 查找全选按钮
    QList<QPushButton*> buttons = view->findChildren<QPushButton*>();
    QPushButton* selectAllBtn = nullptr;
    for (QPushButton* btn : buttons) {
        if (btn->text() == "全选" || btn->text() == "取消全选") {
            selectAllBtn = btn;
            break;
        }
    }
    REQUIRE(selectAllBtn != nullptr);
    
    SECTION("验证全选按钮状态切换")
    {
        // 初始状态应该是"全选"
        VisualVerificationResult result1 = visualUtils.verifySelectAllButtonState(
            selectAllBtn, "全选"
        );
        CHECK(result1.passed);
        
        // 点击全选
        selectAllBtn->click();
        QApplication::processEvents();
        
        VisualVerificationResult result2 = visualUtils.verifySelectAllButtonState(
            selectAllBtn, "取消全选");
        CHECK(result2.passed);
        
        // 验证所有复选框都被勾选
        REQUIRE_EQUAL(mgr->selectedCount(), 5);
        
        visualUtils.generateVerificationReport("select_all_clicked", result2);
        
        // 点击取消全选
        selectAllBtn->click();
        QApplication::processEvents();
        
        VisualVerificationResult result3 = visualUtils.verifySelectAllButtonState(
            selectAllBtn, "全选");
        CHECK(result3.passed);
        REQUIRE_EQUAL(mgr->selectedCount(), 0);
    }
    
    delete view;
    mgr->clearImages();
}

TEST_CASE("视觉验证 - 选择计数显示", "[visual:selection_count]")
{
    ensureApp();
    
    VisualTestUtils visualUtils("test_visual_output");
    auto* mgr = ImageManager::instance();
    mgr->clearImages();
    
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    
    QStringList paths;
    for (int i = 0; i < 6; ++i) {
        paths.append(createTempPng(dir, QString("cnt_%1.png").arg(i), 
                                   64, 64, QColor(100, 150, 50 + i * 30)));
    }
    mgr->importImages(paths);
    
    TrainingInferenceView* view = new TrainingInferenceView();
    view->show();
    QApplication::processEvents();
    
    QLabel* countLabel = nullptr;
    for (QLabel* label : view->findChildren<QLabel*>()) {
        if (label->text().startsWith("已选:") || label->text().contains("已选")) {
            countLabel = label;
            break;
        }
    }
    REQUIRE(countLabel != nullptr);
    
    SECTION("选择计数显示验证")
    {
        // 初始状态
        VisualVerificationResult result0 = visualUtils.verifySelectionCount(
            countLabel, "已选: 0.*"
        );
        CHECK(result0.passed);
        
        // 勾选3个
        mgr->setSelected(paths[1], true);
        mgr->setSelected(paths[3], true);
        mgr->setSelected(paths[5], true);
        QApplication::processEvents();
        
        VisualVerificationResult result1 = visualUtils.verifySelectionCount(
            countLabel, "已选: 3.*"
        );
        CHECK(result1.passed);
        
        visualUtils.generateVerificationReport("selection_count_3", result1);
    }
    
    delete view;
    mgr->clearImages();
}

TEST_CASE("视觉验证 - 类别管理 '加入'按钮状态", "[visual:category_add]")
{
    ensureApp();
    
    VisualTestUtils visualUtils("test_visual_output");
    auto* mgr = ImageManager::instance();
    auto* catMgr = CategoryManager::instance();
    
    mgr->clearImages();
    
    // 创建测试类别
    QString cat1 = catMgr->createCategory("测试类别1");
    QString cat2 = catMgr->createCategory("测试类别2");
    REQUIRE(!cat1.isEmpty());
    REQUIRE(!cat2.isEmpty());
    
    TrainingInferenceView* view = new TrainingInferenceView();
    view->show();
    QApplication::processEvents();
    
    CategoryPanel* categoryPanel = view->findChild<CategoryPanel*>();
    REQUIRE(categoryPanel != nullptr);
    
    QTreeWidget* categoryTree = categoryPanel->findChild<QTreeWidget*>();
    REQUIRE(categoryTree != nullptr);
    
    SECTION("无图片选择时按钮禁用")
    {
        VisualVerificationResult result = visualUtils.verifyCategoryAddButtons(
            categoryTree, false
        );
        CHECK(result.passed);
        
        visualUtils.generateVerificationReport("category_buttons_disabled", result);
    }
    
    delete view;
    mgr->clearImages();
}

TEST_CASE("视觉验证 - 综合完整场景验证", "[visual:integration]")
{
    ensureApp();
    
    VisualTestUtils visualUtils("test_visual_output");
    auto* mgr = ImageManager::instance();
    mgr->clearImages();
    
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    
    // 创建类别和图片
    QStringList paths;
    for (int i = 0; i < 5; ++i) {
        paths.append(createTempPng(dir, QString("int_%1.png").arg(i), 
                                   64, 64, QColor(200 - i * 30, 100 + i * 20, 150)));
    }
    mgr->importImages(paths);
    
    TrainingInferenceView* view = new TrainingInferenceView();
    view->show();
    QApplication::processEvents();
    
    // 截图初始状态
    visualUtils.captureScreenshot(view, "integration_initial");
    
    SECTION("完整操作流程：导入 -> 验证")
    {
        // 查找并验证功能可用性
        CHECK(mgr->imageCount() > 0);
        
        visualUtils.captureScreenshot(view, "integration_final");
    }
    
    delete view;
    mgr->clearImages();
}
