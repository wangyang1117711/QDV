#include "EditView.h"
#include "EditViewBridge.h"
#include "Core/Logger.h"       // v5.1：QML 加载失败转发到 Logger

#include <QQuickWidget>
#include <QQmlContext>
#include <QQmlEngine>
#include <QUrl>
#include <QVBoxLayout>
#include <QLabel>              // v5.1：m_errorOverlay 错误覆盖层
#include <QDebug>
#include <QQuickItem>          // v5.1：首次加载后强制同步 root item 尺寸
#include <QResizeEvent>        // v5.1：手动触发 resize 事件
#include <QShowEvent>          // v5.3：显示/隐藏时管理 QRhi 纹理
#include <QHideEvent>
#include <QApplication>
#include <QTimer>              // v5.4：延迟加载 QML 确保 QRhi 上下文就绪

// v3.0.0 修复：静态库场景下 QML 类型显式注册。
// 静态库中 qmldir 声明的 singleton/type 可能被链接器优化丢弃，
// 必须在 C++ 端用 qmlRegisterSingletonType/qmlRegisterType 显式注册，
// 否则 QML 端 import 后无法找到 DesignTokens/ParamForm 等符号，
// 导致 EditView 界面空白。
static void registerQmlTypesForEditView() {
    // 模块 URI：QDV.EditView（QML 端用 import QDV.EditView 3.0 导入）
    const char* kModuleUri = "QDV.EditView";

    // 单例：DesignTokens（配色/间距/字体等设计 token）
    qmlRegisterSingletonType(
        QUrl(QStringLiteral("qrc:/qml/EditView/DesignTokens.qml")),
        kModuleUri, 3, 0, "DesignTokens");

    // 组件：ParamForm（参数动态表单）
    qmlRegisterType(
        QUrl(QStringLiteral("qrc:/qml/EditView/ParamForm.qml")),
        kModuleUri, 1, 0, "ParamForm");

    // 组件：PropertyPreviewPanel（右侧算子详情面板）
    qmlRegisterType(
        QUrl(QStringLiteral("qrc:/qml/EditView/PropertyPreviewPanel.qml")),
        kModuleUri, 1, 0, "PropertyPreviewPanel");

    // 组件：OperatorEditorDialog（算子参数模态编辑器）
    qmlRegisterType(
        QUrl(QStringLiteral("qrc:/qml/EditView/OperatorEditorDialog.qml")),
        kModuleUri, 1, 0, "OperatorEditorDialog");

    // v3.3.0 组件：OperatorTunerDialog（交互式调参对话框，双击算子打开）
    // 三处注册补全：qmldir + qrc + C++ qmlRegisterType，防止静态库场景符号丢失
    qmlRegisterType(
        QUrl(QStringLiteral("qrc:/qml/EditView/OperatorTunerDialog.qml")),
        kModuleUri, 1, 0, "OperatorTunerDialog");

    // 组件：ROISelector（ROI 区域选择器）
    qmlRegisterType(
        QUrl(QStringLiteral("qrc:/qml/EditView/ROISelector.qml")),
        kModuleUri, 1, 0, "ROISelector");

    // v3.1.0 新增组件
    // 组件：FloatingPanel（可浮动/靠边隐藏面板）
    qmlRegisterType(
        QUrl(QStringLiteral("qrc:/qml/EditView/FloatingPanel.qml")),
        kModuleUri, 1, 0, "FloatingPanel");

    // 组件：ImagePreviewWindow（独立图像预览浮窗）
    qmlRegisterType(
        QUrl(QStringLiteral("qrc:/qml/EditView/ImagePreviewWindow.qml")),
        kModuleUri, 1, 0, "ImagePreviewWindow");

    // 组件：AnnotateOverlay（图像标注覆盖层）
    qmlRegisterType(
        QUrl(QStringLiteral("qrc:/qml/EditView/AnnotateOverlay.qml")),
        kModuleUri, 1, 0, "AnnotateOverlay");

    // 组件：ResultTable（分析结果可视化表格）
    qmlRegisterType(
        QUrl(QStringLiteral("qrc:/qml/EditView/ResultTable.qml")),
        kModuleUri, 1, 0, "ResultTable");

    // v3.2.0 算子参数编辑器增强：文件路径输入组件
    // M8 修复：之前 qmldir 有注册但 .qrc 漏注册 + C++ 端未 qmlRegisterType，
    // 导致静态库场景下 QML 引擎找不到 FilePathField，ParamForm 加载失败，
    // 最终 PropertyPreviewPanel 加载失败，Main.qml 界面完全空白。
    // 显式 qmlRegisterType 可保证链接器不会丢符号。
    qmlRegisterType(
        QUrl(QStringLiteral("qrc:/qml/EditView/FilePathField.qml")),
        kModuleUri, 1, 0, "FilePathField");

    // v4.0 快捷键面板（P0 修复：三处注册补全 — qmldir + qrc + C++ qmlRegisterType）
    qmlRegisterType(
        QUrl(QStringLiteral("qrc:/qml/EditView/ShortcutsPanel.qml")),
        kModuleUri, 1, 0, "ShortcutsPanel");

    // v2.5.0 图像预览组件（补全 C++ 注册：静态库场景下链接器不会优化丢弃）
    qmlRegisterType(
        QUrl(QStringLiteral("qrc:/qml/EditView/ImageViewer.qml")),
        kModuleUri, 1, 0, "ImageViewer");

    // v2.5.0 部署结果对话框（补全 C++ 注册）
    qmlRegisterType(
        QUrl(QStringLiteral("qrc:/qml/EditView/DeployDialog.qml")),
        kModuleUri, 1, 0, "DeployDialog");

    // v2.6.0 预览窗口（P0 修复：三处注册补全 — qmldir + qrc + C++ qmlRegisterType）
    // 历史根因模式 #4：静态库场景下未 qmlRegisterType 的类型被链接器优化丢弃，
    // 导致 Main.qml 实例化 PreviewPanel 时找不到类型 → 连锁加载失败 → 编辑界面空白
    qmlRegisterType(
        QUrl(QStringLiteral("qrc:/qml/EditView/PreviewPanel.qml")),
        kModuleUri, 1, 0, "PreviewPanel");

    // v2.6.0 变量管理面板
    qmlRegisterType(
        QUrl(QStringLiteral("qrc:/qml/EditView/VariableManagerPanel.qml")),
        kModuleUri, 1, 0, "VariableManagerPanel");

    // v2.6.0 变量编辑对话框
    qmlRegisterType(
        QUrl(QStringLiteral("qrc:/qml/EditView/VarEditDialog.qml")),
        kModuleUri, 1, 0, "VarEditDialog");
}

