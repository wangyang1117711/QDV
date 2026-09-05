#include "MainWindow.h"
#include "LoginView.h"
#include "CentralWindow.h"
#include "AuthService.h"
#include "Core/Logger.h"     // v2.1.0 M4 诊断日志（include/ 是 include path 根）
#include <QPropertyAnimation>
#include <QGraphicsOpacityEffect>
#include <QEasingCurve>
#include <QSettings>
#include <QCloseEvent>
#include <QShowEvent>
#include <QResizeEvent>
#include <QMouseEvent>
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
#include <QScreen>      // v2.1.0 M4：restoreGeometry fallback 时用 primaryScreen()
#include <QLineEdit>
#include <QTimer>
#include <QPointer>
#include <QEvent>
#include <QStyle>

#ifdef Q_OS_WIN
#  include <windows.h>
#endif

#include <opencv2/core/version.hpp>

// =================================================================
// 标题栏几何常量（v2.1.0 规范：32px 高 + 12-14pt 字体）
// =================================================================
namespace {
constexpr int kTitleBarHeight = 28;       // 标题栏高度（v5.0：32→28，全屏下更紧凑）
constexpr int kResizeBorder   = 8;        // 8 方向边缘拖拽触发宽度
constexpr int kMinWinWidth    = 200;      // 最小宽度（需求规范）
constexpr int kMinWinHeight   = 100;      // 最小高度（需求规范）
constexpr int kWorkMinWidth   = 800;      // 工作区最小宽度（需求规范）
constexpr int kWorkMinHeight  = 600;      // 工作区最小高度（需求规范）
}

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("奇测视觉检测系统 v2.1.0");
    setMinimumSize(kMinWinWidth, kMinWinHeight);
    // v2.1.0 M4 修复：restoreGeometry() 可能从损坏的 QSettings 恢复出 0×0
    // 或屏幕外窗口（之前看到 geometry=QByteArray{64,0,66,0...}），导致主窗口
    // 登录后不可见。先设默认尺寸作兜底，让 restoreWindowState() 在此基础上
    // 再恢复用户上次保存的尺寸。
    resize(1280, 800);

    createTitleBar();
    createMenuBar();
    createStatusBar();

    m_stackedWidget = new QStackedWidget(this);
    setCentralWidget(m_stackedWidget);

    m_loginView = new LoginView();

    m_stackedWidget->addWidget(m_loginView);

    connect(m_loginView, &LoginView::loginSuccess, this, &MainWindow::onLoginSuccess);

    restoreWindowState();
    showLogin();
}

MainWindow::~MainWindow() {
}

