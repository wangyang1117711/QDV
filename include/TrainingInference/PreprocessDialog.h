#pragma once

#include <QDialog>
#include <QSlider>
#include <QLabel>
#include <QImage>

class QSpinBox;
class QDoubleSpinBox;
class ImageViewWidget;

struct PreprocessParams {
    int brightness = 0;
    double contrast = 1.0;
    double saturation = 1.0;
    int hue = 0;
    double sharpness = 0.0;
    double gamma = 1.0;

    void reset() {
        brightness = 0;
        contrast = 1.0;
        saturation = 1.0;
        hue = 0;
        sharpness = 0.0;
        gamma = 1.0;
    }
};

class PreprocessDialog : public QDialog {
    Q_OBJECT

public:
    PreprocessDialog(const QImage& image, QWidget* parent = nullptr);

    PreprocessParams currentParams() const;
    QImage processedImage() const;

signals:
    void applyRequested(const QImage& image);
    void paramsChanged(const PreprocessParams& params);

private:
    void setupUI(const QSize& imageSize);
    QWidget* createIntSliderRow(const QString& label, int min, int max,
                                int step, int defaultValue,
                                QSlider*& slider, QSpinBox*& spinBox);
    QWidget* createDoubleSliderRow(const QString& label, double min, double max,
                                   double step, double defaultValue,
                                   QSlider*& slider, QDoubleSpinBox*& spinBox);
    void updatePreview();

private slots:
    void onSliderBySpinBox(int value);
    void onBrightnessChanged(int value);
    void onContrastChanged(double value);
    void onSaturationChanged(double value);
    void onHueChanged(int value);
    void onSharpnessChanged(double value);
    void onGammaChanged(double value);
    void onReset();
    void onApply();

private:
    QImage m_sourceImage;
    QImage m_processedImage;
    PreprocessParams m_params;

    QSlider* m_brightnessSlider = nullptr;
    QSlider* m_contrastSlider = nullptr;
    QSlider* m_saturationSlider = nullptr;
    QSlider* m_hueSlider = nullptr;
    QSlider* m_sharpnessSlider = nullptr;
    QSlider* m_gammaSlider = nullptr;
    QSpinBox* m_brightnessSpin = nullptr;
    QSpinBox* m_hueSpin = nullptr;
    QDoubleSpinBox* m_contrastSpin = nullptr;
    QDoubleSpinBox* m_saturationSpin = nullptr;
    QDoubleSpinBox* m_sharpnessSpin = nullptr;
    QDoubleSpinBox* m_gammaSpin = nullptr;
    ImageViewWidget* m_previewWidget = nullptr;
    QLabel* m_infoLabel = nullptr;
};