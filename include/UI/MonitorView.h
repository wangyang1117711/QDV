#ifndef MONITOR_VIEW_H
#define MONITOR_VIEW_H

#include <QWidget>
#include "Core/DetectionStats.h"

class QTimer;
class QLabel;
class QProgressBar;
class QAction;

class MonitorView : public QWidget {
    Q_OBJECT

public:
    explicit MonitorView(QWidget* parent = nullptr);
    ~MonitorView();

    void updateStats(const DetectionStats& stats);
    void startDetection();
    void stopDetection();
    void resetCounters();

signals:
    void detectionCountChanged(int count);
    void alertCountChanged(int count);

private:
    void setupUI();
    void updateDisplay();

    QTimer* m_frameTimer;
    QLabel* m_statusLabel = nullptr;
    QLabel* m_throughputLabel = nullptr;
    QLabel* m_passRateLabel = nullptr;
    QLabel* m_totalLabel = nullptr;
    QLabel* m_passLabel = nullptr;
    QLabel* m_failLabel = nullptr;
    QProgressBar* m_progressBar = nullptr;
    QAction* m_startAction = nullptr;
    QAction* m_stopAction = nullptr;

    DetectionStats m_currentStats;
    int m_detectionCount = 0;
    int m_alertCount = 0;
};

#endif // MONITOR_VIEW_H