void MainWindow::createTitleBar() {
    QWidget* titleBar = new QWidget();
    // 修复：原 48px → 32px（v2.1.0 规范）
    titleBar->setFixedHeight(kTitleBarHeight);
    titleBar->setObjectName("MainTitleBar");   // 给 QSS 锚点
    titleBar->setStyleSheet(R"(
        QWidget#MainTitleBar {
            background-color: #1E1E1E;
            border-bottom: 1px solid #2A2A2A;
        }
    )");

    QHBoxLayout* titleLayout = new QHBoxLayout(titleBar);
    titleLayout->setContentsMargins(12, 0, 4, 0);
    titleLayout->setSpacing(8);

    // 左侧：项目名/版本号 12-14pt 无衬线
    m_titleLabel = new QLabel();
    m_titleLabel->setObjectName("TitleAccent");
    m_titleLabel->setText(QString("QDV · %1 v%2").arg(QStringLiteral("未命名方案"), QApplication::applicationVersion().isEmpty()
        ? QStringLiteral("2.1.0")
        : QApplication::applicationVersion()));
    m_titleLabel->setStyleSheet(R"(
        QLabel#TitleAccent {
            color: #FFFFFF;
            font: 13pt "Segoe UI", "Microsoft YaHei";
        }
    )");
    titleLayout->addWidget(m_titleLabel);

    // 记录以便后续动态更新（绑定方案变化）
    // 通过 findChild<QLabel*>("TitleAccent") 即可拿到
    titleLayout->addStretch();

    // 右侧：min / max / close 按钮（36×28）
    QString btnBaseStyle = QString(R"(
        QPushButton {
            background: transparent;
            border: none;
            color: #FFFFFF;
            font: 12pt "Segoe UI Symbol";
            min-width: %1px;
            max-width: %1px;
            min-height: %2px;
            max-height: %2px;
        }
        QPushButton:hover { background-color: #2D2D2D; }
    )").arg(36).arg(kTitleBarHeight - 4);

    m_minimizeBtn = new QPushButton("\u2014");  // ─
    m_minimizeBtn->setStyleSheet(btnBaseStyle);
    m_minimizeBtn->setToolTip("最小化");
    titleLayout->addWidget(m_minimizeBtn);

    m_maximizeBtn = new QPushButton("\u25A1");  // □
    m_maximizeBtn->setStyleSheet(btnBaseStyle);
    m_maximizeBtn->setToolTip("最大化");
    titleLayout->addWidget(m_maximizeBtn);

    m_closeBtn = new QPushButton("\u2715");   // ✕
    m_closeBtn->setStyleSheet(btnBaseStyle + R"(
        QPushButton:hover { background-color: #E81123; }
    )");
    m_closeBtn->setToolTip("关闭");
    titleLayout->addWidget(m_closeBtn);

    connect(m_minimizeBtn, &QPushButton::clicked, this, &MainWindow::onMinimize);
    connect(m_maximizeBtn, &QPushButton::clicked, this, &MainWindow::onMaximizeRestore);
    connect(m_closeBtn, &QPushButton::clicked, this, &MainWindow::onCloseWindow);

    setMenuWidget(titleBar);
}

void MainWindow::createMenuBar() {
    QMenuBar* menuBar = new QMenuBar(this);
    menuBar->setStyleSheet(R"(
        QMenuBar {
            background-color: #3d3d3d;
            color: #ddd;
            padding: 1px 8px;
            font-size: 12px;
        }
        QMenuBar::item {
            padding: 3px 10px;
            border-radius: 4px;
        }
        QMenuBar::item:selected {
            background-color: #7C4DFF;
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
            background-color: #7C4DFF;
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
            QString("<h3 style='color:#7C4DFF;'>奇测视觉检测系统 v1.0</h3>"
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
            color: #7C4DFF;
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
            background-color: #7C4DFF;
            color: white;
            border: none;
            border-radius: 4px;
            padding: 10px 0;
            font-size: 14px;
            font-weight: bold;
        }
        QPushButton:hover {
            background-color: #8E66FF;
        }
    )");
    layout->addWidget(createButton);

    bool* accepted = new bool(false);

    connect(createButton, &QPushButton::clicked, [dialog, usernameEdit, passwordEdit, confirmEdit, errorLabel, accepted]() {
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
            *accepted = true;
            dialog->accept();
        } else {
            errorLabel->setText("创建失败，该用户名可能已存在");
        }
    });

    if (dialog->exec() != QDialog::Accepted || !(*accepted)) {
        QMessageBox::warning(nullptr, "安装未完成",
            "需要创建管理员账户才能使用本系统。程序将退出。");
        QApplication::quit();
    }
}

void MainWindow::showMain() {
    QDV::Logger::info("[MainWindow] showMain: enter, m_centralWindow=" +
        QString::number(reinterpret_cast<quintptr>(static_cast<void*>(m_centralWindow)), 16));
    if (!m_centralWindow) {
        QDV::Logger::info("[MainWindow] showMain: before new CentralWindow()");
        m_centralWindow = new CentralWindow();
        QDV::Logger::info("[MainWindow] showMain: new CentralWindow() OK, ptr=" +
            QString::number(reinterpret_cast<quintptr>(static_cast<void*>(m_centralWindow)), 16));
        connect(m_centralWindow, &CentralWindow::logout, this, &MainWindow::onLogout);
        // v5.0：视图切换时更新状态栏
        connect(m_centralWindow, &CentralWindow::statusMessageRequested, this, [this](const QString& msg) {
            if (statusBar()) statusBar()->showMessage(msg, 3000);
        });
        QDV::Logger::info("[MainWindow] showMain: connect logout OK");
    }
    // v2.1.0 M4 修复：之前 m_centralWindow->setParent(m_stackedWidget)、
    // m_stackedWidget->addWidget、setParent(this) 同步调用全部 segfault。
    // 根因：CentralWindow 内部含多个 QQuickWidget 子视图，reparent/polish 时
    // 触发 OpenGL 渲染初始化崩溃。
    // 新方案：把 m_centralWindow 作为独立 top-level window，不集成到 MainWindow
    // widget tree。登录成功后：m_centralWindow->show()（独立窗口），MainWindow hide()。
    // logout 时反向：m_centralWindow->close()，MainWindow show() 回到登录页。
    QDV::Logger::info("[MainWindow] showMain: hide() MainWindow login, then show() central as top-level");
    saveWindowState();   // 先保存登录窗口几何
    if (m_centralWindow->parent() == nullptr) {
        QDV::Logger::info("[MainWindow] showMain: central is top-level, no setParent needed");
    }
    if (m_loginView) {
        m_loginView->hide();
    }
    m_stackedWidget->hide();
    this->hide();
    // v2.1.0 BUG修复：之前此处强制 showMaximized()，导致中央窗口在登录后
    // "自动全屏"且难以自由缩放。改为恢复上次窗口几何（默认 1400x900 居中），
    // 始终为可缩放普通窗口，用户可自由调整大小。
    QDV::Logger::info("[MainWindow] showMain: MainWindow hidden, restoring central window state");
    restoreCentralWindowState();
    m_centralWindow->show();
    QDV::Logger::info("[MainWindow] showMain: central shown OK, isVisible=" +
        QString::number(m_centralWindow->isVisible()));
}

void MainWindow::animateViewTransition(int targetIndex) {
    int currentIndex = m_stackedWidget->currentIndex();
    QDV::Logger::info(QString("[MainWindow] animateViewTransition: currentIndex=%1, targetIndex=%2, count=%3")
        .arg(currentIndex).arg(targetIndex).arg(m_stackedWidget->count()));
    if (currentIndex == targetIndex) {
        QDV::Logger::info("[MainWindow] animateViewTransition: same index, return");
        return;
    }

    QWidget* currentWidget = m_stackedWidget->widget(currentIndex);
    QWidget* nextWidget = m_stackedWidget->widget(targetIndex);

    if (!currentWidget || !nextWidget) {
        QDV::Logger::error(QString("[MainWindow] animateViewTransition: null widget (current=%1, next=%2), force setCurrentIndex")
            .arg(reinterpret_cast<quintptr>(currentWidget))
            .arg(reinterpret_cast<quintptr>(nextWidget)));
        if (nextWidget) {
            m_stackedWidget->setCurrentIndex(targetIndex);
        }
        return;
    }

    QGraphicsOpacityEffect* fadeOutEffect = new QGraphicsOpacityEffect(this);
    currentWidget->setGraphicsEffect(fadeOutEffect);
    fadeOutEffect->setOpacity(1.0);

    QPropertyAnimation* fadeOut = new QPropertyAnimation(fadeOutEffect, "opacity");
    fadeOut->setDuration(200);
    fadeOut->setStartValue(1.0);
    fadeOut->setEndValue(0.0);
    fadeOut->setEasingCurve(QEasingCurve::InQuad);

    QGraphicsOpacityEffect* fadeInEffect = new QGraphicsOpacityEffect(this);
    nextWidget->setGraphicsEffect(fadeInEffect);
    fadeInEffect->setOpacity(0.0);

    QPropertyAnimation* fadeIn = new QPropertyAnimation(fadeInEffect, "opacity");
    fadeIn->setDuration(200);
    fadeIn->setStartValue(0.0);
    fadeIn->setEndValue(1.0);
    fadeIn->setEasingCurve(QEasingCurve::OutQuad);

    connect(fadeOut, &QPropertyAnimation::finished, [this, targetIndex, fadeIn, currentWidget, nextWidget]() {
        m_stackedWidget->setCurrentIndex(targetIndex);
        fadeIn->start(QAbstractAnimation::DeleteWhenStopped);
    });

    connect(fadeIn, &QPropertyAnimation::finished, [currentWidget, nextWidget]() {
        currentWidget->setGraphicsEffect(nullptr);
        nextWidget->setGraphicsEffect(nullptr);
    });

    fadeOut->start(QAbstractAnimation::DeleteWhenStopped);
}

void MainWindow::restoreWindowState() {
    QSettings settings("奇测科技", "QDetectVision");
    const QByteArray geo = settings.value("geometry").toByteArray();
    // v2.1.0 M4 修复：Qt 序列化 QRect/QByteArray 至少 16 字节；过短视为损坏。
    if (geo.size() >= 16) {
        restoreGeometry(geo);
    }
    // v2.1.0 M4 修复：若 restoreGeometry 把窗口恢复成 0×0 或超出最小尺寸
    // （QSettings 损坏场景：geometry={64,0,66,0...} 解析失败或保存了无效值），
    // 强制 fallback 到默认尺寸并居中。
    if (width() < kMinWinWidth || height() < kMinWinHeight) {
        qWarning() << "[MainWindow] restoreGeometry produced invalid size ("
                   << width() << "x" << height()
                   << "), fallback to default 1280x800";
        resize(1280, 800);
        if (QScreen* screen = QGuiApplication::primaryScreen()) {
            const QRect avail = screen->availableGeometry();
            move((avail.width() - width()) / 2, (avail.height() - height()) / 2);
        }
    }
    const QByteArray winState = settings.value("windowState").toByteArray();
    if (winState.size() >= 16) {
        restoreState(winState);
    }
}

void MainWindow::saveWindowState() {
    QSettings settings("奇测科技", "QDetectVision");
    settings.setValue("geometry", saveGeometry());
    settings.setValue("windowState", saveState());
}

// =====================================================================
// v2.1.0 BUG修复：中央窗口几何持久化
// 登录后不再强制 showMaximized()，而是恢复上次几何（默认 1400x900 居中），
// 保证窗口始终可自由缩放，且关闭/登出后能记住用户调好的大小。
// =====================================================================
void MainWindow::restoreCentralWindowState() {
    if (!m_centralWindow) return;
    QSettings settings("奇测科技", "QDetectVision");
    const QByteArray geo = settings.value("centralGeometry").toByteArray();
    if (geo.size() >= 16) {
        m_centralWindow->restoreGeometry(geo);
    }
    // 恢复后若尺寸无效（损坏/过小），回退到默认 1400x900 并居中
    if (m_centralWindow->width() < 800 || m_centralWindow->height() < 600) {
        const int w = 1400, h = 900;
        m_centralWindow->resize(w, h);
        if (QScreen* screen = QGuiApplication::primaryScreen()) {
            const QRect avail = screen->availableGeometry();
            m_centralWindow->move((avail.width() - w) / 2, (avail.height() - h) / 2);
        }
    }
}

void MainWindow::saveCentralWindowState() {
    if (!m_centralWindow) return;
    QSettings settings("奇测科技", "QDetectVision");
    settings.setValue("centralGeometry", m_centralWindow->saveGeometry());
}

void MainWindow::closeEvent(QCloseEvent* event) {
    saveWindowState();
    saveCentralWindowState();
    QMainWindow::closeEvent(event);
}

void MainWindow::showEvent(QShowEvent* event) {
    restoreWindowState();
    QMainWindow::showEvent(event);
}

void MainWindow::changeEvent(QEvent* event) {
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange) {
        updateMaxRestoreIcon();
    }
}

