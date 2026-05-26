#include "CentralWindow.h"
#include "CameraView.h"
#include "SchemeView.h"
#include "EditView.h"
#include "IOView.h"
#include "CommView.h"
#include "MonitorView.h"
#include "TrainingInference/TrainingInferenceView.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QToolButton>
#include <QStackedWidget>
#include <QLabel>
#include <QFrame>
#include <QFont>
#include <QShortcut>
#include <QMouseEvent>
#include <QEvent>
#include <QApplication>

CentralWindow::CentralWindow(QWidget* parent) : QWidget(parent) {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    createNavigationPanel(mainLayout);

    m_cameraView = new CameraView();
    m_schemeView = new SchemeView();
    m_editView = new EditView();
    m_ioView = new IOView();
    m_commView = new CommView();
    m_monitorView = new MonitorView();
    m_trainingView = new TrainingInferenceView();

    createContentViews();

    mainLayout->addWidget(m_contentStack, 1);

    connect(m_schemeView, &SchemeView::schemeCountChanged, this, &CentralWindow::updateSchemeCount);
    connect(m_editView, &EditView::toolCountChanged, this, &CentralWindow::updateToolCount);
    connect(m_monitorView, &MonitorView::detectionCountChanged, this, &CentralWindow::updateDetectionCount);
    connect(m_monitorView, &MonitorView::alertCountChanged, this, &CentralWindow::updateAlertCount);
    connect(m_editView, &EditView::requestRunDetection, [this]() {
        switchView(5);
    });

    switchView(0);
}

CentralWindow::~CentralWindow() {
}

void CentralWindow::createNavigationPanel(QVBoxLayout* mainLayout) {
    QWidget* navPanel = new QWidget();
    navPanel->setFixedHeight(72);
    navPanel->setStyleSheet(R"(
        QWidget {
            background-color: #252525;
            border-bottom: 1px solid #444;
        }
    )");

    QHBoxLayout* navLayout = new QHBoxLayout(navPanel);
    navLayout->setContentsMargins(16, 0, 16, 0);
    navLayout->setSpacing(4);

    QLabel* logoLabel = new QLabel("QD");
    logoLabel->setStyleSheet("color: #660874; font-size: 22px; font-weight: bold; padding: 0 8px; background: transparent; border: none;");
    navLayout->addWidget(logoLabel);

    navLayout->addSpacing(12);

    struct NavItem {
        QString text;
        int index;
    };
    QList<NavItem> items = {
        {"首页", 0},
        {"相机", 1},
        {"方案", 2},
        {"编辑", 3},
        {"IO监控", 4},
        {"通信", 5},
        {"监控", 6},
        {"训练推理", 7},
    };

    for (const auto& item : items) {
        QToolButton* btn = createNavButton(item.text, item.index);
        navLayout->addWidget(btn);
        navButtons[item.index] = btn;
    }

    navLayout->addStretch();

    QToolButton* logoutBtn = new QToolButton();
    logoutBtn->setText("退出登录");
    logoutBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    logoutBtn->setStyleSheet(R"(
        QToolButton {
            background: transparent;
            border: 1px solid #555;
            border-radius: 4px;
            padding: 8px 16px;
            color: #e0e0e0;
            font-size: 13px;
        }
        QToolButton:hover {
            background-color: rgba(244, 67, 54, 0.7);
            color: white;
            border-color: #F44336;
        }
    )");
    logoutBtn->setCursor(Qt::PointingHandCursor);
    connect(logoutBtn, &QToolButton::clicked, this, &CentralWindow::onLogoutClicked);
    navLayout->addWidget(logoutBtn);

    mainLayout->addWidget(navPanel);
}

QToolButton* CentralWindow::createNavButton(const QString& text, int index) {
    Q_UNUSED(index);
    QToolButton* btn = new QToolButton();
    btn->setText(text);
    btn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    btn->setCheckable(true);
    btn->setFixedHeight(48);
    btn->setStyleSheet(R"(
        QToolButton {
            background: transparent;
            border: none;
            border-bottom: 3px solid transparent;
            padding: 8px 20px;
            color: #aaa;
            font-size: 14px;
            font-weight: bold;
        }
        QToolButton:hover {
            color: #e0e0e0;
            background-color: rgba(102, 8, 116, 0.1);
        }
        QToolButton:checked {
            color: white;
            border-bottom-color: #660874;
        }
    )");
    btn->setCursor(Qt::PointingHandCursor);
    connect(btn, &QToolButton::clicked, [this, btn]() {
        int idx = -1;
        for (auto it = navButtons.begin(); it != navButtons.end(); ++it) {
            if (it.value() == btn) {
                idx = it.key();
                break;
            }
        }
        if (idx >= 0) {
            switchView(idx);
        }
    });
    return btn;
}

