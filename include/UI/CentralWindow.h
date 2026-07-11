#ifndef CENTRAL_WINDOW_H
#define CENTRAL_WINDOW_H

#include <QWidget>
#include <QStackedWidget>
#include <QToolButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QMap>
#include <QEvent>
#include <QLabel>

struct DetectionStats;

class EditView;
class CameraView;
class SchemeView;
class IOView;
class CommView;
class MonitorView;
class TrainingInferenceView;

class CentralWindow : public QWidget {
    Q_OBJECT
    
public:
    explicit CentralWindow(QWidget* parent = nullptr);
    ~CentralWindow();

public slots:
    void updateSchemeCount(int count);
    void updateToolCount(int count);
    void updateDetectionCount(int count);
    void updateAlertCount(int count);
    void onDetectionResult(const DetectionStats& stats);
    
signals:
    void logout();
    void viewChanged(const QString& viewName);
    void statusMessageRequested(const QString& message);  // v5.0：视图切换状态栏提示
    
private slots:
    void onLogoutClicked();

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    void createNavigationPanel(QVBoxLayout* mainLayout);
    QToolButton* createNavButton(const QString& text, int index);
    void createContentViews();
    QWidget* createHomeView();
    QWidget* createStatCard(const QString& title, const QString& value, const QString& color);
    QWidget* createStatCardEx(const QString& title, const QString& value, const QString& color, QLabel*& outValueLabel);
    QWidget* createStepCard(const QString& number, const QString& title, const QString& description, int targetViewIndex);
    void switchView(int index);
    
    QStackedWidget* m_contentStack;
    QMap<int, QToolButton*> navButtons;
    QMap<QWidget*, int> m_stepCardTargets;

    QLabel* m_statSchemeValue = nullptr;
    QLabel* m_statToolValue = nullptr;
    QLabel* m_statDetectionValue = nullptr;
    QLabel* m_statAlertValue = nullptr;

    CameraView* m_cameraView;
    SchemeView* m_schemeView;
    EditView* m_editView;
    IOView* m_ioView;
    CommView* m_commView;
    MonitorView* m_monitorView;
    TrainingInferenceView* m_trainingView;
};

#endif // CENTRAL_WINDOW_H