void MainWindow::onMinimize() {
    showMinimized();
}

void MainWindow::onMaximizeRestore() {
    if (isMaximized()) {
        showNormal();
    } else {
        showMaximized();
    }
}

void MainWindow::onCloseWindow() {
    close();
}

void MainWindow::updateMaxRestoreIcon() {
    if (!m_maximizeBtn) return;
    if (isMaximized()) {
        m_maximizeBtn->setText("❐");
        m_maximizeBtn->setToolTip("还原");
    } else {
        m_maximizeBtn->setText("□");
        m_maximizeBtn->setToolTip("最大化");
    }
}

// v2.1.0 M4 修复：参数与 LoginView::loginSuccess(const QString&) 严格匹配
// 之前无参版本在 Qt 信号-槽严格匹配下永不触发
void MainWindow::onLoginSuccess(const QString& username) {
    QDV::Logger::info(QString("[MainWindow] onLoginSuccess: user=%1, about to showMain()").arg(username));
    showMain();
}

void MainWindow::onLogout() {
    QDV::Logger::info("[MainWindow] onLogout: close central, show MainWindow login");
    // v2.1.0 M4：CentralWindow 是独立 top-level 窗口，登出时直接 close()，回到登录页
    if (m_centralWindow) {
        saveCentralWindowState();  // 记录中央窗口几何，下次登录恢复
        m_centralWindow->close();
    }
    if (m_loginView) {
        m_loginView->show();
    }
    m_stackedWidget->show();
    this->show();
    restoreWindowState();
    QDV::Logger::info("[MainWindow] onLogout: MainWindow login shown OK, isVisible=" +
        QString::number(this->isVisible()));
}

