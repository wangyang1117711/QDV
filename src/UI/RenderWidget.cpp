#include "RenderWidget.h"
#include <QPainter>
#include <QPaintEvent>

RenderWidget::RenderWidget(QWidget* parent) : QWidget(parent) {
    setMinimumSize(320, 240);
    setStyleSheet("background-color: #1e1e1e;");
}

RenderWidget::~RenderWidget() = default;

void RenderWidget::setImage(const QImage& image) {
    m_image = image;
    update();
}

void RenderWidget::clearImage() {
    m_image = QImage();
    m_displayImage = QImage();
    update();
}

void RenderWidget::setZoom(double zoom) {
    m_zoom = zoom;
    update();
}

void RenderWidget::fitToWindow() {
    if (m_image.isNull()) return;
    double scaleX = static_cast<double>(width()) / m_image.width();
    double scaleY = static_cast<double>(height()) / m_image.height();
    m_zoom = qMin(scaleX, scaleY);
    update();
}

void RenderWidget::setShowROIs(bool show) {
    m_showROIs = show;
    update();
}

void RenderWidget::paintEvent(QPaintEvent* event) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    
    QRect paintRect = event->rect();
    painter.fillRect(paintRect, QColor(30, 30, 30));

    if (!m_image.isNull()) {
        QImage scaled = m_image.scaled(m_image.width() * m_zoom,
                                       m_image.height() * m_zoom,
                                       Qt::KeepAspectRatio,
                                       Qt::SmoothTransformation);
        m_displayImage = scaled;

        int x = (width() - scaled.width()) / 2;
        int y = (height() - scaled.height()) / 2;
        
        painter.drawImage(x, y, scaled);
    } else {
        painter.setPen(QColor(100, 100, 100));
        painter.drawText(rect(), Qt::AlignCenter, "No Image");
    }
}

void RenderWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
}