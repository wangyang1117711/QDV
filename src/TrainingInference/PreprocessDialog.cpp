#include "TrainingInference/PreprocessDialog.h"
#include "TrainingInference/ImageViewWidget.h"
#include "TrainingInference/ImagePreprocessor.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QFormLayout>
#include <QScrollArea>
#include <QSplitter>
#include <QPushButton>
#include <QSpinBox>
#include <QDoubleSpinBox>

PreprocessDialog::PreprocessDialog(const QImage& image, QWidget* parent)
    : QDialog(parent), m_sourceImage(image)
{
    m_processedImage = image;
    setupUI(image.size());
}

void PreprocessDialog::setupUI(const QSize& imageSize) {
    setWindowTitle("图像预处理");
    setMinimumSize(800, 600);
    setStyleSheet(
        "QDialog { background-color: #2d2d2d; color: #e0e0e0; }"
        "QGroupBox { color: #e0e0e0; border: 1px solid #444; "
        "border-radius: 4px; margin-top: 8px; padding-top: 12px; "
        "background-color: #333; }"
    );

    QHBoxLayout* mainLayout = new QHBoxLayout(this);
    mainLayout->setSpacing(12);

    QWidget* controlPanel = new QWidget();
    controlPanel->setFixedWidth(340);
    QVBoxLayout* controlLayout = new QVBoxLayout(controlPanel);
    controlLayout->setSpacing(8);

    QGroupBox* paramGroup = new QGroupBox("参数调整");
    QVBoxLayout* paramLayout = new QVBoxLayout(paramGroup);

    paramLayout->addWidget(createIntSliderRow("亮度", -100, 100, 1, 0,
                           m_brightnessSlider, m_brightnessSpin));
    paramLayout->addWidget(createDoubleSliderRow("对比度", 0.0, 3.0, 0.01, 1.0,
                           m_contrastSlider, m_contrastSpin));
    paramLayout->addWidget(createDoubleSliderRow("饱和度", 0.0, 3.0, 0.01, 1.0,
                           m_saturationSlider, m_saturationSpin));
    paramLayout->addWidget(createIntSliderRow("色相", -180, 180, 1, 0,
                           m_hueSlider, m_hueSpin));
    paramLayout->addWidget(createDoubleSliderRow("锐化", 0.0, 2.0, 0.1, 0.0,
                           m_sharpnessSlider, m_sharpnessSpin));
    paramLayout->addWidget(createDoubleSliderRow("伽马", 0.1, 5.0, 0.1, 1.0,
                           m_gammaSlider, m_gammaSpin));

    controlLayout->addWidget(paramGroup);

    m_infoLabel = new QLabel();
    m_infoLabel->setStyleSheet("color: #888; font-size: 12px; padding: 4px;");
    m_infoLabel->setWordWrap(true);
    controlLayout->addWidget(m_infoLabel);

    QHBoxLayout* btnLayout = new QHBoxLayout();
    QPushButton* resetBtn = new QPushButton("重置");
    resetBtn->setStyleSheet(
        "QPushButton { background-color: #444; border: 1px solid #555; "
        "border-radius: 3px; padding: 6px 16px; color: #e0e0e0; }"
        "QPushButton:hover { background-color: #555; }"
    );
    connect(resetBtn, &QPushButton::clicked, this, &PreprocessDialog::onReset);

    QPushButton* applyBtn = new QPushButton("应用");
    applyBtn->setStyleSheet(
        "QPushButton { background-color: #7C4DFF; color: white; border: none; "
        "border-radius: 4px; padding: 8px 24px; font-weight: bold; }"
        "QPushButton:hover { background-color: #8E66FF; }"
    );
    connect(applyBtn, &QPushButton::clicked, this, &PreprocessDialog::onApply);

    btnLayout->addWidget(resetBtn);
    btnLayout->addStretch();
    btnLayout->addWidget(applyBtn);
    controlLayout->addLayout(btnLayout);

    controlLayout->addStretch();
    mainLayout->addWidget(controlPanel);

    m_previewWidget = new ImageViewWidget();
    m_previewWidget->setImage(m_sourceImage);
    m_previewWidget->setMinimumWidth(400);
    mainLayout->addWidget(m_previewWidget, 1);

    updatePreview();
}

QWidget* PreprocessDialog::createIntSliderRow(const QString& label, int min, int max,
                                                 int step, int defaultValue,
                                                 QSlider*& slider, QSpinBox*& spinBox) {
    QWidget* row = new QWidget();
    QHBoxLayout* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 2, 0, 2);
    layout->setSpacing(8);

    QLabel* nameLabel = new QLabel(label);
    nameLabel->setFixedWidth(50);
    nameLabel->setStyleSheet("color: #ccc; font-size: 13px;");
    layout->addWidget(nameLabel);

    slider = new QSlider(Qt::Horizontal);
    slider->setMinimumWidth(100);
    slider->setRange(min, max);
    slider->setSingleStep(step);
    slider->setValue(defaultValue);
    slider->setStyleSheet(
        "QSlider::groove:horizontal { border: 1px solid #555; height: 6px; "
        "background: #252525; border-radius: 3px; }"
        "QSlider::handle:horizontal { background: #7C4DFF; border: 1px solid #8E66FF; "
        "width: 14px; margin: -4px 0; border-radius: 7px; }"
        "QSlider::handle:horizontal:hover { background: #8E66FF; }"
    );
    layout->addWidget(slider, 1);

    spinBox = new QSpinBox();
    spinBox->setRange(min, max);
    spinBox->setSingleStep(step);
    spinBox->setValue(defaultValue);
    spinBox->setFixedWidth(70);
    spinBox->setStyleSheet(
        "QSpinBox { background-color: #252525; border: 1px solid #555; "
        "border-radius: 3px; padding: 2px 4px; color: #e0e0e0; }"
    );

    connect(slider, &QSlider::valueChanged, spinBox, &QSpinBox::setValue);
    connect(spinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            slider, &QSlider::setValue);

    layout->addWidget(spinBox);

    return row;
}