void CentralWindow::createContentViews() {
    m_contentStack = new QStackedWidget();
    m_contentStack->setStyleSheet("background-color: #1e1e1e;");

    m_contentStack->addWidget(createHomeView());
    m_contentStack->addWidget(m_cameraView);
    m_contentStack->addWidget(m_schemeView);
    m_contentStack->addWidget(m_editView);
    m_contentStack->addWidget(m_ioView);
    m_contentStack->addWidget(m_commView);
    m_contentStack->addWidget(m_monitorView);
    m_contentStack->addWidget(m_trainingView);
}

QWidget* CentralWindow::createHomeView() {
    QWidget* homeWidget = new QWidget();
    homeWidget->setStyleSheet("background-color: #1e1e1e;");
    QVBoxLayout* homeLayout = new QVBoxLayout(homeWidget);
    homeLayout->setContentsMargins(32, 24, 32, 24);
    homeLayout->setSpacing(24);

    QLabel* titleLabel = new QLabel("欢迎使用奇测视觉检测系统");
    titleLabel->setStyleSheet("font-size: 22px; font-weight: bold; color: #e0e0e0; background: transparent; border: none;");
    homeLayout->addWidget(titleLabel);

    QLabel* subtitleLabel = new QLabel("选择下方功能模块开始操作");
    subtitleLabel->setStyleSheet("font-size: 14px; color: #aaa; background: transparent; border: none; margin-bottom: 8px;");
    homeLayout->addWidget(subtitleLabel);

    QHBoxLayout* statRow = new QHBoxLayout();
    statRow->setSpacing(16);

    statRow->addWidget(createStatCardEx("检测方案", "0", "#42A5F5", m_statSchemeValue));
    statRow->addWidget(createStatCardEx("检测工具", "0", "#FFA726", m_statToolValue));
    statRow->addWidget(createStatCardEx("检测次数", "0", "#66BB6A", m_statDetectionValue));
    statRow->addWidget(createStatCardEx("告警信息", "0", "#EF5350", m_statAlertValue));

    homeLayout->addLayout(statRow);

    QLabel* quickLabel = new QLabel("快速入口");
    quickLabel->setStyleSheet("font-size: 16px; font-weight: bold; color: #ccc; background: transparent; border: none; margin-top: 8px;");
    homeLayout->addWidget(quickLabel);

    QHBoxLayout* stepRow = new QHBoxLayout();
    stepRow->setSpacing(16);

    struct Step {
        QString number;
        QString title;
        QString desc;
        int target;
    };
    QList<Step> steps = {
        {"1", "连接相机", "配置并连接工业相机", 1},
        {"2", "创建方案", "新建视觉检测方案", 2},
        {"3", "编辑工具链", "配置检测工具流程", 3},
        {"4", "IO配置", "配置输入输出通道", 4},
        {"5", "通信设置", "设置通信协议参数", 5},
        {"6", "运行监控", "启动检测并监控结果", 6},
        {"7", "训练推理", "小模型训练与快速推理", 7},
    };

    for (const auto& step : steps) {
        QWidget* card = createStepCard(step.number, step.title, step.desc, step.target);
        stepRow->addWidget(card);
    }

    homeLayout->addLayout(stepRow);
    homeLayout->addStretch();

    return homeWidget;
}

QWidget* CentralWindow::createStatCard(const QString& title, const QString& value, const QString& color) {
    QLabel* dummy = nullptr;
    return createStatCardEx(title, value, color, dummy);
}

