#include "ZeroShotResultPanel.h"
#include "ZeroShotKit/Logger.h"

#include <QPainter>
#include <QFont>
#include <QHeaderView>
#include <QGroupBox>
#include <QLinearGradient>
#include <QResizeEvent>
#include <QScrollArea>

#include <opencv2/imgproc.hpp>
#include <algorithm>  // std::max

using namespace QDVMini;

// ============================================================================
// AnomalyGaugeWidget 实现
// ============================================================================

AnomalyGaugeWidget::AnomalyGaugeWidget(QWidget* parent) : QWidget(parent) {
    // 固定最小尺寸，保证仪表盘足够可见
    setMinimumSize(120, 120);
}

void AnomalyGaugeWidget::setValue(double value) {
    // 限制到 [0.0, 1.0]
    if (value < 0.0) value = 0.0;
    if (value > 1.0) value = 1.0;
    if (m_value != value) {
        m_value = value;
        update();  // 触发重绘
    }
}

void AnomalyGaugeWidget::setThreshold(double t) {
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;
    if (m_threshold != t) {
        m_threshold = t;
        update();
    }
}

void AnomalyGaugeWidget::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);  // 抗锯齿

    const int side = qMin(width(), height());
    // 居中绘制区域
    const QRectF baseRect((width() - side) / 2.0, (height() - side) / 2.0, side, side);
    // 内缩留出边距，避免弧线贴边
    const QRectF arcRect = baseRect.adjusted(12, 12, -12, -12);

    // 270 度弧形：起始角 225°（Qt 逆时针为正，对应左下 7:30 位置）
    // 顺时针扫过 270°（spanAngle 取负值），终止于右下 4:30 位置
    // Qt 角度单位为 1/16 度
    const int startAngle = 225 * 16;
    const int totalSpan = 270 * 16;

    // --- 背景弧（灰色） ---
    QPen bgPen(QColor("#3a3a3e"), 10, Qt::SolidLine, Qt::RoundCap);
    painter.setPen(bgPen);
    painter.drawArc(arcRect, startAngle, -totalSpan);

    // --- 值弧（根据阈值决定颜色：正常=绿色，超阈值=红色） ---
    const QColor valueColor = (m_value > m_threshold) ? QColor("#f44747") : QColor("#4ec9b0");
    QPen valuePen(valueColor, 10, Qt::SolidLine, Qt::RoundCap);
    painter.setPen(valuePen);
    // 值弧扫过角度 = 总跨度 × 当前值（负值表示顺时针）
    const int valueSpan = static_cast<int>(-totalSpan * m_value);
    if (valueSpan != 0) {
        painter.drawArc(arcRect, startAngle, valueSpan);
    }

    // --- 中心数值文本 ---
    painter.setPen(QColor("#dcdcaa"));
    QFont font = painter.font();
    font.setPointSize(14);
    font.setBold(true);
    painter.setFont(font);
    painter.drawText(arcRect, Qt::AlignCenter, QString::number(m_value, 'f', 2));
}

// ============================================================================
// ZeroShotResultPanel 实现
// ============================================================================

ZeroShotResultPanel::ZeroShotResultPanel(QWidget* parent) : QWidget(parent) {
    setupUI();
    updateDisplay();  // 初始为空状态
}

