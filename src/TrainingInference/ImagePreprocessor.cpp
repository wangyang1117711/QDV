#include "TrainingInference/ImagePreprocessor.h"
#include "TrainingInference/PreprocessDialog.h"
#include <QImage>
#include <QColor>
#include <cmath>
#include <algorithm>

ImagePreprocessor* ImagePreprocessor::s_instance = nullptr;

ImagePreprocessor* ImagePreprocessor::instance() {
    if (!s_instance) {
        s_instance = new ImagePreprocessor();
    }
    return s_instance;
}

static void applyBrightness(QImage& image, int brightness) {
    if (brightness == 0) return;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            QColor c = image.pixelColor(x, y);
            int r = std::clamp(c.red() + brightness, 0, 255);
            int g = std::clamp(c.green() + brightness, 0, 255);
            int b = std::clamp(c.blue() + brightness, 0, 255);
            image.setPixelColor(x, y, QColor(r, g, b, c.alpha()));
        }
    }
}

static void applyContrast(QImage& image, double contrast) {
    if (std::abs(contrast - 1.0) < 1e-6) return;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            QColor c = image.pixelColor(x, y);
            int r = std::clamp(static_cast<int>((c.red() - 128) * contrast + 128), 0, 255);
            int g = std::clamp(static_cast<int>((c.green() - 128) * contrast + 128), 0, 255);
            int b = std::clamp(static_cast<int>((c.blue() - 128) * contrast + 128), 0, 255);
            image.setPixelColor(x, y, QColor(r, g, b, c.alpha()));
        }
    }
}

static void applySaturation(QImage& image, double saturation) {
    if (std::abs(saturation - 1.0) < 1e-6) return;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            QColor c = image.pixelColor(x, y);
            int gray = static_cast<int>(c.red() * 0.299 + c.green() * 0.587 + c.blue() * 0.114);
            int r = std::clamp(static_cast<int>(gray + (c.red() - gray) * saturation), 0, 255);
            int g = std::clamp(static_cast<int>(gray + (c.green() - gray) * saturation), 0, 255);
            int b = std::clamp(static_cast<int>(gray + (c.blue() - gray) * saturation), 0, 255);
            image.setPixelColor(x, y, QColor(r, g, b, c.alpha()));
        }
    }
}

static void applyHue(QImage& image, int hue) {
    if (hue == 0) return;
    double hueRad = hue * M_PI / 180.0;
    double cosH = cos(hueRad);
    double sinH = sin(hueRad);
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            QColor c = image.pixelColor(x, y);
            int r = std::clamp(static_cast<int>(
                c.red() * (0.787 + 0.213 * cosH) + c.green() * (0.213 - 0.213 * cosH + 0.143 * sinH + 0.787 * sinH) + c.blue() * (0.213 - 0.213 * cosH - 0.715 * sinH) -
                                  c.green() * 0.143 * sinH + c.blue() * 0.715 * sinH), 0, 255);
            double rr = c.red(), gg = c.green(), bb = c.blue();
            r = std::clamp(static_cast<int>(rr * (cosH + (1.0 - cosH) / 3.0 + sinH / std::sqrt(3.0)) +
                                            gg * ((1.0 - cosH) / 3.0 - sinH * (std::sqrt(3.0) / 6.0 - 0.5)) +
                                            bb * ((1.0 - cosH) / 3.0 - sinH * (std::sqrt(3.0) / 6.0 + 0.5))), 0, 255);
            int g = std::clamp(static_cast<int>(rr * ((1.0 - cosH) / 3.0 + sinH * (std::sqrt(3.0) / 6.0 + 0.5)) +
                                                gg * (cosH + (1.0 - cosH) / 3.0 + sinH / std::sqrt(3.0)) +
                                                bb * ((1.0 - cosH) / 3.0 - sinH * (std::sqrt(3.0) / 6.0 - 0.5))), 0, 255);
            int b = std::clamp(static_cast<int>(rr * ((1.0 - cosH) / 3.0 - sinH * (std::sqrt(3.0) / 6.0 - 0.5)) +
                                                gg * ((1.0 - cosH) / 3.0 + sinH * (std::sqrt(3.0) / 6.0 + 0.5)) +
                                                bb * (cosH + (1.0 - cosH) / 3.0 + sinH / std::sqrt(3.0))), 0, 255);
            image.setPixelColor(x, y, QColor(r, g, b, c.alpha()));
        }
    }
}

static void applySharpness(QImage& image, double sharpness) {
    if (sharpness <= 0.0) return;
    QImage blurred = image;
    int kernel[3][3] = {
        {1, 2, 1},
        {2, 4, 2},
        {1, 2, 1}
    };
    int kernelSum = 16;
    for (int y = 1; y < image.height() - 1; ++y) {
        for (int x = 1; x < image.width() - 1; ++x) {
            int sumR = 0, sumG = 0, sumB = 0;
            for (int ky = -1; ky <= 1; ++ky) {
                for (int kx = -1; kx <= 1; ++kx) {
                    QColor nc = image.pixelColor(x + kx, y + ky);
                    int w = kernel[ky + 1][kx + 1];
                    sumR += nc.red() * w;
                    sumG += nc.green() * w;
                    sumB += nc.blue() * w;
                }
            }
            QColor bc(std::clamp(sumR / kernelSum, 0, 255),
                      std::clamp(sumG / kernelSum, 0, 255),
                      std::clamp(sumB / kernelSum, 0, 255));
            QColor oc = image.pixelColor(x, y);
            int r = std::clamp(static_cast<int>(oc.red() + (oc.red() - bc.red()) * sharpness), 0, 255);
            int g = std::clamp(static_cast<int>(oc.green() + (oc.green() - bc.green()) * sharpness), 0, 255);
            int b = std::clamp(static_cast<int>(oc.blue() + (oc.blue() - bc.blue()) * sharpness), 0, 255);
            blurred.setPixelColor(x, y, QColor(r, g, b, oc.alpha()));
        }
    }
    image = blurred;
}

static void applyGamma(QImage& image, double gamma) {
    if (std::abs(gamma - 1.0) < 1e-6) return;
    double invGamma = 1.0 / gamma;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            QColor c = image.pixelColor(x, y);
            int r = std::clamp(static_cast<int>(255.0 * std::pow(c.red() / 255.0, invGamma)), 0, 255);
            int g = std::clamp(static_cast<int>(255.0 * std::pow(c.green() / 255.0, invGamma)), 0, 255);
            int b = std::clamp(static_cast<int>(255.0 * std::pow(c.blue() / 255.0, invGamma)), 0, 255);
            image.setPixelColor(x, y, QColor(r, g, b, c.alpha()));
        }
    }
}

QImage ImagePreprocessor::process(const QImage& source, const PreprocessParams& params) const {
    QImage result = source.convertToFormat(QImage::Format_ARGB32);
    applyBrightness(result, params.brightness);
    applyContrast(result, params.contrast);
    applySaturation(result, params.saturation);
    applyHue(result, params.hue);
    applySharpness(result, params.sharpness);
    applyGamma(result, params.gamma);
    return result;
}