QWidget* CentralWindow::createStatCardEx(const QString& title, const QString& value, const QString& color, QLabel*& outValueLabel) {
    QWidget* card = new QWidget();
    card->setFixedHeight(100);
    card->setStyleSheet(R"(
        QWidget {
            background-color: #2d2d2d;
            border: 1px solid #444;
            border-radius: 8px;
        }
    )");

    QVBoxLayout* cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(16, 12, 16, 12);
    cardLayout->setSpacing(4);

    QLabel* valueLabel = new QLabel(value);
    valueLabel->setStyleSheet(QString("font-size: 32px; font-weight: bold; color: %1; background: transparent; border: none;").arg(color));
    outValueLabel = valueLabel;

    QLabel* titleLabel = new QLabel(title);
    titleLabel->setStyleSheet("font-size: 13px; color: #aaa; background: transparent; border: none;");

    cardLayout->addWidget(valueLabel);
    cardLayout->addWidget(titleLabel);

    return card;
}

QWidget* CentralWindow::createStepCard(const QString& number, const QString& title, const QString& description, int targetViewIndex) {
    QWidget* card = new QWidget();
    card->setMinimumHeight(120);
    card->setStyleSheet(R"(
        QWidget {
            background-color: #2d2d2d;
            border: 1px solid #444;
            border-radius: 8px;
        }
    )");
    card->setCursor(Qt::PointingHandCursor);

    QVBoxLayout* cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(16, 12, 16, 12);
    cardLayout->setSpacing(4);

    QLabel* numLabel = new QLabel(number);
    numLabel->setStyleSheet("font-size: 24px; font-weight: bold; color: #660874; background: transparent; border: none;");
    cardLayout->addWidget(numLabel);

    QLabel* titleLabel = new QLabel(title);
    titleLabel->setStyleSheet("font-size: 13px; font-weight: bold; color: #e0e0e0; background: transparent; border: none;");
    cardLayout->addWidget(titleLabel);

    QLabel* descLabel = new QLabel(description);
    descLabel->setStyleSheet("font-size: 11px; color: #888; background: transparent; border: none;");
    descLabel->setWordWrap(true);
    cardLayout->addWidget(descLabel);

    cardLayout->addStretch();

    QFrame* sep = new QFrame();
    sep->setFrameShape(QFrame::HLine);
    sep->setStyleSheet("background-color: #555; border: none; max-height: 1px;");
    cardLayout->addWidget(sep);

    m_stepCardTargets[card] = targetViewIndex;

    card->installEventFilter(this);

    return card;
}

void CentralWindow::switchView(int index) {
    if (index < 0 || index >= m_contentStack->count()) return;

    m_contentStack->setCurrentIndex(index);

    for (auto it = navButtons.begin(); it != navButtons.end(); ++it) {
        it.value()->setChecked(it.key() == index);
    }

    QStringList viewNames = {"首页", "相机", "方案", "编辑", "IO监控", "通信", "监控", "训练推理"};
    if (index >= 0 && index < viewNames.size()) {
        emit viewChanged(viewNames[index]);
    }
}

bool CentralWindow::eventFilter(QObject* obj, QEvent* event) {
    if (event->type() == QEvent::MouseButtonPress) {
        QWidget* widget = qobject_cast<QWidget*>(obj);
        if (widget && m_stepCardTargets.contains(widget)) {
            int targetIndex = m_stepCardTargets[widget];
            switchView(targetIndex);
            return true;
        }
    }

    if (event->type() == QEvent::KeyPress) {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->modifiers() == Qt::AltModifier) {
            int key = keyEvent->key();
            if (key >= Qt::Key_1 && key <= Qt::Key_8) {
                switchView(key - Qt::Key_1);
                return true;
            }
        }
    }

    return QWidget::eventFilter(obj, event);
}

void CentralWindow::onLogoutClicked() {
    emit logout();
}

void CentralWindow::updateSchemeCount(int count) {
    if (m_statSchemeValue) {
        m_statSchemeValue->setText(QString::number(count));
    }
}

void CentralWindow::updateToolCount(int count) {
    if (m_statToolValue) {
        m_statToolValue->setText(QString::number(count));
    }
}

void CentralWindow::updateDetectionCount(int count) {
    if (m_statDetectionValue) {
        m_statDetectionValue->setText(QString::number(count));
    }
}

void CentralWindow::updateAlertCount(int count) {
    if (m_statAlertValue) {
        m_statAlertValue->setText(QString::number(count));
    }
}