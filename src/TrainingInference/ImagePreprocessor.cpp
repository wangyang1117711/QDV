#include "TrainingInference/ImagePreprocessor.h"
#include "TrainingInference/PreprocessDialog.h"
#include <QImage>
#include <QColor>
#include <cmath>
#include <algorithm>

// P1-C11 修复（架构评估 P3）：原实现使用 image.pixelColor(x,y) 逐像素读写，
// 每次调用涉及函数边界检查 + QRgb 转换 + QColor 构造，性能极差（5-10x 慢）。
// 优化为 scanLine() 行指针直接访问内存，消除函数调用开销与 QColor 构造。
// 对 Format_ARGB32 (Premultiplied=false) 内存布局：每像素 4 字节 BGRA（小端）。

ImagePreprocessor* ImagePreprocessor::s_instance = nullptr;

ImagePreprocessor* ImagePreprocessor::instance() {
    if (!s_instance) {
        s_instance = new ImagePreprocessor();
    }
    return s_instance;
}

static void applyBrightness(QImage& image, int brightness) {
    if (brightness == 0) return;
    const int w = image.width();
    const int h = image.height();
    for (int y = 0; y < h; ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const QRgb c = line[x];
            const int r = std::clamp(qRed(c) + brightness, 0, 255);
            const int g = std::clamp(qGreen(c) + brightness, 0, 255);
            const int b = std::clamp(qBlue(c) + brightness, 0, 255);
            line[x] = qRgba(r, g, b, qAlpha(c));
        }
    }
}

static void applyContrast(QImage& image, double contrast) {
    if (std::abs(contrast - 1.0) < 1e-6) return;
    const int w = image.width();
    const int h = image.height();
    for (int y = 0; y < h; ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const QRgb c = line[x];
            const int r = std::clamp(static_cast<int>((qRed(c) - 128) * contrast + 128), 0, 255);
            const int g = std::clamp(static_cast<int>((qGreen(c) - 128) * contrast + 128), 0, 255);
            const int b = std::clamp(static_cast<int>((qBlue(c) - 128) * contrast + 128), 0, 255);
            line[x] = qRgba(r, g, b, qAlpha(c));
        }
    }
}

static void applySaturation(QImage& image, double saturation) {
    if (std::abs(saturation - 1.0) < 1e-6) return;
    const int w = image.width();
    const int h = image.height();
    for (int y = 0; y < h; ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const QRgb c = line[x];
            const int cr = qRed(c), cg = qGreen(c), cb = qBlue(c);
            const int gray = static_cast<int>(cr * 0.299 + cg * 0.587 + cb * 0.114);
            const int r = std::clamp(static_cast<int>(gray + (cr - gray) * saturation), 0, 255);
            const int g = std::clamp(static_cast<int>(gray + (cg - gray) * saturation), 0, 255);
            const int b = std::clamp(static_cast<int>(gray + (cb - gray) * saturation), 0, 255);
            line[x] = qRgba(r, g, b, qAlpha(c));
        }
    }
}

