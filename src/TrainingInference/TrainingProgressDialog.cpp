#include "TrainingInference/TrainingProgressDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QCloseEvent>
#include <QTimer>
#include <QTime>

TrainingProgressDialog::TrainingProgressDialog(int totalEpochs, QWidget* parent)
    : QDialog(parent)
    , m_totalEpochs(totalEpochs)
    , m_currentEpoch(0)
{
    setupUI();
    setWindowTitle("训练进度");
    setMinimumSize(420, 320);
    setModal(false);  // 非模态，允许用户查看主界面

    // 启动计时器
    m_timer.start();
}

void TrainingProgressDialog::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(12);
    mainLayout->setContentsMargins(16, 16, 16, 16);

    // --- Epoch进度区域 ---
    QGroupBox* epochGroup = new QGroupBox("训练进度");
    QVBoxLayout* epochLayout = new QVBoxLayout(epochGroup);

    m_epochLabel = new QLabel("准备中...");
    m_epochLabel->setStyleSheet("font-size: 16px; font-weight: bold; color: #e0e0e0;");
    m_epochLabel->setAlignment(Qt::AlignCenter);
    epochLayout->addWidget(m_epochLabel);

    m_epochProgressBar = new QProgressBar();
    m_epochProgressBar->setRange(0, m_totalEpochs);
    m_epochProgressBar->setValue(0);
    m_epochProgressBar->setTextVisible(true);
    m_epochProgressBar->setStyleSheet(R"(
        QProgressBar {
            border: 1px solid #555;
            border-radius: 4px;
            background-color: #252525;
            text-align: center;
            color: #e0e0e0;
            font-size: 12px;
            height: 20px;
        }
        QProgressBar::chunk {
            background-color: #660874;
            border-radius: 3px;
        }
    )");
    epochLayout->addWidget(m_epochProgressBar);

    // 时间信息
    QHBoxLayout* timeLayout = new QHBoxLayout();
    m_elapsedLabel = new QLabel("已用时: --");
    m_elapsedLabel->setStyleSheet("color: #aaa; font-size: 12px;");
    m_remainingLabel = new QLabel("预估剩余: --");
    m_remainingLabel->setStyleSheet("color: #aaa; font-size: 12px;");
    timeLayout->addWidget(m_elapsedLabel);
    timeLayout->addStretch();
    timeLayout->addWidget(m_remainingLabel);
    epochLayout->addLayout(timeLayout);

    mainLayout->addWidget(epochGroup);

    // --- 实时指标区域 ---
    QGroupBox* metricsGroup = new QGroupBox("实时指标");
    QFormLayout* metricsLayout = new QFormLayout(metricsGroup);
    metricsLayout->setLabelAlignment(Qt::AlignRight);

    auto makeMetricLabel = []() -> QLabel* {
        QLabel* lbl = new QLabel("--");
        lbl->setStyleSheet("font-size: 14px; font-weight: bold; color: #e0e0e0;");
        lbl->setAlignment(Qt::AlignLeft);
        return lbl;
    };

    m_trainLossLabel = makeMetricLabel();
    m_valLossLabel   = makeMetricLabel();
    m_trainAccLabel  = makeMetricLabel();
    m_valAccLabel    = makeMetricLabel();

    metricsLayout->addRow("Train Loss:", m_trainLossLabel);
    metricsLayout->addRow("Val Loss:",   m_valLossLabel);
    metricsLayout->addRow("Train Acc:",  m_trainAccLabel);
    metricsLayout->addRow("Val Acc:",    m_valAccLabel);

    mainLayout->addWidget(metricsGroup);
    mainLayout->addStretch();

    // --- 取消按钮 ---
    QHBoxLayout* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    m_cancelBtn = new QPushButton("取消训练");
    m_cancelBtn->setStyleSheet(R"(
        QPushButton {
            background-color: #c0392b;
            color: white;
            border: none;
            border-radius: 4px;
            padding: 8px 24px;
            font-size: 13px;
            font-weight: bold;
        }
        QPushButton:hover {
            background-color: #e74c3c;
        }
        QPushButton:pressed {
            background-color: #a93226;
        }
        QPushButton:disabled {
            background-color: #555;
            color: #888;
        }
    )");
    connect(m_cancelBtn, &QPushButton::clicked, this, [this]() {
        m_cancelBtn->setEnabled(false);
        m_cancelBtn->setText("取消中...");
        emit cancelled();
    });
    btnLayout->addWidget(m_cancelBtn);
    mainLayout->addLayout(btnLayout);
}