// ============================================================================
// UI 构建
// ============================================================================
void ZeroShotResultPanel::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(8);

    // --- 堆栈窗口：切换空状态 / 结果展示 ---
    m_stack = new QStackedWidget(this);

    // 页 0：空状态提示
    m_emptyLabel = new QLabel(tr("暂无零样本推理结果"), m_stack);
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setWordWrap(true);
    m_emptyLabel->setStyleSheet(
        "color: #888; font-size: 13px; background-color: #252525; "
        "border: 1px solid #3a3a3e; border-radius: 4px; padding: 24px;");
    m_stack->addWidget(m_emptyLabel);

    // 页 1：结果展示
    QWidget* resultPage = new QWidget(m_stack);
    QVBoxLayout* resultLayout = new QVBoxLayout(resultPage);
    resultLayout->setContentsMargins(0, 0, 0, 0);
    resultLayout->setSpacing(8);

    // --- 顶部汇总条 ---
    m_summaryLabel = new QLabel(tr("暂无汇总数据"), resultPage);
    m_summaryLabel->setStyleSheet(
        "color: #dcdcaa; font-size: 12px; padding: 6px; "
        "background-color: #252525; border: 1px solid #3a3a3e; border-radius: 4px;");
    m_summaryLabel->setWordWrap(true);
    resultLayout->addWidget(m_summaryLabel);

    // --- 主体：左侧缩略图列表 + 右侧详情 ---
    QHBoxLayout* bodyLayout = new QHBoxLayout();
    bodyLayout->setSpacing(8);

    // 左侧缩略图列表
    m_thumbnailList = new QListWidget(resultPage);
    m_thumbnailList->setFixedWidth(160);
    m_thumbnailList->setToolTip(tr("点击切换图像结果"));
    connect(m_thumbnailList, &QListWidget::itemClicked,
            this, &ZeroShotResultPanel::onThumbnailClicked);
    bodyLayout->addWidget(m_thumbnailList);

    // 右侧详情容器
    QWidget* detailPage = new QWidget(resultPage);
    QVBoxLayout* detailLayout = new QVBoxLayout(detailPage);
    detailLayout->setContentsMargins(0, 0, 0, 0);
    detailLayout->setSpacing(8);

    // --- 顶部：异常分数仪表（左） + 分类结果（右） ---
    QHBoxLayout* topLayout = new QHBoxLayout();
    topLayout->setSpacing(12);

    // 异常分数仪表 + 数值文本
    QVBoxLayout* gaugeLayout = new QVBoxLayout();
    gaugeLayout->setAlignment(Qt::AlignCenter);
    m_anomalyGauge = new AnomalyGaugeWidget(detailPage);
    // 通俗解读：分数 0~1，越高越可能是缺陷；指针超过红色刻度线即判定为异常
    m_anomalyGauge->setToolTip(
        tr("异常分数（0~1）：分数越高，越可能是缺陷。\n指针超过红色刻度线（阈值）即判定为异常。"));
    gaugeLayout->addWidget(m_anomalyGauge, 0, Qt::AlignCenter);
    m_anomalyScoreLabel = new QLabel("0.00", detailPage);
    m_anomalyScoreLabel->setAlignment(Qt::AlignCenter);
    m_anomalyScoreLabel->setStyleSheet("color: #dcdcaa; font-size: 12px;");
    gaugeLayout->addWidget(m_anomalyScoreLabel);
    topLayout->addLayout(gaugeLayout);

    // 分类结果（category + confidence）
    QGroupBox* categoryBox = new QGroupBox(tr("分类结果"), detailPage);
    QFormLayout* categoryForm = new QFormLayout(categoryBox);
    categoryForm->setSpacing(6);
    m_categoryLabel = new QLabel("-", categoryBox);
    m_categoryLabel->setStyleSheet("font-weight: bold; color: #4ec9b0;");
    m_categoryLabel->setWordWrap(true);
    m_confidenceLabel = new QLabel("-", categoryBox);
    m_confidenceLabel->setStyleSheet("color: #dcdcaa;");
    categoryForm->addRow(tr("类别:"), m_categoryLabel);
    categoryForm->addRow(tr("置信度:"), m_confidenceLabel);
    topLayout->addWidget(categoryBox, 1);

    detailLayout->addLayout(topLayout);

    // --- 检测效果图（原图 + 检测框/掩码叠加，直观查看检测效果） ---
    QGroupBox* effectBox = new QGroupBox(tr("检测效果图"), detailPage);
    QVBoxLayout* effectLayout = new QVBoxLayout(effectBox);
    effectLayout->setContentsMargins(6, 6, 6, 6);
    m_effectImageLabel = new QLabel(tr("暂无效果图"), effectBox);
    m_effectImageLabel->setAlignment(Qt::AlignCenter);
    m_effectImageLabel->setMinimumSize(240, 180);
    m_effectImageLabel->setStyleSheet(
        "background-color: #1c1c1e; color: #666; border: 1px solid #3a3a3e; "
        "border-radius: 4px; font-size: 12px;");
    m_effectImageLabel->setScaledContents(false);
    effectLayout->addWidget(m_effectImageLabel);
    detailLayout->addWidget(effectBox);

    // --- 检测框列表 ---
    QGroupBox* detectionBox = new QGroupBox(tr("检测结果"), detailPage);
    QVBoxLayout* detectionLayout = new QVBoxLayout(detectionBox);
    m_detectionTable = new QTableWidget(detectionBox);
    m_detectionTable->setColumnCount(6);
    m_detectionTable->setHorizontalHeaderLabels({
        tr("类别名"), tr("置信度"), tr("CX"), tr("CY"), tr("W"), tr("H")
    });
    // 通俗解读：每行是一个被检出的目标；CX/CY 为框中心坐标，W/H 为框宽高
    m_detectionTable->setToolTip(
        tr("检出的目标列表（每行一个）。\n置信度越高越可信；CX/CY 为中心坐标，W/H 为框宽高。"));
    m_detectionTable->setEditTriggers(QAbstractItemView::NoEditTriggers);  // 只读
    m_detectionTable->setAlternatingRowColors(true);
    m_detectionTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_detectionTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_detectionTable->horizontalHeader()->setStretchLastSection(true);
    m_detectionTable->setMaximumHeight(160);
    detectionLayout->addWidget(m_detectionTable);
    detailLayout->addWidget(detectionBox);

    // --- 掩码预览 + 热力图预览（并排） ---
    QHBoxLayout* previewLayout = new QHBoxLayout();
    previewLayout->setSpacing(8);

    // 掩码预览
    QGroupBox* maskBox = new QGroupBox(tr("分割掩码"), detailPage);
    QVBoxLayout* maskLayout = new QVBoxLayout(maskBox);
    m_maskPreviewLabel = new QLabel(maskBox);
    m_maskPreviewLabel->setMinimumSize(200, 200);  // 固定最小尺寸保持布局稳定
    m_maskPreviewLabel->setAlignment(Qt::AlignCenter);
    m_maskPreviewLabel->setStyleSheet(
        "background-color: #1e1e1e; border: 1px solid #3a3a3e;");
    m_maskPreviewLabel->setText(tr("无掩码"));
    // 通俗解读：白色区域 = 模型认为的目标/缺陷区域
    m_maskPreviewLabel->setToolTip(tr("分割掩码：白色区域为模型标出的目标/缺陷区域。"));
    maskLayout->addWidget(m_maskPreviewLabel);
    m_maskInfoLabel = new QLabel("-", maskBox);
    m_maskInfoLabel->setAlignment(Qt::AlignCenter);
    m_maskInfoLabel->setStyleSheet("color: #888; font-size: 11px;");
    maskLayout->addWidget(m_maskInfoLabel);
    previewLayout->addWidget(maskBox);

    // 热力图预览
    QGroupBox* heatmapBox = new QGroupBox(tr("异常热力图"), detailPage);
    QVBoxLayout* heatmapLayout = new QVBoxLayout(heatmapBox);
    m_heatmapPreviewLabel = new QLabel(heatmapBox);
    m_heatmapPreviewLabel->setMinimumSize(200, 200);
    m_heatmapPreviewLabel->setAlignment(Qt::AlignCenter);
    m_heatmapPreviewLabel->setStyleSheet(
        "background-color: #1e1e1e; border: 1px solid #3a3a3e;");
    m_heatmapPreviewLabel->setText(tr("无热力图"));
    // 通俗解读：越偏红（亮）的区域异常分数越高，越可能是缺陷
    m_heatmapPreviewLabel->setToolTip(
        tr("异常热力图：颜色越红（亮），该区域异常分数越高，越可能是缺陷。"));
    heatmapLayout->addWidget(m_heatmapPreviewLabel);
    previewLayout->addWidget(heatmapBox);

    detailLayout->addLayout(previewLayout);

    // --- 性能指标 ---
    QGroupBox* perfBox = new QGroupBox(tr("性能指标"), detailPage);
    perfBox->setToolTip(tr("各阶段耗时（毫秒）。总耗时越短，检测越快。"));
    QFormLayout* perfForm = new QFormLayout(perfBox);
    perfForm->setSpacing(4);
    m_perfPreprocessLabel = new QLabel("-", perfBox);
    m_perfInferenceLabel = new QLabel("-", perfBox);
    m_perfPostprocessLabel = new QLabel("-", perfBox);
    m_perfTotalLabel = new QLabel("-", perfBox);
    m_perfTotalLabel->setStyleSheet("font-weight: bold; color: #4ec9b0;");
    m_backendLabel = new QLabel("-", perfBox);
    perfForm->addRow(tr("预处理:"), m_perfPreprocessLabel);
    perfForm->addRow(tr("推理:"), m_perfInferenceLabel);
    perfForm->addRow(tr("后处理:"), m_perfPostprocessLabel);
    perfForm->addRow(tr("总耗时:"), m_perfTotalLabel);
    perfForm->addRow(tr("后端:"), m_backendLabel);
    detailLayout->addWidget(perfBox);

    // --- 底部导航：上一张 + 导航信息 + 下一张 ---
    QHBoxLayout* navLayout = new QHBoxLayout();
    m_prevBtn = new QPushButton(tr("上一张"), detailPage);
    m_prevBtn->setToolTip(tr("查看上一条推理结果"));
    m_prevBtn->setEnabled(false);
    connect(m_prevBtn, &QPushButton::clicked, this, &ZeroShotResultPanel::onPrevResult);
    navLayout->addWidget(m_prevBtn);

    m_navInfoLabel = new QLabel("-", detailPage);
    m_navInfoLabel->setAlignment(Qt::AlignCenter);
    m_navInfoLabel->setStyleSheet("color: #dcdcaa; font-size: 12px;");
    navLayout->addWidget(m_navInfoLabel, 1);

    m_nextBtn = new QPushButton(tr("下一张"), detailPage);
    m_nextBtn->setToolTip(tr("查看下一条推理结果"));
    m_nextBtn->setEnabled(false);
    connect(m_nextBtn, &QPushButton::clicked, this, &ZeroShotResultPanel::onNextResult);
    navLayout->addWidget(m_nextBtn);

    detailLayout->addLayout(navLayout);

    // --- 人工复核按钮行（默认隐藏） ---
    QHBoxLayout* reviewLayout = new QHBoxLayout();
    m_confirmBtn = new QPushButton(tr("确认"), detailPage);
    m_confirmBtn->setToolTip(tr("确认当前结果准确"));
    m_confirmBtn->setStyleSheet("QPushButton { background-color: #2d4a2d; color: #4ec9b0; "
                                 "border: 1px solid #3a5a3a; padding: 4px 12px; }"
                                 "QPushButton:hover { background-color: #3d5a3d; }");
    connect(m_confirmBtn, &QPushButton::clicked, this, &ZeroShotResultPanel::onConfirmResult);
    reviewLayout->addWidget(m_confirmBtn);

    m_rejectBtn = new QPushButton(tr("拒绝"), detailPage);
    m_rejectBtn->setToolTip(tr("拒绝当前结果，记录为 bad case"));
    m_rejectBtn->setStyleSheet("QPushButton { background-color: #4a2d2d; color: #f44747; "
                                "border: 1px solid #5a3a3a; padding: 4px 12px; }"
                                "QPushButton:hover { background-color: #5a3d3d; }");
    connect(m_rejectBtn, &QPushButton::clicked, this, &ZeroShotResultPanel::onRejectResult);
    reviewLayout->addWidget(m_rejectBtn);

    // 默认隐藏，进入复核模式时由 setReviewMode() 显示
    m_confirmBtn->setVisible(false);
    m_rejectBtn->setVisible(false);
    detailLayout->addLayout(reviewLayout);

    // --- 批量进度条（默认隐藏） ---
    m_batchProgress = new QProgressBar(detailPage);
    m_batchProgress->setRange(0, 100);
    m_batchProgress->setTextVisible(true);
    m_batchProgress->setVisible(false);  // 默认隐藏，批量推理时由 setProgress 显示
    detailLayout->addWidget(m_batchProgress);

    bodyLayout->addWidget(detailPage, 1);
    resultLayout->addLayout(bodyLayout, 1);

    m_stack->addWidget(resultPage);

    // v2.1.0 界面优化：结果面板内容（效果图+仪表+表格+预览）可能高于可视区，
    // 包进滚动区，保证小窗口/低分辨率下也能完整查看，且可滚动。
    QScrollArea* scrollArea = new QScrollArea(this);
    scrollArea->setWidget(m_stack);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    mainLayout->addWidget(scrollArea);
}

