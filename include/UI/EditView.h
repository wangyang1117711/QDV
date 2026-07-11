#ifndef EDITVIEW_H
#define EDITVIEW_H

#include <QWidget>
#include <QStackedWidget>
#include <QQuickWidget>

class QLabel;                // v5.1 错误覆盖层前置声明
class EditViewBridge;      // v2.1.0 M2：C++ ↔ QML 桥

// v2.1.0 M7：全 QML 重构 - EditView 只承载 QQuickWidget。
// 经典 QWidget 工具库/画布/属性面板已删除（M6 起仅 QML 模式）。
// 数据源单一：EditViewBridge（m_currentNodes / m_connections）+ PropertyPreviewPanel。
//
// v5.1 永久修复：新增 m_errorOverlay 错误覆盖层。
// 历史根因：QML 加载失败仅 qWarning 打印，用户看到"空白"无任何提示，
// 导致同一空白 bug 反复出现 5-10 次。现在 status==Error 时显示红色覆盖层。
//
// v5.4 彻底修复 QRhi 跨实例纹理错误：
// 根因：QQuickWidget 在构造函数中 setSource() 时，EditView 尚未可见，
// 纹理创建在临时 QRhi 实例上。当 EditView 首次显示时 QRhi 实例已变化，
// 旧纹理被新 QRhi 使用时报 "Texture belongs to QRhi A but attempted with QRhi B"。
// 修复：延迟 QML 加载到首次 showEvent，确保纹理始终在正确的 QRhi 上创建。
// 隐藏时卸载 QML 释放纹理，再次显示时重新加载。
class EditView : public QWidget {
    Q_OBJECT

public:
    explicit EditView(QWidget* parent = nullptr);
    ~EditView() override;

    /// v2.1.0 M2：访问 C++ ↔ QML 桥（CentralWindow/EditViewBridge 共享）
    EditViewBridge* bridge() const { return m_bridge; }

    // v5.3 彻底修复 QRhi 跨实例错误：
    // QQuickWidget 嵌入 QStackedWidget 时，隐藏/显示会重建 RHI 上下文，
    // 旧 QSG 纹理引用导致 "Texture belongs to QRhi A but client code attempted
    // to use it with QRhi B"。在切换出编辑视图前卸载 QML，切换回时重新加载，
    // 确保所有纹理在新 RHI 上下文上创建。
    void prepareForHide();
    void prepareForShow();

signals:
    void schemeModified();
    void toolCountChanged(int count);
    void requestRunDetection();

private slots:
    // v5.1 永久修复：统一处理 QML 加载状态，替代原 setupQmlCanvas 中的 if 判断
    void onQmlStatusChanged(QQuickWidget::Status status);

private:
    // v5.3：重写显示/隐藏事件，管理 QRhi 纹理生命周期
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

    void setupQmlCanvas(QWidget* parent);
    void setupErrorOverlay();                                   // v5.1：初始化错误覆盖层
    void showErrorOverlay(const QString& errorText);            // v5.1：显示错误详情
    void hideErrorOverlay();                                    // v5.1：隐藏覆盖层

    // v5.4：加载 QML（从 showEvent 调用，确保 QRhi 上下文已就绪）
    void loadQml();
    // v5.4：卸载 QML（从 hideEvent 调用，释放所有 QSG 纹理）
    void unloadQml();

    QQuickWidget*   m_qmlCanvas    = nullptr;  ///< QML 画布（唯一渲染面）
    EditViewBridge* m_bridge       = nullptr;  ///< C++ ↔ QML 桥
    QLabel*         m_errorOverlay = nullptr;  ///< v5.1：QML 加载失败时显示的红色覆盖层
    bool            m_qmlLoaded    = false;    ///< v5.4：QML 是否已加载（防止重复加载）
};

#endif // EDITVIEW_H
