#include "MainWindow.h"
#include "LoginView.h"
#include "CentralWindow.h"
#include "AuthService.h"
#include <QPropertyAnimation>
#include <QEasingCurve>
#include <QSettings>
#include <QCloseEvent>
#include <QShowEvent>
#include <QMenuBar>
#include <QStatusBar>
#include <QToolBar>
#include <QLabel>
#include <QPixmap>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPushButton>
#include <QMessageBox>
#include <QInputDialog>
#include <QDialog>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QStackedWidget>
#include <QApplication>
#include <QLineEdit>
#include <opencv2/core/version.hpp>

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("奇测视觉检测系统 v1.0");
    setMinimumSize(1200, 800);

    createTitleBar();
    createMenuBar();
    createStatusBar();

    m_stackedWidget = new QStackedWidget(this);
    setCentralWidget(m_stackedWidget);

    m_loginView = new LoginView();
    m_centralWindow = new CentralWindow();

    m_stackedWidget->addWidget(m_loginView);
    m_stackedWidget->addWidget(m_centralWindow);

    connect(m_loginView, &LoginView::loginSuccess, this, &MainWindow::onLoginSuccess);
    connect(m_centralWindow, &CentralWindow::logout, this, &MainWindow::onLogout);

    restoreWindowState();
    showLogin();
}

MainWindow::~MainWindow() {
}

void MainWindow::createTitleBar() {
    QWidget* titleBar = new QWidget();
    titleBar->setFixedHeight(48);
    titleBar->setStyleSheet(R"(
        QWidget {
            background-color: #660874;
        }
    )");

    QHBoxLayout* titleLayout = new QHBoxLayout(titleBar);
    titleLayout->setContentsMargins(16, 0, 16, 0);
    titleLayout->setSpacing(12);

    QLabel* logoLabel = new QLabel();
    logoLabel->setStyleSheet(R"(
        QLabel {
            color: white;
            font-size: 18px;
            font-weight: bold;
        }
    )");
    logoLabel->setText("奇测科技");
    titleLayout->addWidget(logoLabel);

    QLabel* titleLabel = new QLabel("智能视觉检测系统");
    titleLabel->setStyleSheet(R"(
        QLabel {
            color: rgba(255,255,255,0.9);
            font-size: 14px;
        }
    )");
    titleLayout->addWidget(titleLabel);

    titleLayout->addStretch();

    QLabel* versionLabel = new QLabel("v1.0.0");
    versionLabel->setStyleSheet(R"(
        QLabel {
            color: rgba(255,255,255,0.6);
            font-size: 12px;
        }
    )");
    titleLayout->addWidget(versionLabel);

    setMenuWidget(titleBar);
}

