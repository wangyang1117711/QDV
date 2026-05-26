#ifndef MAIN_WINDOW_H
#define MAIN_WINDOW_H

#include <QMainWindow>
#include <QStackedWidget>
#include <QCloseEvent>
#include <QShowEvent>

class LoginView;
class CentralWindow;

class MainWindow : public QMainWindow {
    Q_OBJECT
    
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();
    
    void showLogin();
    void showMain();
    
protected:
    void closeEvent(QCloseEvent* event) override;
    void showEvent(QShowEvent* event) override;
    
private slots:
    void onLoginSuccess();
    void onLogout();
    
private:
    void createTitleBar();
    void createMenuBar();
    void createStatusBar();
    void animateViewTransition(int targetIndex);
    void restoreWindowState();
    void saveWindowState();
    void showFirstRunSetup();
    
    QStackedWidget* m_stackedWidget;
    LoginView* m_loginView;
    CentralWindow* m_centralWindow;
};

#endif // MAIN_WINDOW_H