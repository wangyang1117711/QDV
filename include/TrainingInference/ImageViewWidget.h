#pragma once

#include <QWidget>
#include <QImage>
#include <QPixmap>
#include <QPoint>
#include <QList>
#include <QRubberBand>
#include <QColor>
#include <QMouseEvent>

struct Annotation {
    QRect rect;
    QString label;
    QColor color;
};

class ImageViewWidget : public QWidget {
    Q_OBJECT

public:
    explicit ImageViewWidget(QWidget* parent = nullptr);

    void setImage(const QImage& image);
    void clearImage();
    void setZoom(double zoom);
    double zoom() const;
    void fitToWindow();
    void actualSize();
    void rotateImage(double degrees);
    double rotation() const;
    void setCropMode(bool enabled);
    bool isCropMode() const;
    QRect cropRect() const;
    QImage croppedImage() const;

    void addAnnotation(const Annotation& annotation);
    void removeAnnotation(int index);
    void clearAnnotations();
    QList<Annotation> annotations() const;

    QImage currentImage() const;

signals:
    void annotationAdded(const Annotation& annotation);
    void zoomChanged(double zoom);
    void rotationChanged(double degrees);
    void cropApplied(const QRect& rect);

protected:
    void paintEvent(QPaintEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    QImage m_image;
    QList<Annotation> m_annotations;

    double m_zoom = 1.0;
    double m_rotation = 0.0;

    bool m_cropMode = false;
    bool m_isDrawing = false;
    QPoint m_drawStart;
    QPoint m_drawEnd;

    QPoint m_panStart;
    QPointF m_panOffset;
    bool m_isPanning = false;
};