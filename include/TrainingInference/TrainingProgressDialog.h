#ifndef TRAININGPROGRESSDIALOG_H
#define TRAININGPROGRESSDIALOG_H

#include <QDialog>
#include <QProgressBar>
#include <QLabel>
#include <QPushButton>
#include <QElapsedTimer>
#include <QVariantMap>

// --- TrainingProgressDialog ---
// 训练进度对话框：显示epoch进度、预估剩余时间、实时训练/验证指标，
// 并通过取消按钮允许用户中止训练。
class TrainingProgressDialog : public QDialog {
    Q_OBJECT

public:
    explicit TrainingProgressDialog(int totalEpochs, QWidget* parent = nullptr);

    // 由外部（TrainingBridge信号）驱动，更新对话框内的各项指标
    void updateProgress(const QVariantMap& progress);

signals:
    // 用户点击取消按钮时发射
    void cancelled();

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void setupUI();
    QString formatTime(qint64 seconds) const;

    // --- UI控件 ---
    QProgressBar* m_epochProgressBar;
    QLabel* m_epochLabel;          // "Epoch 3 / 20"
    QLabel* m_elapsedLabel;        // 已用时间
    QLabel* m_remainingLabel;      // 预估剩余时间

    // 实时指标
    QLabel* m_trainLossLabel;
    QLabel* m_valLossLabel;
    QLabel* m_trainAccLabel;
    QLabel* m_valAccLabel;

    QPushButton* m_cancelBtn;

    // --- 内部状态 ---
    QElapsedTimer m_timer;
    int m_totalEpochs;
    int m_currentEpoch;            // 当前完成的epoch（用于时间估算）
};

#endif // TRAININGPROGRESSDIALOG_H