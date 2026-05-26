#ifndef MONITOR_VIEW_H
#define MONITOR_VIEW_H

#include <QWidget>

class QTimer;

class MonitorView : public QWidget {
    Q_OBJECT

public:
    explicit MonitorView(QWidget* parent = nullptr);
    ~MonitorView();

signals:
    void detectionCountChanged(int count);
    void alertCountChanged(int count);

private:
    void setupUI();
    QTimer* m_frameTimer;
    int m_detectionCount = 0;
    int m_alertCount = 0;
};

#endif // MONITOR_VIEW_H