static void applyHue(QImage& image, int hue) {
    if (hue == 0) return;
    double hueRad = hue * M_PI / 180.0;
    double cosH = cos(hueRad);
    double sinH = sin(hueRad);
    const int w = image.width();
    const int h = image.height();
    for (int y = 0; y < h; ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const QRgb c = line[x];
            double rr = qRed(c), gg = qGreen(c), bb = qBlue(c);
            int r = std::clamp(static_cast<int>(rr * (cosH + (1.0 - cosH) / 3.0 + sinH / std::sqrt(3.0)) +
                                                gg * ((1.0 - cosH) / 3.0 - sinH * (std::sqrt(3.0) / 6.0 - 0.5)) +
                                                bb * ((1.0 - cosH) / 3.0 - sinH * (std::sqrt(3.0) / 6.0 + 0.5))), 0, 255);
            int g = std::clamp(static_cast<int>(rr * ((1.0 - cosH) / 3.0 + sinH * (std::sqrt(3.0) / 6.0 + 0.5)) +
                                                gg * (cosH + (1.0 - cosH) / 3.0 + sinH / std::sqrt(3.0)) +
                                                bb * ((1.0 - cosH) / 3.0 - sinH * (std::sqrt(3.0) / 6.0 - 0.5))), 0, 255);
            int b = std::clamp(static_cast<int>(rr * ((1.0 - cosH) / 3.0 - sinH * (std::sqrt(3.0) / 6.0 - 0.5)) +
                                                gg * ((1.0 - cosH) / 3.0 + sinH * (std::sqrt(3.0) / 6.0 + 0.5)) +
                                                bb * (cosH + (1.0 - cosH) / 3.0 + sinH / std::sqrt(3.0))), 0, 255);
            line[x] = qRgba(r, g, b, qAlpha(c));
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
    const int w = image.width();
    const int h = image.height();
    for (int y = 1; y < h - 1; ++y) {
        const QRgb* prevLine = reinterpret_cast<const QRgb*>(image.scanLine(y - 1));
        const QRgb* curLine  = reinterpret_cast<const QRgb*>(image.scanLine(y));
        const QRgb* nextLine = reinterpret_cast<const QRgb*>(image.scanLine(y + 1));
        QRgb* outLine = reinterpret_cast<QRgb*>(blurred.scanLine(y));
        for (int x = 1; x < w - 1; ++x) {
            int sumR = 0, sumG = 0, sumB = 0;
            // 展开卷积核 3x3 邻域采样
            sumR += qRed(prevLine[x-1]) * kernel[0][0];  sumG += qGreen(prevLine[x-1]) * kernel[0][0];  sumB += qBlue(prevLine[x-1]) * kernel[0][0];
            sumR += qRed(prevLine[x])   * kernel[0][1];  sumG += qGreen(prevLine[x])   * kernel[0][1];  sumB += qBlue(prevLine[x])   * kernel[0][1];
            sumR += qRed(prevLine[x+1]) * kernel[0][2];  sumG += qGreen(prevLine[x+1]) * kernel[0][2];  sumB += qBlue(prevLine[x+1]) * kernel[0][2];
            sumR += qRed(curLine[x-1])  * kernel[1][0];  sumG += qGreen(curLine[x-1])  * kernel[1][0];  sumB += qBlue(curLine[x-1])  * kernel[1][0];
            sumR += qRed(curLine[x])    * kernel[1][1];  sumG += qGreen(curLine[x])    * kernel[1][1];  sumB += qBlue(curLine[x])    * kernel[1][1];
            sumR += qRed(curLine[x+1])  * kernel[1][2];  sumG += qGreen(curLine[x+1])  * kernel[1][2];  sumB += qBlue(curLine[x+1])  * kernel[1][2];
            sumR += qRed(nextLine[x-1]) * kernel[2][0];  sumG += qGreen(nextLine[x-1]) * kernel[2][0];  sumB += qBlue(nextLine[x-1]) * kernel[2][0];
            sumR += qRed(nextLine[x])   * kernel[2][1];  sumG += qGreen(nextLine[x])   * kernel[2][1];  sumB += qBlue(nextLine[x])   * kernel[2][1];
            sumR += qRed(nextLine[x+1]) * kernel[2][2];  sumG += qGreen(nextLine[x+1]) * kernel[2][2];  sumB += qBlue(nextLine[x+1]) * kernel[2][2];
            const int br = sumR / kernelSum, bg = sumG / kernelSum, bb = sumB / kernelSum;
            const QRgb oc = curLine[x];
            const int r = std::clamp(static_cast<int>(qRed(oc) + (qRed(oc) - br) * sharpness), 0, 255);
            const int g = std::clamp(static_cast<int>(qGreen(oc) + (qGreen(oc) - bg) * sharpness), 0, 255);
            const int b = std::clamp(static_cast<int>(qBlue(oc) + (qBlue(oc) - bb) * sharpness), 0, 255);
            outLine[x] = qRgba(r, g, b, qAlpha(oc));
        }
    }
    image = blurred;
}

static void applyGamma(QImage& image, double gamma) {
    if (std::abs(gamma - 1.0) < 1e-6) return;
    double invGamma = 1.0 / gamma;
    // P1-C11 优化：预构建 256 项 LUT，避免每像素 std::pow 调用（约 8x 提速）
    int lut[256];
    for (int i = 0; i < 256; ++i) {
        lut[i] = std::clamp(static_cast<int>(255.0 * std::pow(i / 255.0, invGamma)), 0, 255);
    }
    const int w = image.width();
    const int h = image.height();
    for (int y = 0; y < h; ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const QRgb c = line[x];
            line[x] = qRgba(lut[qRed(c)], lut[qGreen(c)], lut[qBlue(c)], qAlpha(c));
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