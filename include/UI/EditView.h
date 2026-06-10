#ifndef EDITVIEW_H
#define EDITVIEW_H

#include <QWidget>
#include <QStackedWidget>
#include <QQuickWidget>

class EditViewBridge;      // v2.1.0 M2：C++ ↔ QML 桥

// v2.1.0 M7：全 QML 重构 - EditView 只承载 QQuickWidget。
// 经典 QWidget 工具库/画布/属性面板已删除（M6 起仅 QML 模式）。
// 数据源单一：EditViewBridge（m_currentNodes / m_connections）+ PropertyPreviewPanel。
class EditView : public QWidget {
    Q_OBJECT

public:
    explicit EditView(QWidget* parent = nullptr);
    ~EditView() override;

    /// v2.1.0 M2：访问 C++ ↔ QML 桥（CentralWindow/EditViewBridge 共享）
    EditViewBridge* bridge() const { return m_bridge; }

signals:
    void schemeModified();
    void toolCountChanged(int count);
    void requestRunDetection();

private:
    void setupQmlCanvas(QWidget* parent);

    QQuickWidget*   m_qmlCanvas    = nullptr;  ///< QML 画布（唯一渲染面）
    EditViewBridge* m_bridge       = nullptr;  ///< C++ ↔ QML 桥
};

#endif // EDITVIEW_H