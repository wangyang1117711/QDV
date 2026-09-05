#include "CentralWindow.h"
#include "CameraView.h"
#include "SchemeView.h"
#include "EditView.h"
#include "IOView.h"
#include "CommView.h"
#include "MonitorView.h"
#include "TrainingInference/TrainingInferenceView.h"
#include "ZeroShotDetectView.h"  // [零样本检测模块] 索引 8
#include "Core/DetectionStats.h"
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
#include <QPropertyAnimation>
#include <QResizeEvent>
#include <QLayout>
#include <exception>
#include "Core/Logger.h"

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
    // [零样本检测模块] 索引 8
    // 用 try/catch 包裹构造：若运行环境缺少模型源 / 单例未就绪导致构造抛异常，
    // 不让它拖垮整个 CentralWindow 构造（否则主界面直接打不开）。
    // 构造失败时退化为占位页，并在界面与日志中明确提示，便于定位"界面未打开"根因。
    try {
        m_zeroShotView = new ZeroShotDetectView();
    } catch (const std::exception& e) {
        m_zeroShotView = nullptr;
        QDV::Logger::error(QStringLiteral("[CentralWindow] 零样本检测视图构造失败: %1").arg(e.what()));
    } catch (...) {
        m_zeroShotView = nullptr;
        QDV::Logger::error(QStringLiteral("[CentralWindow] 零样本检测视图构造失败（未知异常）"));
    }

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
    navPanel->setFixedHeight(56);  // v5.1 融合优化：42→56px，平衡紧凑与点击区域
    navPanel->setStyleSheet(R"(
        QWidget {
            background-color: #1A1A1A;
            border-bottom: 1px solid #3D3D3D;
        }
    )");

    QHBoxLayout* navLayout = new QHBoxLayout(navPanel);
    navLayout->setContentsMargins(16, 0, 16, 0);
    navLayout->setSpacing(2);

    QLabel* logoLabel = new QLabel("QD");
    logoLabel->setStyleSheet("color: #7C4DFF; font-size: 18px; font-weight: bold; padding: 0 6px; background: transparent; border: none;");
    navLayout->addWidget(logoLabel);

    QLabel* brandLabel = new QLabel("奇测视觉");
    brandLabel->setStyleSheet("color: #E0E0E0; font-size: 13px; font-weight: bold; background: transparent; border: none; padding-right: 8px;");
    navLayout->addWidget(brandLabel);

    QFrame* brandSep = new QFrame();
    brandSep->setFixedWidth(1);
    brandSep->setStyleSheet("background-color: #3D3D3D; border: none; max-height: 24px;");
    navLayout->addWidget(brandSep);

    navLayout->addSpacing(8);

    struct NavItem {
        QString text;
        int index;
    };
    QList<NavItem> items = {
        {"\u2302 首页", 0},
        {"\u25C9 相机", 1},
        {"\u229E 方案", 2},
        {"\u270E 编辑", 3},
        {"\u21C4 IO监控", 4},
        {"\u27E1 通信", 5},
        {"\u26A1 监控", 6},
        {"\u25B6 训练推理", 7},
        {"\u2728 零样本检测", 8},  // [零样本检测模块] 索引 8
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
            border: 1px solid #3D3D3D;
            border-radius: 4px;
            padding: 6px 12px;
            color: #e0e0e0;
            font-size: 12px;
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
    btn->setFixedHeight(48);  // v5.1 融合优化：42→48px，提升点击区域
    btn->setStyleSheet(R"(
        QToolButton {
            background: transparent;
            border: none;
            border-bottom: 2px solid transparent;
            padding: 6px 16px;
            color: #aaa;
            font-size: 13px;
            font-weight: bold;
        }
        QToolButton:hover {
            color: #e0e0e0;
            background-color: rgba(124, 77, 255, 0.1);
        }
        QToolButton:checked {
            color: white;
            border-bottom-color: #7C4DFF;
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
    m_contentStack->setStyleSheet("background-color: #1E1E1E;");

    m_contentStack->addWidget(createHomeView());
    m_contentStack->addWidget(m_cameraView);
    m_contentStack->addWidget(m_schemeView);
    m_contentStack->addWidget(m_editView);
    m_contentStack->addWidget(m_ioView);
    m_contentStack->addWidget(m_commView);
    m_contentStack->addWidget(m_monitorView);
    m_contentStack->addWidget(m_trainingView);
    // [零样本检测模块] 索引 8
    if (m_zeroShotView) {
        m_contentStack->addWidget(m_zeroShotView);
    } else {
        // 构造失败时的占位页：明确告知用户模块未加载，并引导查看日志
        QWidget* placeholder = new QWidget();
        QVBoxLayout* ph = new QVBoxLayout(placeholder);
        QLabel* tip = new QLabel(QStringLiteral(
            "⚠ 零样本检测模块未能加载\n\n"
            "可能原因：运行环境缺少模型源（LM Studio / 本地模型目录）或单例未就绪。\n"
            "请查看 logs 目录中的错误日志以定位根因。"), placeholder);
        tip->setAlignment(Qt::AlignCenter);
        tip->setWordWrap(true);
        tip->setStyleSheet("color:#ffb74d; font-size:13px; background-color:#1e1e1e; padding:24px;");
        ph->addWidget(tip);
        m_contentStack->addWidget(placeholder);
    }
}

QWidget* CentralWindow::createHomeView() {
    QWidget* homeWidget = new QWidget();
    homeWidget->setStyleSheet("background-color: #1E1E1E;");
    QVBoxLayout* homeLayout = new QVBoxLayout(homeWidget);
    homeLayout->setContentsMargins(48, 20, 48, 20);  // v5.0：全屏下左右 margin 增大，上下减小
    homeLayout->setSpacing(20);

    QLabel* titleLabel = new QLabel("欢迎使用奇测视觉检测系统");
    titleLabel->setStyleSheet("font-size: 26px; font-weight: bold; color: #e0e0e0; background: transparent; border: none;");
    homeLayout->addWidget(titleLabel);

    QLabel* subtitleLabel = new QLabel("选择下方功能模块开始操作");
    subtitleLabel->setStyleSheet("font-size: 14px; color: #aaa; background: transparent; border: none;");
    homeLayout->addWidget(subtitleLabel);

    QHBoxLayout* statRow = new QHBoxLayout();
    statRow->setSpacing(20);

    statRow->addWidget(createStatCardEx("检测方案", "0", "#42A5F5", m_statSchemeValue));
    statRow->addWidget(createStatCardEx("检测工具", "0", "#FFA726", m_statToolValue));
    statRow->addWidget(createStatCardEx("检测次数", "0", "#66BB6A", m_statDetectionValue));
    statRow->addWidget(createStatCardEx("告警信息", "0", "#EF5350", m_statAlertValue));

    // v5.0：统计卡片等宽拉伸，全屏下充分利用空间
    // v5.1 修复：QBoxLayout 用 setStretch(int index, int stretch) 而非 setStretchFactor
    for (int statIdx = 0; statIdx < statRow->count(); ++statIdx) {
        statRow->setStretch(statIdx, 1);
    }

    homeLayout->addLayout(statRow);

    QLabel* quickLabel = new QLabel("快速入口");
    quickLabel->setStyleSheet("font-size: 16px; font-weight: bold; color: #ccc; background: transparent; border: none; margin-top: 8px;");
    homeLayout->addWidget(quickLabel);

    QHBoxLayout* stepRow = new QHBoxLayout();
    stepRow->setSpacing(20);

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
    // v5.0：不设最小高度，让卡片根据内容自动拉伸
    card->setStyleSheet(R"(
        QWidget {
            background-color: #242424;
            border: 1px solid #3D3D3D;
            border-radius: 8px;
        }
    )");

    QVBoxLayout* cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(16, 12, 16, 12);
    cardLayout->setSpacing(4);

    QLabel* valueLabel = new QLabel(value);
    valueLabel->setStyleSheet(QString("font-size: 32px; font-weight: bold; color: %1; background: transparent; border: none;").arg(color));  // v5.1 融合优化：28→32px，提升数值醒目度
    outValueLabel = valueLabel;

    QLabel* titleLabel = new QLabel(title);
    titleLabel->setStyleSheet("font-size: 13px; color: #aaa; background: transparent; border: none;");

    cardLayout->addWidget(valueLabel);
    cardLayout->addWidget(titleLabel);

    return card;
}

QWidget* CentralWindow::createStepCard(const QString& number, const QString& title, const QString& description, int targetViewIndex) {
    QWidget* card = new QWidget();
    card->setMinimumHeight(90);  // v5.0
    card->setStyleSheet(R"(
        QWidget {
            background-color: #242424;
            border: 1px solid #3D3D3D;
            border-radius: 8px;
        }
    )");
    card->setCursor(Qt::PointingHandCursor);

    QVBoxLayout* cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(16, 12, 16, 12);
    cardLayout->setSpacing(4);

    QLabel* numLabel = new QLabel(number);
    numLabel->setStyleSheet("font-size: 24px; font-weight: bold; color: #7C4DFF; background: transparent; border: none;");  // v5.1 融合优化：22→24px，提升步骤编号可读性
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
    sep->setStyleSheet("background-color: #3D3D3D; border: none; max-height: 1px;");
    cardLayout->addWidget(sep);

    m_stepCardTargets[card] = targetViewIndex;

    card->installEventFilter(this);

    return card;
}

void CentralWindow::switchView(int index) {
    if (index < 0 || index >= m_contentStack->count()) return;
    const int oldIndex = m_contentStack->currentIndex();
    if (oldIndex == index) return;  // 避免重复切换

    // v5.3.2 备注：EditView 的 QRhi 纹理管理已内聚到 EditView::showEvent/hideEvent
    // 以及 QQuickWindow 的 sceneGraph 生命周期槽中。在 QStackedWidget 切换时会自动
    // 卸载/重新加载 QML，无需此处手动干预。

    // P1-C14 修复（交互评估 UI-005）：视图切换滑动动画
    // 之前 QStackedWidget::setCurrentIndex 是硬切，体验生硬
    // 现用 QPropertyAnimation 实现 200ms 水平滑动（新视图从右侧滑入）
    // 注意：EditView 内含 QQuickWidget，QGraphicsOpacityEffect 会引起渲染冲突，
    // 故采用 geometry 滑动而非 opacity 淡入。
    QWidget* newWidget = m_contentStack->widget(index);
    auto finalizeSwitch = [this, newWidget]() {
        // v3.2.1 统一修复：QStackedWidget 中的子视图在首次显示时，
        // 内部布局（QQuickWidget viewport / QSplitter sizes）可能尚未稳定，
        // 导致"未铺满"。在切换完成后强制发送一次 resize 事件，并刷新 layout。
        if (newWidget) {
            const QSize sz = m_contentStack->size();
            if (sz.isValid() && sz.width() > 0 && sz.height() > 0) {
                newWidget->setGeometry(m_contentStack->rect());
                newWidget->updateGeometry();
                if (QLayout* l = newWidget->layout()) {
                    l->invalidate();
                    l->activate();
                }
                QResizeEvent re(sz, QSize());
                QApplication::sendEvent(newWidget, &re);
            }
        }
    };

    if (newWidget && oldIndex >= 0) {
        const QRect targetGeometry = newWidget->geometry();
        const int xOffset = m_contentStack->width();
        // 起点向右偏移一个画布宽度
        newWidget->move(targetGeometry.x() + xOffset, targetGeometry.y());
        m_contentStack->setCurrentIndex(index);
        QPropertyAnimation* anim = new QPropertyAnimation(newWidget, "geometry", this);
        anim->setDuration(200);
        anim->setStartValue(QRect(targetGeometry.x() + xOffset, targetGeometry.y(),
                                  targetGeometry.width(), targetGeometry.height()));
        anim->setEndValue(targetGeometry);
        anim->setEasingCurve(QEasingCurve::OutCubic);
        connect(anim, &QPropertyAnimation::finished, anim, &QObject::deleteLater);
        connect(anim, &QPropertyAnimation::finished, this, finalizeSwitch);
        anim->start();
    } else {
        // 首次切换无前驱视图，直接显示
        m_contentStack->setCurrentIndex(index);
        finalizeSwitch();
    }

    for (auto it = navButtons.begin(); it != navButtons.end(); ++it) {
        it.value()->setChecked(it.key() == index);
    }

    QStringList viewNames = {"首页", "相机", "方案", "编辑", "IO监控", "通信", "监控", "训练推理", "零样本检测"};
    if (index >= 0 && index < viewNames.size()) {
        emit viewChanged(viewNames[index]);
        // v5.0：视图切换时发出状态栏提示
        emit statusMessageRequested(QString("已切换到：%1").arg(viewNames[index]));
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
            if (key >= Qt::Key_1 && key <= Qt::Key_9) {  // [零样本检测模块] 扩展到 Key_9（Alt+9 切换零样本检测）
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

void CentralWindow::onDetectionResult(const DetectionStats& stats) {
    if (m_monitorView) {
        m_monitorView->updateStats(stats);
    }
}