void MainWindow::createMenuBar() {
    QMenuBar* menuBar = new QMenuBar(this);
    menuBar->setStyleSheet(R"(
        QMenuBar {
            background-color: #3d3d3d;
            color: #ddd;
            padding: 2px 8px;
            font-size: 13px;
        }
        QMenuBar::item {
            padding: 4px 12px;
            border-radius: 4px;
        }
        QMenuBar::item:selected {
            background-color: #660874;
            color: #fff;
        }
        QMenu {
            background-color: #3d3d3d;
            color: #ddd;
            border: 1px solid #555;
            padding: 4px 0;
        }
        QMenu::item {
            padding: 6px 32px 6px 16px;
        }
        QMenu::item:selected {
            background-color: #660874;
            color: #fff;
        }
        QMenu::separator {
            height: 1px;
            background-color: #555;
            margin: 4px 8px;
        }
    )");

    QMenu* fileMenu = menuBar->addMenu("文件(&F)");
    QAction* newSchemeAction = fileMenu->addAction("新建方案");
    QAction* openSchemeAction = fileMenu->addAction("打开方案");
    QAction* saveSchemeAction = fileMenu->addAction("保存方案");
    fileMenu->addSeparator();
    QAction* importAction = fileMenu->addAction("导入方案");
    QAction* exportAction = fileMenu->addAction("导出方案");
    fileMenu->addSeparator();
    QAction* exitAction = fileMenu->addAction("退出");
    exitAction->setShortcut(QKeySequence("Ctrl+Q"));

    QMenu* editMenu = menuBar->addMenu("编辑(&E)");
    QAction* undoAction = editMenu->addAction("撤销");
    QAction* redoAction = editMenu->addAction("重做");
    editMenu->addSeparator();
    QAction* cutAction = editMenu->addAction("剪切");
    QAction* copyAction = editMenu->addAction("复制");
    QAction* pasteAction = editMenu->addAction("粘贴");

    QMenu* viewMenu = menuBar->addMenu("视图(&V)");
    QAction* toolbarAction = viewMenu->addAction("工具栏");
    toolbarAction->setCheckable(true);
    toolbarAction->setChecked(true);
    QAction* statusBarAction = viewMenu->addAction("状态栏");
    statusBarAction->setCheckable(true);
    statusBarAction->setChecked(true);
    viewMenu->addSeparator();
    QAction* fullscreenAction = viewMenu->addAction("全屏显示");

    QMenu* toolsMenu = menuBar->addMenu("工具(&T)");
    QAction* cameraCalibAction = toolsMenu->addAction("相机标定");
    QAction* imageProcessAction = toolsMenu->addAction("图像处理");
    QAction* batchProcessAction = toolsMenu->addAction("批量处理");

    QMenu* helpMenu = menuBar->addMenu("帮助(&H)");
    QAction* aboutAction = helpMenu->addAction("关于");
    QAction* helpDocAction = helpMenu->addAction("帮助文档");

    setMenuBar(menuBar);

    connect(exitAction, &QAction::triggered, this, &QMainWindow::close);
    connect(fullscreenAction, &QAction::triggered, this, &MainWindow::showFullScreen);

    connect(aboutAction, &QAction::triggered, [this]() {
        QMessageBox::about(this, "关于 奇测视觉检测系统",
            QString("<h3 style='color:#660874;'>奇测视觉检测系统 v1.0</h3>"
                    "<p>智能工业视觉检测平台</p>"
                    "<hr>"
                    "<p style='color:#ccc;'>"
                    "构建: Qt %1 | OpenCV %2<br>"
                    "编译器: MinGW-w64 GCC 11.2.0<br>"
                    "平台: Windows x64</p>"
                    "<hr>"
                    "<p style='color:#999; font-size:11px;'>"
                    "© 2026 奇测科技. 保留所有权利.</p>")
            .arg(QT_VERSION_STR)
            .arg(CV_VERSION));
    });

    QList<QAction*> unimplementedActions = {
        newSchemeAction, openSchemeAction, saveSchemeAction,
        importAction, exportAction,
        undoAction, redoAction, cutAction, copyAction, pasteAction,
        cameraCalibAction, imageProcessAction, batchProcessAction,
        helpDocAction
    };

    for (QAction* action : unimplementedActions) {
        action->setEnabled(false);
        action->setToolTip("即将推出的功能，敬请期待");
    }
}

void MainWindow::createStatusBar() {
    QStatusBar* statusBar = new QStatusBar(this);
    statusBar->setStyleSheet(R"(
        QStatusBar {
            background-color: #2d2d2d;
            color: #aaa;
            border-top: 1px solid #444;
            font-size: 12px;
            padding: 2px 8px;
        }
        QStatusBar::item {
            border: none;
        }
    )");
    statusBar->showMessage("就绪 - 欢迎使用奇测视觉检测系统");
    setStatusBar(statusBar);
}

void MainWindow::showLogin() {
    if (AuthService::instance()->isFirstRun()) {
        showFirstRunSetup();
    }
    animateViewTransition(0);
}