// ============================================================================
// 根据当前索引更新所有显示
// ============================================================================
void ZeroShotResultPanel::updateDisplay() {
    // 结果为空 → 切换到空状态页
    if (m_results.isEmpty()) {
        m_stack->setCurrentIndex(0);
        m_prevBtn->setEnabled(false);
        m_nextBtn->setEnabled(false);
        m_navInfoLabel->setText("-");
        if (m_thumbnailList) m_thumbnailList->clear();
        return;
    }

    // 切换到结果展示页
    m_stack->setCurrentIndex(1);
    displayResult(m_results[m_currentIndex]);

    // 更新导航信息 "当前 / 总数"
    m_navInfoLabel->setText(tr("%1 / %2").arg(m_currentIndex + 1).arg(m_results.size()));

    // 更新按钮启用状态：首张禁用上一张，末张禁用下一张
    m_prevBtn->setEnabled(m_currentIndex > 0);
    m_nextBtn->setEnabled(m_currentIndex < m_results.size() - 1);

    // 同步左侧缩略图高亮与滚动
    if (m_thumbnailList) {
        m_thumbnailList->setCurrentRow(m_currentIndex);
        QListWidgetItem* currentItem = m_thumbnailList->currentItem();
        if (currentItem) {
            m_thumbnailList->scrollToItem(currentItem, QAbstractItemView::PositionAtCenter);
        }
    }
}