// v2.1.0 M7：全 QML 重构。
// EditView 构造函数极简化：仅构造 EditViewBridge + 单一 QQuickWidget 承载 Main.qml。
// 所有交互（工具库/画布/属性/Undo/I/O）由 QML 端 + EditViewBridge 协同。
//
// v5.4 关键修复：构造函数中不再调用 setSource()。
// 根因：QQuickWidget 在构造时加载 QML，会在不可见状态下创建 QRhi 上下文和纹理。
// 当 EditView 首次被 QStackedWidget 显示时，QRhi 上下文已重建，
// 旧纹理被新 QRhi 使用时报 "Texture belongs to QRhi A but attempted with QRhi B"。
// 修复：构造函数只创建 QQuickWidget 容器，QML 延迟到首次 showEvent 时加载。
EditView::EditView(QWidget* parent) : QWidget(parent) {
    // 构造 C++ ↔ QML 桥
    m_bridge = new EditViewBridge(this);

    // v2.1.0 M7：转发 bridge 的 currentNodesChanged 为 toolCountChanged 信号，
    // 维持 CentralWindow::updateToolCount 槽的契约（Home 视图显示工具数）。
    connect(m_bridge, &EditViewBridge::currentNodesChanged, this, [this]() {
        emit toolCountChanged(m_bridge->currentNodes().size());
    });

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // 单一 QQuickWidget 占据整个 EditView（但不立即加载 QML）
    setupQmlCanvas(this);
    mainLayout->addWidget(m_qmlCanvas);
}

EditView::~EditView() = default;