QWidget* PreprocessDialog::createDoubleSliderRow(const QString& label, double min, double max,
                                                  double step, double defaultValue,
                                                  QSlider*& slider, QDoubleSpinBox*& spinBox) {
    QWidget* row = new QWidget();
    QHBoxLayout* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 2, 0, 2);
    layout->setSpacing(8);

    QLabel* nameLabel = new QLabel(label);
    nameLabel->setFixedWidth(50);
    nameLabel->setStyleSheet("color: #ccc; font-size: 13px;");
    layout->addWidget(nameLabel);

    int sliderSteps = static_cast<int>((max - min) / step);
    slider = new QSlider(Qt::Horizontal);
    slider->setMinimumWidth(100);
    slider->setRange(0, sliderSteps);
    int defaultStep = static_cast<int>((defaultValue - min) / step);
    slider->setValue(defaultStep);
    slider->setStyleSheet(
        "QSlider::groove:horizontal { border: 1px solid #555; height: 6px; "
        "background: #252525; border-radius: 3px; }"
        "QSlider::handle:horizontal { background: #7C4DFF; border: 1px solid #8E66FF; "
        "width: 14px; margin: -4px 0; border-radius: 7px; }"
        "QSlider::handle:horizontal:hover { background: #8E66FF; }"
    );
    layout->addWidget(slider, 1);

    spinBox = new QDoubleSpinBox();
    spinBox->setRange(min, max);
    spinBox->setSingleStep(step);
    spinBox->setDecimals(2);
    spinBox->setValue(defaultValue);
    spinBox->setFixedWidth(70);
    spinBox->setStyleSheet(
        "QDoubleSpinBox { background-color: #252525; border: 1px solid #555; "
        "border-radius: 3px; padding: 2px 4px; color: #e0e0e0; }"
    );

    connect(slider, &QSlider::valueChanged, [slider, spinBox, min, step](int) {
        double val = min + slider->value() * step;
        spinBox->blockSignals(true);
        spinBox->setValue(val);
        spinBox->blockSignals(false);
    });
    connect(spinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            [slider, min, step](double val) {
        int sliderVal = static_cast<int>((val - min) / step);
        slider->blockSignals(true);
        slider->setValue(sliderVal);
        slider->blockSignals(false);
    });

    layout->addWidget(spinBox);

    return row;
}

void PreprocessDialog::onSliderBySpinBox(int value) {
    Q_UNUSED(value);
    m_params.brightness = m_brightnessSlider->value();
    m_params.contrast = 0.0 + m_contrastSlider->value() * 0.01;
    m_params.saturation = 0.0 + m_saturationSlider->value() * 0.01;
    m_params.hue = m_hueSlider->value();
    m_params.sharpness = 0.0 + m_sharpnessSlider->value() * 0.1;
    m_params.gamma = 0.1 + m_gammaSlider->value() * 0.1;
    updatePreview();
}

void PreprocessDialog::onBrightnessChanged(int value) {
    m_params.brightness = value;
    updatePreview();
}

void PreprocessDialog::onContrastChanged(double value) {
    m_params.contrast = value;
    updatePreview();
}

void PreprocessDialog::onSaturationChanged(double value) {
    m_params.saturation = value;
    updatePreview();
}

void PreprocessDialog::onHueChanged(int value) {
    m_params.hue = value;
    updatePreview();
}

void PreprocessDialog::onSharpnessChanged(double value) {
    m_params.sharpness = value;
    updatePreview();
}

void PreprocessDialog::onGammaChanged(double value) {
    m_params.gamma = value;
    updatePreview();
}

void PreprocessDialog::updatePreview() {
    m_processedImage = ImagePreprocessor::instance()->process(m_sourceImage, m_params);
    m_previewWidget->setImage(m_processedImage);

    m_infoLabel->setText(QString("亮度:%1 对比度:%2 饱和度:%3 色相:%4 锐化:%5 伽马:%6")
        .arg(m_params.brightness)
        .arg(m_params.contrast, 0, 'f', 2)
        .arg(m_params.saturation, 0, 'f', 2)
        .arg(m_params.hue)
        .arg(m_params.sharpness, 0, 'f', 1)
        .arg(m_params.gamma, 0, 'f', 1));

    emit paramsChanged(m_params);
}

void PreprocessDialog::onReset() {
    m_params.reset();
    m_brightnessSlider->setValue(0);
    m_contrastSlider->setValue(100);
    m_saturationSlider->setValue(100);
    m_hueSlider->setValue(0);
    m_sharpnessSlider->setValue(0);
    m_gammaSlider->setValue(9);
    m_processedImage = m_sourceImage;
    m_previewWidget->setImage(m_sourceImage);
}

void PreprocessDialog::onApply() {
    emit applyRequested(m_processedImage);
    accept();
}

PreprocessParams PreprocessDialog::currentParams() const {
    return m_params;
}

QImage PreprocessDialog::processedImage() const {
    return m_processedImage;
}