// ============================================================================
// 显示单条推理结果
// ============================================================================
void ZeroShotResultPanel::displayResult(const zsu::ZeroShotResult& result) {
    // 失败结果：显示错误信息，仪表显示红色 0.0
    if (!result.success) {
        m_anomalyGauge->setThreshold(0.0);  // 强制红色（任何值 > 0 即超阈值）
        m_anomalyGauge->setValue(0.0);
        m_anomalyScoreLabel->setText("0.00");
        m_categoryLabel->setText(tr("推理失败"));
        m_categoryLabel->setStyleSheet("font-weight: bold; color: #f44747;");
        m_confidenceLabel->setText(result.errorMessage.isEmpty() ? tr("未知错误") : result.errorMessage);
        m_confidenceLabel->setWordWrap(true);

        // 清空检测表与预览
        m_detectionTable->setRowCount(0);
        m_effectImageLabel->setText(tr("推理失败"));
        m_effectImageLabel->setPixmap(QPixmap());
        m_maskPreviewLabel->setText(tr("推理失败"));
        m_maskPreviewLabel->setPixmap(QPixmap());
        m_maskInfoLabel->setText("-");
        m_heatmapPreviewLabel->setText(tr("推理失败"));
        m_heatmapPreviewLabel->setPixmap(QPixmap());

        // 性能指标仍尝试显示
        m_perfPreprocessLabel->setText(QString::number(result.metrics.preprocessMs) + " ms");
        m_perfInferenceLabel->setText(QString::number(result.metrics.inferenceMs) + " ms");
        m_perfPostprocessLabel->setText(QString::number(result.metrics.postprocessMs) + " ms");
        m_perfTotalLabel->setText(QString::number(result.metrics.totalMs) + " ms");
        m_backendLabel->setText(result.metrics.backend.isEmpty() ? "-" : result.metrics.backend);

        ZSU_LOG_WARN(QString("ZeroShotResultPanel: 显示失败结果 - %1").arg(result.errorMessage));
        return;
    }

    // --- 成功结果 ---

    // 恢复分类标签样式（可能被失败状态改红）
    m_categoryLabel->setStyleSheet("font-weight: bold; color: #4ec9b0;");
    m_confidenceLabel->setStyleSheet("color: #dcdcaa;");

    // 异常分数仪表
    m_anomalyGauge->setThreshold(0.5);  // 恢复默认阈值
    m_anomalyGauge->setValue(result.anomalyScore);
    m_anomalyScoreLabel->setText(QString::number(result.anomalyScore, 'f', 2));

    // 分类结果
    m_categoryLabel->setText(result.category.isEmpty() ? tr("（无）") : result.category);

    // --- 检测效果图（原图 + 检测框/掩码/热力图叠加） ---
    {
        const QPixmap effect = buildEffectImage(result);
        if (!effect.isNull()) {
            m_effectImageLabel->setPixmap(effect);
            m_effectImageLabel->setText(QString());
        } else {
            m_effectImageLabel->setText(tr("无效果图"));
            m_effectImageLabel->setPixmap(QPixmap());
        }
    }
    m_confidenceLabel->setText(QString::number(result.confidence, 'f', 4));

    // --- 检测框表格 ---
    m_detectionTable->setRowCount(static_cast<int>(result.detections.size()));
    for (size_t i = 0; i < result.detections.size(); ++i) {
        const auto& d = result.detections[i];
        const int row = static_cast<int>(i);
        m_detectionTable->setItem(row, 0, new QTableWidgetItem(d.className));
        m_detectionTable->setItem(row, 1, new QTableWidgetItem(
            QString::number(d.confidence, 'f', 3)));
        m_detectionTable->setItem(row, 2, new QTableWidgetItem(
            QString::number(d.cx, 'f', 4)));
        m_detectionTable->setItem(row, 3, new QTableWidgetItem(
            QString::number(d.cy, 'f', 4)));
        m_detectionTable->setItem(row, 4, new QTableWidgetItem(
            QString::number(d.w, 'f', 4)));
        m_detectionTable->setItem(row, 5, new QTableWidgetItem(
            QString::number(d.h, 'f', 4)));
    }
    m_detectionTable->resizeColumnsToContents();

    // --- 掩码预览 ---
    if (!result.mask.empty()) {
        const QPixmap maskPix = matToPixmap(result.mask);
        m_maskPreviewLabel->setPixmap(maskPix);
        m_maskPreviewLabel->setText(QString());

        // 计算前景像素百分比（非零像素 / 总像素）
        cv::Mat grayMask;
        if (result.mask.channels() > 1) {
            cv::cvtColor(result.mask, grayMask, cv::COLOR_BGR2GRAY);
        } else {
            grayMask = result.mask;
        }
        const int nonZero = cv::countNonZero(grayMask);
        const int total = grayMask.rows * grayMask.cols;
        const double fgPercent = (total > 0) ? (static_cast<double>(nonZero) / total * 100.0) : 0.0;
        m_maskInfoLabel->setText(tr("前景占比: %1%").arg(fgPercent, 0, 'f', 2));
    } else {
        m_maskPreviewLabel->setPixmap(QPixmap());
        m_maskPreviewLabel->setText(tr("无掩码"));
        m_maskInfoLabel->setText("-");
    }

    // --- 热力图预览 ---
    if (!result.anomalyMap.empty()) {
        const QPixmap heatPix = applyColormap(result.anomalyMap);
        m_heatmapPreviewLabel->setPixmap(heatPix);
        m_heatmapPreviewLabel->setText(QString());
    } else {
        m_heatmapPreviewLabel->setPixmap(QPixmap());
        m_heatmapPreviewLabel->setText(tr("无热力图"));
    }

    // --- 性能指标 ---
    m_perfPreprocessLabel->setText(QString::number(result.metrics.preprocessMs) + " ms");
    m_perfInferenceLabel->setText(QString::number(result.metrics.inferenceMs) + " ms");
    m_perfPostprocessLabel->setText(QString::number(result.metrics.postprocessMs) + " ms");
    m_perfTotalLabel->setText(QString::number(result.metrics.totalMs) + " ms");
    m_backendLabel->setText(result.metrics.backend.isEmpty() ? "-" : result.metrics.backend);
}