void EditView::setupQmlCanvas(QWidget* parent) {
    Q_UNUSED(parent);
    if (!m_bridge) {
        qWarning() << "[EditView] setupQmlCanvas: bridge is null";
        return;
    }

    m_qmlCanvas = new QQuickWidget();
    // 关键修复：让 QML root object 跟随 QQuickWidget 尺寸（窗口最大化时铺满）。
    m_qmlCanvas->setResizeMode(QQuickWidget::SizeRootObjectToView);
    m_qmlCanvas->setClearColor(QColor("#1E1E1E"));
    m_qmlCanvas->setAttribute(Qt::WA_TranslucentBackground, false);
    m_qmlCanvas->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    QQmlContext* ctx = m_qmlCanvas->rootContext();
    if (ctx) {
        ctx->setContextProperty("editViewBridge", m_bridge);
    }

    // v2.1.0 M4：初始化 QRC 资源（静态库必须显式初始化）
    Q_INIT_RESOURCE(EditView);

    // v3.0.0 修复：静态库中 qmldir 声明的 singleton/type 注册
    registerQmlTypesForEditView();

    // v5.1 永久修复：初始化错误覆盖层（在 setSource 之前创建，确保首次 statusChanged 可用）
    setupErrorOverlay();

    // v5.1 永久修复：连接 statusChanged 信号，替代原先的 if 判断。
    // 原先仅 qWarning 打印错误，用户看到"空白"无任何提示，导致同一 bug 反复出现 5-10 次。
    // 现在由 onQmlStatusChanged 统一处理：Ready→隐藏覆盖层；Error→显示红色错误详情。
    connect(m_qmlCanvas, &QQuickWidget::statusChanged,
            this, &EditView::onQmlStatusChanged);

    // v5.4 修复：不在构造函数中加载 QML。
    // setSource() 延迟到 showEvent 中调用，确保 QQuickWidget 的 QRhi 上下文已就绪。
    // 这彻底解决了 "Texture belongs to QRhi A but attempted with QRhi B" 错误。
    qDebug() << "[EditView] QQuickWidget created, QML loading deferred to showEvent";
}

// v5.1 永久修复：初始化 QML 加载失败错误覆盖层
void EditView::setupErrorOverlay() {
    m_errorOverlay = new QLabel(this);
    m_errorOverlay->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_errorOverlay->setStyleSheet(
        "QLabel { background-color: #2a1a1a; color: #ff6b6b; "
        "font-family: Consolas, 'Microsoft YaHei UI'; font-size: 12px; "
        "padding: 12px; border: 1px solid #5a2a2a; }");
    m_errorOverlay->setWordWrap(true);
    m_errorOverlay->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_errorOverlay->hide();
}

// v5.1 永久修复：统一处理 QML 加载状态
void EditView::onQmlStatusChanged(QQuickWidget::Status status) {
    if (status == QQuickWidget::Status::Ready) {
        hideErrorOverlay();
        qDebug() << "[EditView] QML loaded successfully. status=Ready";

        // v3.2.1 修复：Main.qml 根 Rectangle 已改为 anchors.fill: parent，
        // 理论上 SizeRootObjectToView 会自动占满 viewport。但在 QStackedWidget
        // 中首次显示时，QQuickWidget 的尺寸可能尚未稳定，这里保留双重保险：
        // 1) Ready 时立即同步一次；2) 延迟到下一事件循环再同步一次，确保布局完成。
        if (m_qmlCanvas) {
            auto syncRootSize = [this]() {
                QQuickItem* rootItem = m_qmlCanvas->rootObject();
                if (!rootItem) return;
                const QSize viewportSize = m_qmlCanvas->size();
                if (viewportSize.width() <= 0 || viewportSize.height() <= 0) return;
                // 只有当 root item 未占满 viewport 时才重置
                if (!qFuzzyCompare(rootItem->width(), viewportSize.width()) ||
                    !qFuzzyCompare(rootItem->height(), viewportSize.height())) {
                    qDebug() << "[EditView] sync root item size to:" << viewportSize;
                    rootItem->setWidth(viewportSize.width());
                    rootItem->setHeight(viewportSize.height());
                }
                // 额外触发一次 resize 事件，确保 QML 内部 SplitView 重新计算
                QResizeEvent re(viewportSize, QSize());
                QApplication::sendEvent(m_qmlCanvas, &re);
            };
            syncRootSize();
            QTimer::singleShot(0, this, syncRootSize);
        }
    } else if (status == QQuickWidget::Status::Error) {
        const auto errors = m_qmlCanvas->errors();
        QString errorText = QStringLiteral("⚠️ EditView QML 加载失败\n\n");
        for (int i = 0; i < errors.size() && i < 5; ++i) {
            errorText += QStringLiteral("[%1] %2\n").arg(i + 1).arg(errors[i].toString());
        }
        if (errors.size() > 5) {
            errorText += QStringLiteral("\n... 共 %1 条错误，选中可复制\n").arg(errors.size());
        }
        showErrorOverlay(errorText);

        // 转发到 Logger（持久化日志）
        QDV::Logger::error(QStringLiteral("[EditView] QML load FAILED:"));
        for (const auto& err : errors) {
            QDV::Logger::error(QStringLiteral("  ") + err.toString());
        }

        // 转发到 editViewBridge.errorRaised（QML 端可显示 Toast）
        if (m_bridge) {
            emit m_bridge->errorRaised(QStringLiteral("QML_LOAD"), errorText);
        }
    }
}

