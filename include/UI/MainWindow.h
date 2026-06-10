#ifndef MAIN_WINDOW_H
#define MAIN_WINDOW_H

#include <QMainWindow>
#include <QStackedWidget>
#include <QCloseEvent>
#include <QShowEvent>
#include <QPushButton>
#include <QLabel>

class LoginView;
class CentralWindow;

class MainWindow : public QMainWindow {
    Q_OBJECT
    
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();
    
    void showFirstRunSetup();
    void showLogin();
    void showMain();
    
protected:
    void closeEvent(QCloseEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void changeEvent(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;

private slots:
    // v2.1.0 M4 修复：与 LoginView::loginSuccess(const QString&) 签名匹配
    // 之前 onLoginSuccess() 无参数，导致 Qt 严格匹配下信号-槽连接永不触发，
    // showMain() 永远不被调用（症状：登录后无任何切换，直接"看似退出"）。
    void onLoginSuccess(const QString& username);
    void onLogout();
    void onMinimize();
    void onMaximizeRestore();
    void onCloseWindow();
    void updateMaxRestoreIcon();

public slots:
    /** v2.1.0：动态更新标题栏方案名（不修改主窗口标题，避免任务栏重复） */
    void setCurrentSchemeName(const QString& name);

private:
    void createTitleBar();
    void createMenuBar();
    void createStatusBar();
    void animateViewTransition(int targetIndex);
    void restoreWindowState();
    void saveWindowState();
    
    QStackedWidget* m_stackedWidget;
    LoginView* m_loginView;
    CentralWindow* m_centralWindow = nullptr;

    QPushButton* m_minimizeBtn = nullptr;
    QPushButton* m_maximizeBtn = nullptr;
    QPushButton* m_closeBtn = nullptr;

    QLabel*     m_titleLabel = nullptr;   ///< 标题栏方案名标签（用于动态更新）
    QString     m_currentSchemeName;       ///< 当前方案名缓存
};

#endif // MAIN_WINDOW_H