// ============================================================================
// cv::Mat 转 QPixmap
// 处理不同通道数: 1=灰度, 3=BGR, 4=BGRA
// 转 RGB 后构造 QImage 再转 QPixmap，缩放到预览区域大小保持宽高比
// ============================================================================
QPixmap ZeroShotResultPanel::matToPixmap(const cv::Mat& mat) {
    if (mat.empty()) {
        return QPixmap();
    }

    cv::Mat rgb;
    if (mat.channels() == 1) {
        // 灰度图：若非 8 位则先归一化到 8 位
        cv::Mat gray8;
        if (mat.type() == CV_8U) {
            gray8 = mat;
        } else {
            double minVal = 0.0, maxVal = 0.0;
            cv::minMaxLoc(mat, &minVal, &maxVal);
            const double range = maxVal - minVal;
            if (range > 1e-6) {
                mat.convertTo(gray8, CV_8U, 255.0 / range, -minVal * 255.0 / range);
            } else {
                mat.convertTo(gray8, CV_8U);
            }
        }
        cv::cvtColor(gray8, rgb, cv::COLOR_GRAY2RGB);
    } else if (mat.channels() == 3) {
        cv::cvtColor(mat, rgb, cv::COLOR_BGR2RGB);
    } else if (mat.channels() == 4) {
        cv::cvtColor(mat, rgb, cv::COLOR_BGRA2RGB);
    } else {
        ZSU_LOG_WARN(QString("ZeroShotResultPanel: 不支持的通道数 %1").arg(mat.channels()));
        return QPixmap();
    }

    // 构造 QImage，必须深拷贝（rgb 为局部变量，离开作用域后数据失效）
    QImage image(rgb.data, rgb.cols, rgb.rows,
                 static_cast<int>(rgb.step), QImage::Format_RGB888);
    QPixmap pixmap = QPixmap::fromImage(image.copy());

    // 缩放到预览区域大小（200x200），保持宽高比
    return pixmap.scaled(200, 200, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

// ============================================================================
// 异常热力图伪彩色处理
// 归一化到 0-255 后应用 JET 色表，再转 QPixmap
// ============================================================================
QPixmap ZeroShotResultPanel::applyColormap(const cv::Mat& anomalyMap) {
    if (anomalyMap.empty()) {
        return QPixmap();
    }

    // 归一化到 0-255，输出 CV_8U
    cv::Mat normalized;
    cv::normalize(anomalyMap, normalized, 0, 255, cv::NORM_MINMAX, CV_8U);

    // 应用 JET 伪彩色
    cv::Mat colored;
    cv::applyColorMap(normalized, colored, cv::COLORMAP_JET);

    // 转 RGB 后转 QPixmap
    cv::Mat rgb;
    cv::cvtColor(colored, rgb, cv::COLOR_BGR2RGB);
    QImage image(rgb.data, rgb.cols, rgb.rows,
                 static_cast<int>(rgb.step), QImage::Format_RGB888);
    QPixmap pixmap = QPixmap::fromImage(image.copy());

    return pixmap.scaled(200, 200, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

// ============================================================================
// 热力图叠加到原图（保留原图结构，异常区域半透明红色高亮）
// anomalyMap 尺寸需与 source 一致（推理后处理已上采样到原图尺寸）
// ============================================================================
cv::Mat ZeroShotResultPanel::computeHeatOverlay(const cv::Mat& source, const cv::Mat& anomalyMap) {
    if (source.empty() || anomalyMap.empty()) {
        return source.clone();
    }

    // 统一到 8 位 3 通道
    cv::Mat src;
    if (source.channels() == 1) {
        cv::cvtColor(source, src, cv::COLOR_GRAY2BGR);
    } else if (source.channels() == 4) {
        cv::cvtColor(source, src, cv::COLOR_BGRA2BGR);
    } else {
        src = source.clone();
    }

    // 归一化异常图到 0-255
    cv::Mat normalized;
    cv::normalize(anomalyMap, normalized, 0, 255, cv::NORM_MINMAX, CV_8U);

    // 仅保留异常分数较高的区域（> 均值）作为高亮，避免整图泛红
    cv::Scalar meanScalar = cv::mean(normalized);
    double activeThresh = std::max<double>(meanScalar[0], 80.0);
    cv::Mat hotMask;
    cv::threshold(normalized, hotMask, activeThresh, 255, cv::THRESH_BINARY);

    // 生成红色热力图
    cv::Mat heat;
    cv::applyColorMap(normalized, heat, cv::COLORMAP_JET);

    // 仅在高亮区域叠加（半透明，alpha=0.55）
    cv::Mat overlay = src.clone();
    cv::Mat hotBGR;
    cv::cvtColor(hotMask, hotBGR, cv::COLOR_GRAY2BGR);
    cv::addWeighted(heat, 0.55, overlay, 0.45, 0.0, overlay, CV_32F);
    cv::Mat eight;
    overlay.convertTo(eight, CV_8U);
    cv::Mat result = src.clone();
    eight.copyTo(result, hotBGR);
    return result;
}

// ============================================================================
// 检测效果图：把检测框/掩码/热力图叠加到原图上，直观展示检测效果
// 优先级：原图 + 检测框 > 原图 + 热力图 > 原图 + 掩码
// ============================================================================
QPixmap ZeroShotResultPanel::buildEffectImage(const zsu::ZeroShotResult& result) {
    if (result.sourceImage.empty()) {
        // 无原图时退化为展示掩码/热力图本身
        if (!result.mask.empty()) {
            return matToPixmap(result.mask);
        }
        if (!result.anomalyMap.empty()) {
            return applyColormap(result.anomalyMap);
        }
        return QPixmap();
    }

    cv::Mat overlay = result.sourceImage.clone();
    if (overlay.channels() == 1) {
        cv::cvtColor(overlay, overlay, cv::COLOR_GRAY2BGR);
    }

    // 1) 叠加异常热力图（如果与原图同尺寸）
    if (!result.anomalyMap.empty()) {
        cv::Mat heat = computeHeatOverlay(overlay, result.anomalyMap);
        if (!heat.empty()) {
            overlay = heat;
        }
    }

    // 2) 叠加分割掩码（半透明青色，标识分割前景）
    if (!result.mask.empty()) {
        cv::Mat maskGray;
        if (result.mask.channels() > 1) {
            cv::cvtColor(result.mask, maskGray, cv::COLOR_BGR2GRAY);
        } else {
            maskGray = result.mask;
        }
        cv::resize(maskGray, maskGray, cv::Size(overlay.cols, overlay.rows));
        cv::threshold(maskGray, maskGray, 127, 255, cv::THRESH_BINARY);

        // 生成青色，仅在前景区域半透明混合
        cv::Mat maskBool;
        cv::threshold(maskGray, maskBool, 127, 255, cv::THRESH_BINARY);
        cv::Mat cyan(overlay.size(), CV_8UC3, cv::Scalar(0, 200, 200));
        cv::Mat cyanBlend;
        cv::addWeighted(cyan, 0.45, overlay, 0.55, 0, cyanBlend);
        cv::Mat cyanMask;
        cv::cvtColor(maskBool, cyanMask, cv::COLOR_GRAY2BGR);
        cyanBlend.copyTo(overlay, cyanMask);
    }

    // 3) 叠加检测框 + 类别/置信度标签
    for (const auto& d : result.detections) {
        const int w = overlay.cols;
        const int h = overlay.rows;
        const int x1 = static_cast<int>((d.cx - d.w / 2.0) * w);
        const int y1 = static_cast<int>((d.cy - d.h / 2.0) * h);
        const int x2 = static_cast<int>((d.cx + d.w / 2.0) * w);
        const int y2 = static_cast<int>((d.cy + d.h / 2.0) * h);
        cv::rectangle(overlay, cv::Point(x1, y1), cv::Point(x2, y2),
                      cv::Scalar(0, 0, 255), 2);  // 红色框

        // 标签底条
        const QString label = QString("%1 %2")
            .arg(d.className)
            .arg(d.confidence, 0, 'f', 0);
        const int labelTop = std::max(0, y1 - 18);
        cv::rectangle(overlay, cv::Point(x1, labelTop),
                      cv::Point(x1 + static_cast<int>(label.size() * 9 + 14), labelTop + 16),
                      cv::Scalar(0, 0, 255), cv::FILLED);
        // 中文标签用 cv::putText 中文可能乱码，仅画英文/数字标签
        cv::putText(overlay, label.toStdString(), cv::Point(x1 + 4, labelTop + 12),
                    cv::FONT_HERSHEY_SIMPLEX, 0.45, cv::Scalar(255, 255, 255), 1, cv::LINE_AA);
    }

    // 转 QPixmap 并等比缩放适配显示区域
    cv::Mat rgb;
    cv::cvtColor(overlay, rgb, cv::COLOR_BGR2RGB);
    QImage image(rgb.data, rgb.cols, rgb.rows,
                 static_cast<int>(rgb.step), QImage::Format_RGB888);
    QPixmap pixmap = QPixmap::fromImage(image.copy());

    // 等比缩放到效果图区域（限宽高，保持宽高比）
    const int maxW = m_effectImageLabel ? qMax(240, m_effectImageLabel->width()) : 320;
    const int maxH = m_effectImageLabel ? qMax(180, m_effectImageLabel->height()) : 220;
    return pixmap.scaled(maxW, maxH, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

// ============================================================================
// 设置单条推理结果
// ============================================================================
void ZeroShotResultPanel::setResult(const zsu::ZeroShotResult& result) {
    m_results.clear();
    m_results.append(result);
    m_currentIndex = 0;
    updateDisplay();
}

// ============================================================================
// 设置批量推理结果列表
// ============================================================================
void ZeroShotResultPanel::setResults(const QList<zsu::ZeroShotResult>& results) {
    m_results = results;
    m_currentIndex = 0;  // 重置到首张
    updateSummary();
    updateThumbnailList();
    updateDisplay();

    // 导航按钮状态在 updateDisplay() 中已更新
    ZSU_LOG_INFO(QString("ZeroShotResultPanel: 载入 %1 条推理结果").arg(results.size()));
}

// ============================================================================
// 清空结果
// ============================================================================
void ZeroShotResultPanel::clearResults() {
    m_results.clear();
    m_currentIndex = 0;
    // 清空表格与预览
    if (m_detectionTable) m_detectionTable->setRowCount(0);
    if (m_effectImageLabel) {
        m_effectImageLabel->setPixmap(QPixmap());
        m_effectImageLabel->setText(tr("暂无效果图"));
    }
    if (m_maskPreviewLabel) {
        m_maskPreviewLabel->setPixmap(QPixmap());
        m_maskPreviewLabel->setText(tr("无掩码"));
    }
    if (m_heatmapPreviewLabel) {
        m_heatmapPreviewLabel->setPixmap(QPixmap());
        m_heatmapPreviewLabel->setText(tr("无热力图"));
    }
    // 隐藏进度条
    if (m_batchProgress) m_batchProgress->setVisible(false);
    // 清空汇总与缩略图
    if (m_summaryLabel) m_summaryLabel->setText(tr("暂无汇总数据"));
    if (m_thumbnailList) m_thumbnailList->clear();
    updateDisplay();
    ZSU_LOG_INFO("ZeroShotResultPanel: 已清空结果");
}

// ============================================================================
// 设置空状态引导提示（依赖注入）
// 由主项目 ZeroShotDetectView 注入新手分步指引；空 hint 时恢复默认文案
// ============================================================================
void ZeroShotResultPanel::setEmptyHint(const QString& hint) {
    m_emptyHint = hint;
    if (!m_emptyLabel) return;
    m_emptyLabel->setText(hint.isEmpty()
                          ? tr("暂无零样本推理结果")
                          : hint);
    m_emptyLabel->setAlignment(hint.isEmpty() ? Qt::AlignCenter : Qt::AlignLeft | Qt::AlignVCenter);
}

// ============================================================================
// 设置批量推理进度
// ============================================================================
void ZeroShotResultPanel::setProgress(int current, int total) {
    if (total <= 0) {
        m_batchProgress->setVisible(false);
        return;
    }
    m_batchProgress->setVisible(true);
    m_batchProgress->setRange(0, total);
    m_batchProgress->setValue(current);
    m_batchProgress->setFormat(tr("%1 / %2").arg(current).arg(total));
}

// ============================================================================
// 导航槽：上一张
// ============================================================================
void ZeroShotResultPanel::onPrevResult() {
    if (m_currentIndex > 0) {
        --m_currentIndex;
        updateDisplay();
        emit resultNavigationChanged(m_currentIndex);
    }
}

// ============================================================================
// 导航槽：下一张
// ============================================================================
void ZeroShotResultPanel::onNextResult() {
    if (m_currentIndex < m_results.size() - 1) {
        ++m_currentIndex;
        updateDisplay();
        emit resultNavigationChanged(m_currentIndex);
    }
}

// ============================================================================
// 更新顶部汇总统计
// 统计：总图像数、总目标数、各类别数量分布
// ============================================================================
void ZeroShotResultPanel::updateSummary() {
    if (!m_summaryLabel) return;

    const int totalImages = m_results.size();
    int totalDetections = 0;
    QMap<QString, int> classCounts;
    int failedCount = 0;

    for (const auto& r : m_results) {
        if (!r.success) {
            ++failedCount;
            continue;
        }
        totalDetections += static_cast<int>(r.detections.size());
        for (const auto& d : r.detections) {
            const QString name = d.className.isEmpty() ? tr("未命名") : d.className;
            classCounts[name]++;
        }
    }

    QStringList parts;
    parts << tr("图像: %1").arg(totalImages);
    parts << tr("目标: %1").arg(totalDetections);
    if (failedCount > 0) {
        parts << tr("失败: %1").arg(failedCount);
    }
    for (auto it = classCounts.begin(); it != classCounts.end(); ++it) {
        parts << QString("%1: %2").arg(it.key()).arg(it.value());
    }

    m_summaryLabel->setText(parts.join("  |  "));
}

// ============================================================================
// 更新左侧缩略图列表
// 每行显示序号与目标数，失败项标红提示
// ============================================================================
void ZeroShotResultPanel::updateThumbnailList() {
    if (!m_thumbnailList) return;

    m_thumbnailList->clear();
    for (int i = 0; i < m_results.size(); ++i) {
        const auto& r = m_results[i];
        // 优先显示原图像文件名，无文件名时回退为"图 N"
        QString text = r.imageName.isEmpty() ? tr("图 %1").arg(i + 1) : r.imageName;
        if (!r.success) {
            text += tr(" (失败)");
        } else {
            text += tr(" (%1个目标)").arg(r.detections.size());
        }

        QListWidgetItem* item = new QListWidgetItem(text);
        item->setData(Qt::UserRole, i);
        if (!r.success) {
            item->setForeground(QColor("#f44747"));
        }
        m_thumbnailList->addItem(item);
    }
}

// ============================================================================
// 点击缩略图切换当前结果
// ============================================================================
void ZeroShotResultPanel::onThumbnailClicked(QListWidgetItem* item) {
    if (!item) return;
    const int index = item->data(Qt::UserRole).toInt();
    if (index >= 0 && index < m_results.size() && index != m_currentIndex) {
        m_currentIndex = index;
        updateDisplay();
        emit resultNavigationChanged(m_currentIndex);
    }
}

// ============================================================================
// 人工复核模式控制
// ============================================================================
void ZeroShotResultPanel::setReviewMode(bool enabled) {
    m_reviewMode = enabled;
    if (m_confirmBtn) m_confirmBtn->setVisible(enabled);
    if (m_rejectBtn) m_rejectBtn->setVisible(enabled);

    // 进入复核模式时初始化复核状态
    if (enabled) {
        m_reviewedStatus.clear();
        for (int i = 0; i < m_results.size(); ++i) {
            m_reviewedStatus.append(0);  // 全部待复核
        }
    }
    updateDisplay();
}

// ============================================================================
// 确认当前结果
// ============================================================================
void ZeroShotResultPanel::onConfirmResult() {
    if (m_currentIndex < 0 || m_currentIndex >= m_results.size()) return;

    if (m_currentIndex < m_reviewedStatus.size()) {
        m_reviewedStatus[m_currentIndex] = 1;  // 已确认
    }
    emit reviewResultConfirmed(m_currentIndex, m_results[m_currentIndex]);

    // 自动跳转下一张
    if (m_currentIndex < m_results.size() - 1) {
        onNextResult();
    }
}

// ============================================================================
// 拒绝当前结果，记录为 bad case
// ============================================================================
void ZeroShotResultPanel::onRejectResult() {
    if (m_currentIndex < 0 || m_currentIndex >= m_results.size()) return;

    if (m_currentIndex < m_reviewedStatus.size()) {
        m_reviewedStatus[m_currentIndex] = 2;  // 已拒绝
    }
    emit reviewResultRejected(m_currentIndex, m_results[m_currentIndex]);

    // 自动跳转下一张
    if (m_currentIndex < m_results.size() - 1) {
        onNextResult();
    }
}
