#include "MonitorView.h"
#include "Database/ResultDatabase.h"
#include "Database/DatabaseIntegrator.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QToolBar>
#include <QAction>
#include <QLabel>
#include <QProgressBar>
#include <QTimer>
#include <QGroupBox>
#include <QGridLayout>

MonitorView::MonitorView(QWidget* parent) : QWidget(parent) {
    setupUI();
    m_frameTimer = new QTimer(this);
    connect(m_frameTimer, &QTimer::timeout, this, &MonitorView::updateDisplay);
}

MonitorView::~MonitorView() {
    m_frameTimer->stop();
}

void MonitorView::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    QToolBar* toolbar = new QToolBar();
    toolbar->setStyleSheet(R"(
        QToolBar {
            background-color: #2d2d2d;
            border-bottom: 1px solid #444;
            padding: 4px 8px;
            spacing: 6px;
        }
        QToolBar QToolButton {
            background: transparent;
            border: 1px solid transparent;
            border-radius: 4px;
            padding: 6px 12px;
            color: #e0e0e0;
            font-size: 13px;
        }
        QToolBar QToolButton:hover {
            background-color: #555;
            border-color: #555;
        }
        QToolBar QToolButton:disabled {
            color: #666;
        }
    )");

    m_startAction = toolbar->addAction("开始检测");
    m_stopAction = toolbar->addAction("停止检测");
    m_stopAction->setEnabled(false);
    QAction* resetAction = toolbar->addAction("重置计数");
    QAction* reportAction = toolbar->addAction("生成报告");
    reportAction->setEnabled(false);
    reportAction->setToolTip("即将推出");

    mainLayout->addWidget(toolbar);

    QWidget* contentWidget = new QWidget();
    QVBoxLayout* contentLayout = new QVBoxLayout(contentWidget);
    contentLayout->setContentsMargins(16, 16, 16, 16);
    contentLayout->setSpacing(16);

    QGroupBox* statusGroup = new QGroupBox("系统状态");
    QGridLayout* statusLayout = new QGridLayout(statusGroup);
    statusLayout->setSpacing(16);

    m_statusLabel = new QLabel("就绪");
    m_statusLabel->setStyleSheet("color: #FFA726; font-size: 18px; font-weight: bold;");
    statusLayout->addWidget(new QLabel("状态:"), 0, 0);
    statusLayout->addWidget(m_statusLabel, 0, 1);

    m_throughputLabel = new QLabel("0 件/分钟");
    m_throughputLabel->setStyleSheet("font-size: 14px;");
    statusLayout->addWidget(new QLabel("检测速度:"), 1, 0);
    statusLayout->addWidget(m_throughputLabel, 1, 1);

    m_passRateLabel = new QLabel("--");
    m_passRateLabel->setStyleSheet("color: #aaa; font-size: 14px; font-weight: bold;");
    statusLayout->addWidget(new QLabel("良品率:"), 2, 0);
    statusLayout->addWidget(m_passRateLabel, 2, 1);

    contentLayout->addWidget(statusGroup);

    m_progressBar = new QProgressBar();
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setTextVisible(true);
    m_progressBar->setFormat("批次进度: %p%");
    m_progressBar->setFixedHeight(24);
    contentLayout->addWidget(m_progressBar);

    QGroupBox* countGroup = new QGroupBox("检测计数");
    QGridLayout* countLayout = new QGridLayout(countGroup);
    countLayout->setSpacing(12);

    m_totalLabel = new QLabel("0");
    m_totalLabel->setStyleSheet("font-size: 24px; font-weight: bold; color: #42A5F5;");
    countLayout->addWidget(new QLabel("检测总数:"), 0, 0);
    countLayout->addWidget(m_totalLabel, 0, 1);

    m_passLabel = new QLabel("0");
    m_passLabel->setStyleSheet("font-size: 24px; font-weight: bold; color: #4CAF50;");
    countLayout->addWidget(new QLabel("合格数:"), 1, 0);
    countLayout->addWidget(m_passLabel, 1, 1);

    m_failLabel = new QLabel("0");
    m_failLabel->setStyleSheet("font-size: 24px; font-weight: bold; color: #F44336;");
    countLayout->addWidget(new QLabel("不合格数:"), 2, 0);
    countLayout->addWidget(m_failLabel, 2, 1);

    contentLayout->addWidget(countGroup);

    contentLayout->addStretch();
    mainLayout->addWidget(contentWidget);

    connect(m_startAction, &QAction::triggered, [this]() {
        startDetection();
    });

    connect(m_stopAction, &QAction::triggered, [this]() {
        stopDetection();
    });

    connect(resetAction, &QAction::triggered, [this]() {
        resetCounters();
    });
}

void MonitorView::startDetection() {
    m_startAction->setEnabled(false);
    m_stopAction->setEnabled(true);
    m_statusLabel->setText("运行中");
    m_statusLabel->setStyleSheet("color: #4CAF50; font-size: 18px; font-weight: bold;");
    m_frameTimer->start(2000);
}

void MonitorView::stopDetection() {
    m_startAction->setEnabled(true);
    m_stopAction->setEnabled(false);
    m_frameTimer->stop();
    m_statusLabel->setText("已停止");
    m_statusLabel->setStyleSheet("color: #FFA726; font-size: 18px; font-weight: bold;");
}

void MonitorView::resetCounters() {
    m_detectionCount = 0;
    m_alertCount = 0;
    m_currentStats = DetectionStats();
    updateDisplay();
    emit detectionCountChanged(0);
    emit alertCountChanged(0);
}

void MonitorView::updateStats(const DetectionStats& stats) {
    m_currentStats = stats;
    m_detectionCount = stats.totalDetected;
    m_alertCount = stats.alerts;

    if (stats.totalDetected > 0) {
        DatabaseIntegrator::instance()->saveDetectionResult(
            "current_scheme", "current_scheme_name", stats);
    }
}

void MonitorView::updateDisplay() {
    m_totalLabel->setText(QString::number(m_currentStats.totalDetected));
    m_passLabel->setText(QString::number(m_currentStats.passed));
    m_failLabel->setText(QString::number(m_currentStats.failed));

    if (m_currentStats.totalDetected > 0) {
        m_passRateLabel->setText(
            QString::number(m_currentStats.passRate, 'f', 1) + "%");
        m_passRateLabel->setStyleSheet(
            m_currentStats.passRate >= 95.0
            ? "color: #4CAF50; font-size: 14px; font-weight: bold;"
            : "color: #F44336; font-size: 14px; font-weight: bold;");
    }

    m_throughputLabel->setText(
        QString::number(m_currentStats.throughputPerMin, 'f', 1) + " 件/分钟");

    if (!m_currentStats.status.isEmpty()) {
        m_statusLabel->setText(m_currentStats.status);
    }

    emit detectionCountChanged(m_currentStats.totalDetected);
    emit alertCountChanged(m_currentStats.alerts);
}