void MainWindow::showFirstRunSetup() {
    QDialog* dialog = new QDialog(this);
    dialog->setWindowTitle("首次运行 - 创建管理员账户");
    dialog->setFixedSize(420, 260);
    dialog->setStyleSheet(R"(
        QDialog {
            background-color: #2b2b2b;
        }
        QLabel {
            color: #ccc;
            font-size: 13px;
        }
        QLabel#titleLabel {
            color: #660874;
            font-size: 16px;
            font-weight: bold;
        }
        QLineEdit {
            border: 1px solid #555;
            border-radius: 4px;
            padding: 8px 10px;
            font-size: 14px;
            background: #2d2d2d;
            color: #e0e0e0;
        }
        QLabel#errorLabel {
            color: #F44336;
            font-size: 12px;
        }
    )");

    QVBoxLayout* layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(14);

    QLabel* titleLabel = new QLabel("欢迎使用奇测视觉检测系统");
    titleLabel->setObjectName("titleLabel");
    layout->addWidget(titleLabel);

    QLabel* descLabel = new QLabel("首次运行需要创建管理员账户");
    descLabel->setStyleSheet("color: #999; font-size: 12px;");
    layout->addWidget(descLabel);

    QFormLayout* formLayout = new QFormLayout();
    formLayout->setSpacing(10);

    QLineEdit* usernameEdit = new QLineEdit();
    usernameEdit->setPlaceholderText("请输入管理员用户名");
    formLayout->addRow("用户名:", usernameEdit);

    QLineEdit* passwordEdit = new QLineEdit();
    passwordEdit->setPlaceholderText("最少8位，含大小写+数字+特殊字符至少3类");
    passwordEdit->setEchoMode(QLineEdit::Password);
    formLayout->addRow("密码:", passwordEdit);

    QLineEdit* confirmEdit = new QLineEdit();
    confirmEdit->setPlaceholderText("请再次输入密码");
    confirmEdit->setEchoMode(QLineEdit::Password);
    formLayout->addRow("确认密码:", confirmEdit);

    layout->addLayout(formLayout);

    QLabel* errorLabel = new QLabel();
    errorLabel->setObjectName("errorLabel");
    layout->addWidget(errorLabel);

    QPushButton* createButton = new QPushButton("创建管理员账户");
    createButton->setStyleSheet(R"(
        QPushButton {
            background-color: #660874;
            color: white;
            border: none;
            border-radius: 4px;
            padding: 10px 0;
            font-size: 14px;
            font-weight: bold;
        }
        QPushButton:hover {
            background-color: #7d1a8f;
        }
    )");
    layout->addWidget(createButton);

    connect(createButton, &QPushButton::clicked, [dialog, usernameEdit, passwordEdit, confirmEdit, errorLabel]() {
        QString username = usernameEdit->text().trimmed();
        QString password = passwordEdit->text();
        QString confirm = confirmEdit->text();

        if (username.isEmpty()) {
            errorLabel->setText("请输入用户名");
            return;
        }

        if (password.length() < 8) {
            errorLabel->setText("密码至少需要8位");
            return;
        }

        bool hasUpper = false, hasLower = false, hasDigit = false, hasSpecial = false;
        for (const QChar& c : password) {
            if (c.isUpper()) hasUpper = true;
            else if (c.isLower()) hasLower = true;
            else if (c.isDigit()) hasDigit = true;
            else hasSpecial = true;
        }
        int categories = hasUpper + hasLower + hasDigit + hasSpecial;
        if (categories < 3) {
            errorLabel->setText("密码强度不足，需包含大小写、数字、特殊字符中至少3类");
            return;
        }

        if (password != confirm) {
            errorLabel->setText("两次输入的密码不一致");
            return;
        }

        if (AuthService::instance()->createUser(username, password, true)) {
            dialog->accept();
        } else {
            errorLabel->setText("创建失败，该用户名可能已存在");
        }
    });

    if (dialog->exec() != QDialog::Accepted) {
        QApplication::quit();
    }
}

void MainWindow::showMain() {
    animateViewTransition(1);
}

void MainWindow::animateViewTransition(int targetIndex) {
    int currentIndex = m_stackedWidget->currentIndex();
    if (currentIndex == targetIndex) return;

    QWidget* currentWidget = m_stackedWidget->widget(currentIndex);
    QWidget* nextWidget = m_stackedWidget->widget(targetIndex);

    QPropertyAnimation* fadeOut = new QPropertyAnimation(currentWidget, "windowOpacity");
    fadeOut->setDuration(200);
    fadeOut->setStartValue(1.0);
    fadeOut->setEndValue(0.0);
    fadeOut->setEasingCurve(QEasingCurve::InQuad);

    QPropertyAnimation* fadeIn = new QPropertyAnimation(nextWidget, "windowOpacity");
    fadeIn->setDuration(200);
    fadeIn->setStartValue(0.0);
    fadeIn->setEndValue(1.0);
    fadeIn->setEasingCurve(QEasingCurve::OutQuad);

    connect(fadeOut, &QPropertyAnimation::finished, [this, fadeIn, targetIndex]() {
        m_stackedWidget->setCurrentIndex(targetIndex);
        fadeIn->start(QAbstractAnimation::DeleteWhenStopped);
    });

    fadeOut->start(QAbstractAnimation::DeleteWhenStopped);
}

void MainWindow::restoreWindowState() {
    QSettings settings("奇测科技", "QDetectVision");
    restoreGeometry(settings.value("geometry").toByteArray());
    restoreState(settings.value("windowState").toByteArray());
}

void MainWindow::saveWindowState() {
    QSettings settings("奇测科技", "QDetectVision");
    settings.setValue("geometry", saveGeometry());
    settings.setValue("windowState", saveState());
}

void MainWindow::closeEvent(QCloseEvent* event) {
    saveWindowState();
    QMainWindow::closeEvent(event);
}

void MainWindow::showEvent(QShowEvent* event) {
    restoreWindowState();
    QMainWindow::showEvent(event);
}

void MainWindow::onLoginSuccess() {
    showMain();
}

void MainWindow::onLogout() {
    showLogin();
}