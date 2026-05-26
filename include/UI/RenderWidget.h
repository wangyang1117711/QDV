#ifndef RENDERWIDGET_H
#define RENDERWIDGET_H

#include <QWidget>
#include <QImage>
#include <QColor>

class RenderWidget : public QWidget {
    Q_OBJECT

public:
    explicit RenderWidget(QWidget* parent = nullptr);
    ~RenderWidget() override;

    void setImage(const QImage& image);
    QImage image() const { return m_image; }
    void clearImage();

    void setZoom(double zoom);
    double zoom() const { return m_zoom; }
    void fitToWindow();

    void setShowROIs(bool show);
    bool showROIs() const { return m_showROIs; }

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    QImage m_image;
    QImage m_displayImage;
    double m_zoom = 1.0;
    bool m_showROIs = true;
};

#endif // RENDERWIDGET_H