// =====================================================================
// v2.1.0：标题栏方案名动态绑定
// 调用方：CentralWindow / SchemeView / EditView 等在切换/保存方案时触发
// 缓存：m_currentSchemeName 供后续读
// =====================================================================
void MainWindow::setCurrentSchemeName(const QString& name) {
    m_currentSchemeName = name;
    if (m_titleLabel) {
        const QString version = QApplication::applicationVersion().isEmpty()
            ? QStringLiteral("2.1.0")
            : QApplication::applicationVersion();
        m_titleLabel->setText(QString("QDV · %1 v%2").arg(name.isEmpty()
            ? QStringLiteral("未命名方案")
            : name, version));
    }
}

// =====================================================================
// v2.1.0 新增：8 方向边缘拖拽大小调整
// 拦截 WM_NCHITTEST，将边缘 8px 区域映射到 HTLEFT/HTRIGHT/HTTOP/HTBOTTOM/HTCORNER*
// 注意：最大化状态下禁用边缘拖拽（避免与系统 Snap 冲突）
// =====================================================================
bool MainWindow::nativeEvent(const QByteArray& eventType, void* message, qintptr* result) {
#ifdef Q_OS_WIN
    if (eventType == "windows_generic_MSG" || eventType == "WM_NCHITTEST") {
        MSG* msg = static_cast<MSG*>(message);
        if (msg->message == WM_NCHITTEST) {
            // 最大化时不处理（让系统处理）
            if (isMaximized() || isFullScreen()) {
                return QMainWindow::nativeEvent(eventType, message, result);
            }
            // 必须使用 frameGeometry()（含窗口边框）而不是 geometry()
            const QRect frame = frameGeometry();
            const int x = static_cast<int>(LOWORD(msg->lParam)) - frame.x();
            const int y = static_cast<int>(HIWORD(msg->lParam)) - frame.y();
            const int w = frame.width();
            const int h = frame.height();
            const int B = kResizeBorder;

            const bool left   = (x < B);
            const bool right  = (x > w - B);
            const bool top    = (y < B);
            const bool bottom = (y > h - B);

            // 命中区域在客户区内的子控件上时让 Qt 处理
            // （按钮、菜单等需要可点击，避免被吞掉）
            if (!left && !right && !top && !bottom) {
                return QMainWindow::nativeEvent(eventType, message, result);
            }

            if (top && left)        { *result = HTTOPLEFT;     return true; }
            if (top && right)       { *result = HTTOPRIGHT;    return true; }
            if (bottom && left)     { *result = HTBOTTOMLEFT;  return true; }
            if (bottom && right)    { *result = HTBOTTOMRIGHT; return true; }
            if (left)               { *result = HTLEFT;        return true; }
            if (right)              { *result = HTRIGHT;       return true; }
            if (top)                { *result = HTTOP;         return true; }
            if (bottom)             { *result = HTBOTTOM;      return true; }
        }
    }
#endif
    return QMainWindow::nativeEvent(eventType, message, result);
}

