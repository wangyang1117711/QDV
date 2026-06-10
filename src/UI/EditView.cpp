#include "EditView.h"
#include "EditViewBridge.h"

#include <QQuickWidget>
#include <QQmlContext>
#include <QQmlEngine>
#include <QUrl>
#include <QVBoxLayout>
#include <QDebug>

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
}

// v2.1.0 M7：全 QML 重构。
// EditView 构造函数极简化：仅构造 EditViewBridge + 单一 QQuickWidget 承载 Main.qml。
// 所有交互（工具库/画布/属性/Undo/I/O）由 QML 端 + EditViewBridge 协同。
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

    // 单一 QQuickWidget 占据整个 EditView
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
    m_qmlCanvas->setClearColor(QColor("#1E1E1E"));
    m_qmlCanvas->setAttribute(Qt::WA_TranslucentBackground, false);

    QQmlContext* ctx = m_qmlCanvas->rootContext();
    if (ctx) {
        ctx->setContextProperty("editViewBridge", m_bridge);
    }

    // v2.1.0 M4：初始化 QRC 资源（静态库必须显式初始化
    Q_INIT_RESOURCE(EditView);

    // v3.0.0 修复：静态库中 qmldir 声明的 singleton/type 注册
    registerQmlTypesForEditView();

    // 加载 qrc:/qml/EditView/Main.qml
    const QUrl mainQmlUrl(QStringLiteral("qrc:/qml/EditView/Main.qml"));
    qDebug() << "[EditView] Loading QML from:" << mainQmlUrl.toString();
    m_qmlCanvas->setSource(mainQmlUrl);
    if (m_qmlCanvas->status() == QQuickWidget::Error) {
        const auto errors = m_qmlCanvas->errors();
        qWarning() << "[EditView] QML load FAILED. status=" << m_qmlCanvas->status();
        for (const auto& err : errors) {
            qWarning() << "  " << err.toString();
        }
    } else {
        qDebug() << "[EditView] QML loaded successfully. status=" << m_qmlCanvas->status();
    }
}
