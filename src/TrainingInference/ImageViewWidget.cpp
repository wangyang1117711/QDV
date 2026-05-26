#include "TrainingInference/ImageViewWidget.h"
#include <QPainter>
#include <QPaintEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QtMath>

ImageViewWidget::ImageViewWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(320, 240);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setStyleSheet("background-color: #1e1e1e; border: 1px solid #444;");
}

void ImageViewWidget::setImage(const QImage& image)
{
    m_image = image;
    m_zoom = 1.0;
    m_rotation = 0.0;
    m_panOffset = QPointF(0, 0);
    fitToWindow();
    update();
}

void ImageViewWidget::clearImage()
{
    m_image = QImage();
    clearAnnotations();
    update();
}

void ImageViewWidget::setZoom(double zoom)
{
    m_zoom = qBound(0.10, zoom, 4.0);
    emit zoomChanged(m_zoom);
    update();
}

double ImageViewWidget::zoom() const
{
    return m_zoom;
}

void ImageViewWidget::fitToWindow()
{
    if (m_image.isNull()) return;
    double scaleX = static_cast<double>(width()) / m_image.width();
    double scaleY = static_cast<double>(height()) / m_image.height();
    m_zoom = qMin(scaleX, scaleY) * 0.9;
    m_zoom = qBound(0.10, m_zoom, 4.0);
    m_panOffset = QPointF(0, 0);
    emit zoomChanged(m_zoom);
    update();
}

void ImageViewWidget::actualSize()
{
    m_zoom = 1.0;
    m_panOffset = QPointF(0, 0);
    emit zoomChanged(m_zoom);
    update();
}

void ImageViewWidget::rotateImage(double degrees)
{
    m_rotation = fmod(m_rotation + degrees, 360.0);
    emit rotationChanged(m_rotation);
    update();
}

double ImageViewWidget::rotation() const
{
    return m_rotation;
}

void ImageViewWidget::setCropMode(bool enabled)
{
    m_cropMode = enabled;
    if (enabled)
    {
        setCursor(Qt::CrossCursor);
    }
    else
    {
        setCursor(Qt::ArrowCursor);
    }
    m_isDrawing = false;
}

bool ImageViewWidget::isCropMode() const
{
    return m_cropMode;
}

QRect ImageViewWidget::cropRect() const
{
    return QRect(m_drawStart, m_drawEnd).normalized();
}

QImage ImageViewWidget::croppedImage() const
{
    QRect rect = cropRect();
    if (rect.isEmpty()) return QImage();
    return m_image.copy(rect);
}

void ImageViewWidget::addAnnotation(const Annotation& annotation)
{
    m_annotations.append(annotation);
    emit annotationAdded(annotation);
    update();
}

void ImageViewWidget::removeAnnotation(int index)
{
    if (index >= 0 && index < m_annotations.size())
    {
        m_annotations.removeAt(index);
        update();
    }
}

void ImageViewWidget::clearAnnotations()
{
    m_annotations.clear();
    update();
}

QList<Annotation> ImageViewWidget::annotations() const
{
    return m_annotations;
}

QImage ImageViewWidget::currentImage() const
{
    return m_image;
}

void ImageViewWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.fillRect(rect(), QColor(30, 30, 30));

    if (m_image.isNull()) return;

    QTransform transform;
    transform.translate(width() / 2.0 + m_panOffset.x(), height() / 2.0 + m_panOffset.y());
    transform.rotate(m_rotation);
    transform.scale(m_zoom, m_zoom);
    transform.translate(-m_image.width() / 2.0, -m_image.height() / 2.0);

    painter.setTransform(transform);
    painter.drawImage(0, 0, m_image);

    if (m_cropMode && m_isDrawing)
    {
        QColor cropColor(102, 8, 116, 100);
        QPen cropPen(QColor(102, 8, 116), 2, Qt::DashLine);

        QTransform invTransform = transform.inverted();
        QPoint p1 = invTransform.map(m_drawStart).toPoint();
        QPoint p2 = invTransform.map(m_drawEnd).toPoint();
        QRect cropRect = QRect(p1, p2).normalized();

        painter.setTransform(QTransform());
        painter.setPen(cropPen);
        painter.setBrush(cropColor);
        painter.drawRect(cropRect);
    }

    for (const Annotation& ann : m_annotations)
    {
        QPen pen(ann.color, 2);
        painter.setTransform(transform);
        painter.setPen(pen);
        painter.setBrush(QColor(ann.color.red(), ann.color.green(), ann.color.blue(), 40));
        painter.drawRect(ann.rect);
    }
}

void ImageViewWidget::wheelEvent(QWheelEvent* event)
{
    double factor = event->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15;
    setZoom(m_zoom * factor);
}

void ImageViewWidget::keyPressEvent(QKeyEvent* event)
{
    if (event->modifiers() & Qt::ControlModifier)
    {
        switch (event->key())
        {
        case Qt::Key_0:
            fitToWindow();
            break;
        case Qt::Key_1:
            actualSize();
            break;
        default:
            break;
        }
    }
    QWidget::keyPressEvent(event);
}

void ImageViewWidget::mousePressEvent(QMouseEvent* event)
{
    if (m_cropMode && event->button() == Qt::LeftButton)
    {
        m_isDrawing = true;
        m_drawStart = event->pos();
        m_drawEnd = event->pos();
    }
    else if (event->button() == Qt::MiddleButton)
    {
        m_isPanning = true;
        m_panStart = event->pos();
    }
}

void ImageViewWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (m_cropMode && m_isDrawing)
    {
        m_drawEnd = event->pos();
        update();
    }
    else if (m_isPanning)
    {
        QPoint delta = event->pos() - m_panStart;
        m_panOffset += QPointF(delta.x(), delta.y());
        m_panStart = event->pos();
        update();
    }
}

void ImageViewWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_cropMode && m_isDrawing && event->button() == Qt::LeftButton)
    {
        m_isDrawing = false;
        m_drawEnd = event->pos();
        QRect rect = cropRect();
        if (rect.width() > 4 && rect.height() > 4)
        {
            emit cropApplied(rect);
        }
    }
    else if (event->button() == Qt::MiddleButton)
    {
        m_isPanning = false;
    }
}