// =====================================================================
// v2.1.0 新增：平滑重绘（resize 期间冻结更新，结束后一次性重绘）
// 解决连续拖拽边缘时画面闪烁
// =====================================================================
void MainWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    
    // 边界硬限制（防止 Qt 在某些情况越过 setMinimumSize）
    // 1. 确保窗口不小于绝对最小值
    if (width() < kMinWinWidth)  { resize(kMinWinWidth, height());  return; }
    if (height() < kMinWinHeight) { resize(width(), kMinWinHeight);  return; }
    
    // 2. 确保工作区满足最小要求（800×600）
    // 工作区 = 窗口尺寸 - 边框 - 标题栏
    const int workWidth = width() - (frameGeometry().width() - geometry().width());
    const int workHeight = height() - (frameGeometry().height() - geometry().height()) - kTitleBarHeight;
    
    if (workWidth < kWorkMinWidth || workHeight < kWorkMinHeight) {
        // 计算需要的最小窗口尺寸
        const int borderWidth = frameGeometry().width() - geometry().width();
        const int borderHeight = frameGeometry().height() - geometry().height();
        const int minWindowWidth = kWorkMinWidth + borderWidth;
        const int minWindowHeight = kWorkMinHeight + borderHeight + kTitleBarHeight;
        
        // 使用两者中较大的值
        const int targetWidth = qMax(width(), minWindowWidth);
        const int targetHeight = qMax(height(), minWindowHeight);
        resize(targetWidth, targetHeight);
        return;
    }

    // 异步单次重绘，避免多个 paint 事件堆积
    QTimer::singleShot(0, this, [this]() {
        setUpdatesEnabled(false);
        setUpdatesEnabled(true);
        update();
    });
}

// =====================================================================
// v2.1.0 新增：标题栏双击切换最大化/还原
// 仅在鼠标位置位于标题栏区域内时触发
// =====================================================================
void MainWindow::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        // 判断点击位置是否在标题栏区域（顶部 32px）
        QWidget* menuWidget = this->menuWidget();
        if (menuWidget) {
            const QPoint localPos = menuWidget->mapFrom(this, event->pos());
            if (menuWidget->rect().contains(localPos)) {
                onMaximizeRestore();
                event->accept();
                return;
            }
        }
    }
    QMainWindow::mouseDoubleClickEvent(event);
}