void EditView::showErrorOverlay(const QString& errorText) {
    if (!m_errorOverlay) return;
    m_errorOverlay->setText(errorText);
    m_errorOverlay->setGeometry(this->rect());
    m_errorOverlay->raise();
    m_errorOverlay->show();
}

void EditView::hideErrorOverlay() {
    if (m_errorOverlay) {
        m_errorOverlay->hide();
    }
}

// v5.4：加载 QML。
// 从 showEvent 调用，此时 QQuickWidget 已附加到可见顶层窗口，
// QRhi 上下文已就绪，纹理将创建在正确的 QRhi 实例上。
void EditView::loadQml() {
    if (!m_qmlCanvas || m_qmlLoaded) return;

    qDebug() << "[EditView] loadQml: loading Main.qml";
    const QUrl mainQmlUrl(QStringLiteral("qrc:/qml/EditView/Main.qml"));
    m_qmlCanvas->setSource(mainQmlUrl);
    m_qmlLoaded = true;
}

// v5.4：卸载 QML。
// 从 hideEvent 调用，释放所有 QSG 纹理和 QRhi 资源，
// 防止下次显示时旧纹理被新 QRhi 误用。
void EditView::unloadQml() {
    if (!m_qmlCanvas) return;
    if (!m_qmlCanvas->source().isEmpty()) {
        qDebug() << "[EditView] unloadQml: releasing QML scene and QRhi textures";
        m_qmlCanvas->setSource(QUrl());
    }
    m_qmlLoaded = false;
}

// v5.3.2：保留空实现，实际逻辑已下沉到 showEvent/hideEvent。
void EditView::prepareForHide() {
    // no-op: 由 hideEvent 统一处理 QML 卸载
}

void EditView::prepareForShow() {
    // no-op: 由 showEvent 统一处理 QML 加载
}

// v5.4 核心修复：EditView 显示时加载 QML。
// 首次显示：QML 从未加载过，此时加载确保 QRhi 上下文正确。
// 再次显示：QML 已被 hideEvent 卸载，重新加载。
// 使用 QTimer::singleShot(0) 延迟到下一个事件循环，
// 确保 QQuickWidget 的 QRhi 上下文已完全初始化。
void EditView::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);

    if (!m_qmlCanvas) return;

    // 延迟到下一个事件循环，确保 QQuickWidget 的 QRhi 上下文已就绪。
    // 直接在 showEvent 中调用 setSource 有时会导致 QRhi 尚未完成初始化。
    QTimer::singleShot(0, this, [this]() {
        if (!m_qmlCanvas) return;
        // 无论是否已加载，只要 source 为空就重新加载
        if (m_qmlCanvas->source().isEmpty()) {
            qDebug() << "[EditView] showEvent: loading QML (deferred)";
            loadQml();
        }
    });
}

// v5.4 核心修复：EditView 隐藏时卸载 QML。
// 释放所有 QSG 纹理和 QRhi 资源，防止下次显示时旧纹理被新 QRhi 误用。
void EditView::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
    unloadQml();
}
