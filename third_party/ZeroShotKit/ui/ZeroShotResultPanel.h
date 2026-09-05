#pragma once

#include <QWidget>
#include <QLabel>
#include <QTableWidget>
#include <QPushButton>
#include <QProgressBar>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QStackedWidget>
#include <QListWidget>
#include "ZeroShotKit/ZeroShotTypes.h"  // zsu::ZeroShotResult
#include <QList>

namespace QDVMini {

// 自定义异常分数仪表控件（圆形进度条）
// 绘制 270 度弧形进度条，根据阈值切换颜色（正常绿色 / 超阈值红色）
class AnomalyGaugeWidget : public QWidget {
    Q_OBJECT
public:
    explicit AnomalyGaugeWidget(QWidget* parent = nullptr);
    void setValue(double value);      // 0.0-1.0
    void setThreshold(double t);      // 超过阈值显示红色
    QSize sizeHint() const override { return QSize(120, 120); }
protected:
    void paintEvent(QPaintEvent* event) override;
private:
    double m_value = 0.0;
    double m_threshold = 0.5;
};

// 零样本推理结果展示面板
// 展示异常分数仪表、分类结果、检测框列表、掩码与热力图预览、性能指标
// 支持批量结果导航（上一张/下一张）与批量推理进度显示
class ZeroShotResultPanel : public QWidget {
    Q_OBJECT
public:
    explicit ZeroShotResultPanel(QWidget* parent = nullptr);

    // 设置单条推理结果
    void setResult(const zsu::ZeroShotResult& result);
    // 设置批量推理结果列表
    void setResults(const QList<zsu::ZeroShotResult>& results);
    // 清空结果
    void clearResults();
    // 设置进度（批量推理时）
    void setProgress(int current, int total);

    // --- 人工复核模式 ---
    void setReviewMode(bool enabled);
    bool reviewMode() const { return m_reviewMode; }

    // --- 空状态引导（依赖注入，保持 ZeroShotKit 独立于主项目） ---
    /// 设置空状态页的引导提示文本（多行纯文本）
    /// 由主项目 ZeroShotDetectView 注入新手分步指引；默认显示"暂无零样本推理结果"
    void setEmptyHint(const QString& hint);

    /// 当前是否已有可展示的推理结果（供外部引导条判断"步骤④完成"）
    bool hasResults() const { return !m_results.isEmpty(); }

signals:
    void resultNavigationChanged(int index);  // 上一张/下一张导航
    void reviewResultConfirmed(int index, const zsu::ZeroShotResult& result);
    void reviewResultRejected(int index, const zsu::ZeroShotResult& result);

private slots:
    void onPrevResult();
    void onNextResult();
    void onThumbnailClicked(QListWidgetItem* item);  // 点击缩略图切换
    void onConfirmResult();   // 确认当前结果
    void onRejectResult();    // 拒绝当前结果

private:
    void setupUI();
    void updateDisplay();          // 根据当前索引更新所有显示
    void displayResult(const zsu::ZeroShotResult& result);
    void updateSummary();          // 更新顶部汇总统计
    void updateThumbnailList();    // 更新左侧缩略图列表
    QPixmap matToPixmap(const cv::Mat& mat);  // cv::Mat 转 QPixmap
    QPixmap applyColormap(const cv::Mat& anomalyMap);  // 热力图伪彩色
    QPixmap buildEffectImage(const zsu::ZeroShotResult& result);  // 检测效果图（原图+检测框/掩码叠加）
    cv::Mat computeHeatOverlay(const cv::Mat& source, const cv::Mat& anomalyMap);  // 热力图叠加到原图

    // 数据
    QList<zsu::ZeroShotResult> m_results;
    int m_currentIndex = 0;

    // 复核状态
    bool m_reviewMode = false;
    QList<int> m_reviewedStatus;  // 0=待复核, 1=已确认, 2=已拒绝

    // UI 控件
    // 1. 异常分数仪表
    AnomalyGaugeWidget* m_anomalyGauge = nullptr;
    QLabel* m_anomalyScoreLabel = nullptr;  // 数值文本

    // 2. 分类结果
    QLabel* m_categoryLabel = nullptr;
    QLabel* m_confidenceLabel = nullptr;

    // 3. 检测框列表
    QTableWidget* m_detectionTable = nullptr;  // 列: className, confidence, cx, cy, w, h

    // 4. 掩码预览
    QLabel* m_maskPreviewLabel = nullptr;  // 显示掩码 QPixmap
    QLabel* m_maskInfoLabel = nullptr;     // 前景像素百分比

    // 5. 热力图预览
    QLabel* m_heatmapPreviewLabel = nullptr;

    // 6. 性能指标
    QLabel* m_perfPreprocessLabel = nullptr;
    QLabel* m_perfInferenceLabel = nullptr;
    QLabel* m_perfPostprocessLabel = nullptr;
    QLabel* m_perfTotalLabel = nullptr;
    QLabel* m_backendLabel = nullptr;

    // 7. 导航
    QPushButton* m_prevBtn = nullptr;
    QPushButton* m_nextBtn = nullptr;
    QLabel* m_navInfoLabel = nullptr;  // "3 / 10"

    // 8. 人工复核按钮（opt-in，默认隐藏）
    QPushButton* m_confirmBtn = nullptr;
    QPushButton* m_rejectBtn = nullptr;

    // 进度条（批量推理中）
    QProgressBar* m_batchProgress = nullptr;

    // 批量汇总与缩略图导航
    QLabel* m_summaryLabel = nullptr;     // 顶部汇总条
    QListWidget* m_thumbnailList = nullptr; // 左侧缩略图列表

    // 检测效果图（原图 + 检测框/掩码叠加）
    QLabel* m_effectImageLabel = nullptr;   // 效果图 QPixmap

    // 空状态提示
    QLabel* m_emptyLabel = nullptr;
    QString m_emptyHint;            // 外部注入的空状态引导文本（默认空 = 显示默认文案）
    QStackedWidget* m_stack = nullptr;  // 0=空状态, 1=结果展示
};

} // namespace QDVMini