void TrainingProgressDialog::updateProgress(const QVariantMap& progress)
{
    int epoch = progress.value("epoch", 0).toInt();
    int total = progress.value("totalEpochs", m_totalEpochs).toInt();

    // 更新epoch总数（训练过程中可能不会变，但做兼容处理）
    if (total != m_totalEpochs) {
        m_totalEpochs = total;
        m_epochProgressBar->setRange(0, total);
    }
    m_currentEpoch = epoch;

    // 更新epoch标签与进度条
    m_epochLabel->setText(QString("Epoch %1 / %2").arg(epoch).arg(total));
    m_epochProgressBar->setValue(epoch);

    // 计算并更新预估剩余时间
    qint64 elapsedSec = m_timer.elapsed() / 1000;
    m_elapsedLabel->setText(QString("已用时: %1").arg(formatTime(elapsedSec)));

    if (epoch > 0 && epoch < total) {
        // 线性外推：已完成的epoch数对应的平均每epoch时间
        qint64 remainSec = static_cast<qint64>(
            static_cast<double>(elapsedSec) / epoch * (total - epoch));
        m_remainingLabel->setText(QString("预估剩余: %1").arg(formatTime(remainSec)));
    } else if (epoch >= total) {
        m_remainingLabel->setText("已完成");
    }

    // 更新实时指标
    // train_loss / val_loss
    if (progress.contains("trainLoss")) {
        double tl = progress["trainLoss"].toDouble();
        m_trainLossLabel->setText(QString::number(tl, 'f', 4));
    }
    if (progress.contains("valLoss")) {
        double vl = progress["valLoss"].toDouble();
        m_valLossLabel->setText(QString::number(vl, 'f', 4));
    }
    // train_acc / val_acc（兼容百分比 [0-100] 或小数 [0-1]）
    if (progress.contains("trainAccuracy")) {
        double ta = progress["trainAccuracy"].toDouble();
        // 如果是0-1范围的小数，转换为百分比显示
        if (ta <= 1.0 && ta >= 0.0) {
            m_trainAccLabel->setText(QString("%1%").arg(QString::number(ta * 100.0, 'f', 2)));
        } else {
            m_trainAccLabel->setText(QString("%1%").arg(QString::number(ta, 'f', 2)));
        }
    }
    if (progress.contains("valAccuracy")) {
        double va = progress["valAccuracy"].toDouble();
        if (va <= 1.0 && va >= 0.0) {
            m_valAccLabel->setText(QString("%1%").arg(QString::number(va * 100.0, 'f', 2)));
        } else {
            m_valAccLabel->setText(QString("%1%").arg(QString::number(va, 'f', 2)));
        }
    }
}

void TrainingProgressDialog::closeEvent(QCloseEvent* event)
{
    // 训练进行中，拦截关闭事件，提示用户使用取消按钮
    if (m_cancelBtn && m_cancelBtn->isEnabled() && m_currentEpoch < m_totalEpochs) {
        event->ignore();
        return;
    }
    QDialog::closeEvent(event);
}

QString TrainingProgressDialog::formatTime(qint64 seconds) const
{
    if (seconds < 0) seconds = 0;
    qint64 h = seconds / 3600;
    qint64 m = (seconds % 3600) / 60;
    qint64 s = seconds % 60;

    if (h > 0) {
        return QString("%1时%2分%3秒").arg(h).arg(m, 2, 10, QChar('0')).arg(s, 2, 10, QChar('0'));
    } else if (m > 0) {
        return QString("%1分%2秒").arg(m).arg(s, 2, 10, QChar('0'));
    } else {
        return QString("%1秒").arg(s);
    }
}