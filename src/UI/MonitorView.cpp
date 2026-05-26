#include "MonitorView.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QToolBar>
#include <QAction>
#include <QLabel>
#include <QProgressBar>
#include <QTimer>
#include <QGroupBox>
#include <QGridLayout>
#include <QRandomGenerator>

MonitorView::MonitorView(QWidget* parent) : QWidget(parent) {
    setupUI();
    m_frameTimer = new QTimer(this);
    connect(m_frameTimer, &QTimer::timeout, [this]() {
        m_detectionCount += QRandomGenerator::global()->bounded(1, 6);
        m_alertCount += QRandomGenerator::global()->bounded(0, 2);
        emit detectionCountChanged(m_detectionCount);
        emit alertCountChanged(m_alertCount);
    });
    m_frameTimer->start(2000);
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

    QAction* startAction = toolbar->addAction("开始检测");
    QAction* stopAction = toolbar->addAction("停止检测");
    stopAction->setEnabled(false);
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

    QLabel* statusLabel = new QLabel("运行中");
    statusLabel->setStyleSheet("color: #4CAF50; font-size: 18px; font-weight: bold;");
    statusLayout->addWidget(new QLabel("状态:"), 0, 0);
    statusLayout->addWidget(statusLabel, 0, 1);

    QLabel* throughputLabel = new QLabel("0 件/分钟");
    throughputLabel->setStyleSheet("font-size: 14px;");
    statusLayout->addWidget(new QLabel("检测速度:"), 1, 0);
    statusLayout->addWidget(throughputLabel, 1, 1);

    QLabel* passRateLabel = new QLabel("99.8%");
    passRateLabel->setStyleSheet("color: #4CAF50; font-size: 14px; font-weight: bold;");
    statusLayout->addWidget(new QLabel("良品率:"), 2, 0);
    statusLayout->addWidget(passRateLabel, 2, 1);

    contentLayout->addWidget(statusGroup);

    QProgressBar* progressBar = new QProgressBar();
    progressBar->setRange(0, 100);
    progressBar->setValue(78);
    progressBar->setTextVisible(true);
    progressBar->setFormat("批次进度: %p%");
    progressBar->setFixedHeight(24);
    contentLayout->addWidget(progressBar);

    QGroupBox* countGroup = new QGroupBox("检测计数");
    QGridLayout* countLayout = new QGridLayout(countGroup);
    countLayout->setSpacing(12);

    QLabel* totalLabel = new QLabel("0");
    totalLabel->setStyleSheet("font-size: 24px; font-weight: bold; color: #42A5F5;");
    countLayout->addWidget(new QLabel("检测总数:"), 0, 0);
    countLayout->addWidget(totalLabel, 0, 1);

    QLabel* passLabel = new QLabel("0");
    passLabel->setStyleSheet("font-size: 24px; font-weight: bold; color: #4CAF50;");
    countLayout->addWidget(new QLabel("合格数:"), 1, 0);
    countLayout->addWidget(passLabel, 1, 1);

    QLabel* failLabel = new QLabel("0");
    failLabel->setStyleSheet("font-size: 24px; font-weight: bold; color: #F44336;");
    countLayout->addWidget(new QLabel("不合格数:"), 2, 0);
    countLayout->addWidget(failLabel, 2, 1);

    contentLayout->addWidget(countGroup);

    contentLayout->addStretch();
    mainLayout->addWidget(contentWidget);

    connect(startAction, &QAction::triggered, [startAction, stopAction, progressBar, totalLabel, passLabel, failLabel, throughputLabel]() {
        startAction->setEnabled(false);
        stopAction->setEnabled(true);
        int total = QRandomGenerator::global()->bounded(50, 200);
        int pass = total - QRandomGenerator::global()->bounded(0, 10);
        totalLabel->setText(QString::number(total));
        passLabel->setText(QString::number(pass));
        failLabel->setText(QString::number(total - pass));
        throughputLabel->setText(QString::number(QRandomGenerator::global()->bounded(10, 50)) + " 件/分钟");
        progressBar->setValue(100);
    });

    connect(stopAction, &QAction::triggered, [startAction, stopAction]() {
        startAction->setEnabled(true);
        stopAction->setEnabled(false);
    });

    connect(resetAction, &QAction::triggered, [this, totalLabel, passLabel, failLabel, progressBar, throughputLabel]() {
        m_detectionCount = 0;
        m_alertCount = 0;
        totalLabel->setText("0");
        passLabel->setText("0");
        failLabel->setText("0");
        throughputLabel->setText("0 件/分钟");
        progressBar->setValue(0);
        emit detectionCountChanged(0);
        emit alertCountChanged(0);
    });

    connect(this, &MonitorView::detectionCountChanged, [totalLabel, passLabel](int count) {
        totalLabel->setText(QString::number(count));
        passLabel->setText(QString::number(count * 95 / 100));
    });

    connect(this, &MonitorView::alertCountChanged, [failLabel](int count) {
        failLabel->setText(QString::number(count));
    });
}