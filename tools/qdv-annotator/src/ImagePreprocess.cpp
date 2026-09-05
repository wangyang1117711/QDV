// =====================================================================
// ImagePreprocess.cpp — 预处理/边缘增强/候选建议实现
// =====================================================================
#include "ImagePreprocess.h"

#include <QColor>
#include <QVector>
#include <cmath>
#include <algorithm>
#include <utility>

namespace Prep {

QImage toGray(const QImage& src)
{
    if (src.isNull()) return src;
    if (src.format() == QImage::Format_Grayscale8) return src;
    return src.convertToFormat(QImage::Format_Grayscale8);
}

// ---- 分离盒式模糊（水平 / 垂直），半径 r ----
static QImage boxBlurH(const QImage& src, int r)
{
    int W = src.width(), H = src.height();
    QImage out(W, H, QImage::Format_Grayscale8);
    if (W == 0 || H == 0) return out;
    int norm = 2 * r + 1;
    for (int y = 0; y < H; ++y) {
        const uchar* s = src.constScanLine(y);
        uchar* d = out.scanLine(y);
        int sum = 0;
        for (int x = -r; x <= r; ++x) sum += s[qBound(0, x, W - 1)];
        for (int x = 0; x < W; ++x) {
            d[x] = static_cast<uchar>(sum / norm);
            int add = s[qBound(0, x + r + 1, W - 1)];
            int sub = s[qBound(0, x - r, W - 1)];
            sum += add - sub;
        }
    }
    return out;
}
static QImage boxBlurV(const QImage& src, int r)
{
    int W = src.width(), H = src.height();
    QImage out(W, H, QImage::Format_Grayscale8);
    if (W == 0 || H == 0) return out;
    int norm = 2 * r + 1;
    for (int x = 0; x < W; ++x) {
        int sum = 0;
        for (int y = -r; y <= r; ++y) sum += src.constScanLine(qBound(0, y, H - 1))[x];
        for (int y = 0; y < H; ++y) {
            out.scanLine(y)[x] = static_cast<uchar>(sum / norm);
            int add = src.constScanLine(qBound(0, y + r + 1, H - 1))[x];
            int sub = src.constScanLine(qBound(0, y - r, H - 1))[x];
            sum += add - sub;
        }
    }
    return out;
}
QImage gaussianBlur(const QImage& src, int radius, int iterations)
{
    QImage g = toGray(src);
    QImage cur = g;
    for (int i = 0; i < iterations; ++i) {
        cur = boxBlurH(cur, radius);
        cur = boxBlurV(cur, radius);
    }
    return cur;
}

QImage unsharp(const QImage& src, int radius, double amount)
{
    QImage g = toGray(src);
    if (g.isNull()) return g;
    QImage blurred = gaussianBlur(g, radius, 2);
    int W = g.width(), H = g.height();
    QImage out(W, H, QImage::Format_Grayscale8);
    for (int y = 0; y < H; ++y) {
        const uchar* s = g.constScanLine(y);
        const uchar* b = blurred.constScanLine(y);
        uchar* d = out.scanLine(y);
        for (int x = 0; x < W; ++x) {
            int v = s[x] + static_cast<int>(amount * (s[x] - b[x]));
            d[x] = static_cast<uchar>(qBound(0, v, 255));
        }
    }
    return out;
}

// ---- CLAHE：分块直方图 + 裁剪 + CDF 双线性插值 ----
QImage clahe(const QImage& src, int tile, double clipLimit)
{
    QImage g = toGray(src);
    if (g.isNull()) return g;
    int W = g.width(), H = g.height();
    if (tile < 2) tile = 2;
    int tx = (W + tile - 1) / tile;
    int ty = (H + tile - 1) / tile;
    // 每块 LUT[256]
    QVector<QVector<QVector<int>>> lut(ty, QVector<QVector<int>>(tx, QVector<int>(256, 0)));
    for (int tyi = 0; tyi < ty; ++tyi) {
        for (int txi = 0; txi < tx; ++txi) {
            int x0 = txi * tile, y0 = tyi * tile;
            int x1 = qMin(x0 + tile, W), y1 = qMin(y0 + tile, H);
            int cnt = (x1 - x0) * (y1 - y0);
            QVector<int> hist(256, 0);
            for (int y = y0; y < y1; ++y) {
                const uchar* row = g.constScanLine(y);
                for (int x = x0; x < x1; ++x) ++hist[row[x]];
            }
            // 裁剪：把超过 limit 的部分平均回补
            int limit = qMax(1, static_cast<int>(clipLimit * cnt / 256.0));
            int excess = 0;
            for (int i = 0; i < 256; ++i) {
                if (hist[i] > limit) { excess += hist[i] - limit; hist[i] = limit; }
            }
            int inc = excess / 256, rem = excess % 256;
            for (int i = 0; i < 256; ++i) hist[i] += inc;
            for (int i = 0; i < rem; ++i) ++hist[i];
            // CDF -> LUT
            int sum = 0;
            QVector<int>& L = lut[tyi][txi];
            for (int i = 0; i < 256; ++i) {
                sum += hist[i];
                L[i] = (cnt > 0) ? static_cast<int>(255.0 * sum / cnt) : 0;
            }
        }
    }
    QImage out(W, H, QImage::Format_Grayscale8);
    for (int y = 0; y < H; ++y) {
        const uchar* srow = g.constScanLine(y);
        uchar* drow = out.scanLine(y);
        double fy = (ty == 1) ? 0.0 : (static_cast<double>(y) / tile - 0.5);
        int ty0 = qBound(0, static_cast<int>(std::floor(fy)), ty - 1);
        int ty1 = qBound(0, ty0 + 1, ty - 1);
        double wy = qBound(0.0, fy - ty0, 1.0);
        for (int x = 0; x < W; ++x) {
            double fx = (tx == 1) ? 0.0 : (static_cast<double>(x) / tile - 0.5);
            int tx0 = qBound(0, static_cast<int>(std::floor(fx)), tx - 1);
            int tx1 = qBound(0, tx0 + 1, tx - 1);
            double wx = qBound(0.0, fx - tx0, 1.0);
            int v = srow[x];
            double top = (1.0 - wx) * lut[ty0][tx0][v] + wx * lut[ty0][tx1][v];
            double bot = (1.0 - wx) * lut[ty1][tx0][v] + wx * lut[ty1][tx1][v];
            int val = static_cast<int>((1.0 - wy) * top + wy * bot);
            drow[x] = static_cast<uchar>(qBound(0, val, 255));
        }
    }
    return out;
}

QImage sobelMagnitude(const QImage& src)
{
    QImage g = toGray(src);
    if (g.isNull()) return g;
    int W = g.width(), H = g.height();
    QImage out(W, H, QImage::Format_Grayscale8);
    out.fill(0);
    if (W < 3 || H < 3) return out;
    for (int y = 1; y < H - 1; ++y) {
        const uchar* a = g.constScanLine(y - 1);
        const uchar* b = g.constScanLine(y);
        const uchar* c = g.constScanLine(y + 1);
        uchar* d = out.scanLine(y);
        for (int x = 1; x < W - 1; ++x) {
            int gx = -a[x - 1] - 2 * b[x - 1] - c[x - 1]
                     + a[x + 1] + 2 * b[x + 1] + c[x + 1];
            int gy = -a[x - 1] - 2 * a[x] - a[x + 1]
                     + c[x - 1] + 2 * c[x] + c[x + 1];
            int mag = static_cast<int>(std::sqrt(double(gx * gx + gy * gy)));
            d[x] = static_cast<uchar>(qMin(255, mag / 4)); // 1/4 归一化近似
        }
    }
    return out;
}

QImage sobelOverlay(const QImage& src, const QColor& edge, int threshold)
{
    QImage g = toGray(src);
    int W = g.width(), H = g.height();
    QImage out(W, H, QImage::Format_ARGB32);
    out.fill(qRgba(0, 0, 0, 0));
    if (g.isNull() || W < 3 || H < 3) return out;
    for (int y = 1; y < H - 1; ++y) {
        const uchar* a = g.constScanLine(y - 1);
        const uchar* b = g.constScanLine(y);
        const uchar* c = g.constScanLine(y + 1);
        QRgb* d = reinterpret_cast<QRgb*>(out.scanLine(y));
        for (int x = 1; x < W - 1; ++x) {
            int gx = -a[x - 1] - 2 * b[x - 1] - c[x - 1]
                     + a[x + 1] + 2 * b[x + 1] + c[x + 1];
            int gy = -a[x - 1] - 2 * a[x] - a[x + 1]
                     + c[x - 1] + 2 * c[x] + c[x + 1];
            int mag = static_cast<int>(std::sqrt(double(gx * gx + gy * gy)));
            int nm = qMin(255, mag / 4);
            if (nm >= threshold)
                d[x] = qRgba(edge.red(), edge.green(), edge.blue(), nm);
        }
    }
    return out;
}

ImageStats analyze(const QImage& src)
{
    ImageStats st;
    QImage g = toGray(src);
    if (g.isNull()) return st;
    int W = g.width(), H = g.height();
    st.width = W; st.height = H;
    qlonglong sum = 0;
    int minv = 255, maxv = 0;
    const int N = W * H;
    if (N == 0) return st;
    // 第一遍：均值 + 动态范围
    for (int y = 0; y < H; ++y) {
        const uchar* s = g.constScanLine(y);
        for (int x = 0; x < W; ++x) {
            int v = s[x];
            sum += v;
            if (v < minv) minv = v;
            if (v > maxv) maxv = v;
        }
    }
    st.minv = minv; st.maxv = maxv;
    double mean = double(sum) / N;
    st.mean = mean;
    // 第二遍：标准差
    double var = 0;
    for (int y = 0; y < H; ++y) {
        const uchar* s = g.constScanLine(y);
        for (int x = 0; x < W; ++x) {
            double d = s[x] - mean;
            var += d * d;
        }
    }
    st.stddev = std::sqrt(var / N);
    // 边缘密度（Sobel 幅值均值 /255）
    double edgeSum = 0;
    if (W >= 3 && H >= 3) {
        for (int y = 1; y < H - 1; ++y) {
            const uchar* a = g.constScanLine(y - 1);
            const uchar* b = g.constScanLine(y);
            const uchar* c = g.constScanLine(y + 1);
            for (int x = 1; x < W - 1; ++x) {
                int gx = -a[x - 1] - 2 * b[x - 1] - c[x - 1]
                         + a[x + 1] + 2 * b[x + 1] + c[x + 1];
                int gy = -a[x - 1] - 2 * a[x] - a[x + 1]
                         + c[x - 1] + 2 * c[x] + c[x + 1];
                int mag = static_cast<int>(std::sqrt(double(gx * gx + gy * gy)));
                edgeSum += qMin(255, mag / 4);
            }
        }
        st.edgeDensity = (edgeSum / double((W - 2) * (H - 2))) / 255.0;
    }
    // 低对比 / 弱边缘判定（阈值依据 8bpp 工业图经验）
    st.lowContrast = (st.stddev < 28.0);   // 8bpp 下 stddev<28 视为对比度偏低
    st.weakEdge = (st.edgeDensity < 0.06); // 边缘密度 <6% 视为弱边缘
    return st;
}

// ---- 形态学闭运算（3x3），连通域候选框 ----
static QImage morphClose(const QImage& bin, int iters)
{
    // bin: Format_Grayscale8, 前景=255
    QImage cur = bin.copy();
    for (int it = 0; it < iters; ++it) {
        // 膨胀
        QImage dil = cur.copy();
        int W = cur.width(), H = cur.height();
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                bool on = false;
                for (int dy = -1; dy <= 1 && !on; ++dy)
                    for (int dx = -1; dx <= 1 && !on; ++dx) {
                        int nx = x + dx, ny = y + dy;
                        if (nx >= 0 && ny >= 0 && nx < W && ny < H && cur.constScanLine(ny)[nx])
                            on = true;
                    }
                dil.scanLine(y)[x] = on ? 255 : 0;
            }
        // 腐蚀
        QImage ero = dil.copy();
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                bool on = true;
                for (int dy = -1; dy <= 1 && on; ++dy)
                    for (int dx = -1; dx <= 1 && on; ++dx) {
                        int nx = x + dx, ny = y + dy;
                        if (nx >= 0 && ny >= 0 && nx < W && ny < H && !dil.constScanLine(ny)[nx])
                            on = false;
                    }
                ero.scanLine(y)[x] = on ? 255 : 0;
            }
        cur = ero;
    }
    return cur;
}

QVariantList suggestRegions(const QImage& src,
                            double areaMinFrac, double areaMaxFrac,
                            double aspectMin, double aspectMax)
{
    QVariantList result;
    QImage g = toGray(src);
    if (g.isNull()) return result;
    int W = g.width(), H = g.height();
    QImage edge = sobelMagnitude(g);

    // 自适应阈值：均值 + 1.0*std（仅统计有边缘响应的区域）
    qlonglong es = 0; double emean = 0;
    const int N = W * H;
    if (N == 0) return result;
    for (int y = 0; y < H; ++y) {
        const uchar* s = edge.constScanLine(y);
        for (int x = 0; x < W; ++x) es += s[x];
    }
    emean = double(es) / N;
    double evar = 0;
    for (int y = 0; y < H; ++y) {
        const uchar* s = edge.constScanLine(y);
        for (int x = 0; x < W; ++x) { double d = s[x] - emean; evar += d * d; }
    }
    double estd = std::sqrt(evar / N);
    int thr = qBound(8, static_cast<int>(emean + 1.0 * estd), 200);

    // 二值化边缘
    QImage bin(W, H, QImage::Format_Grayscale8);
    bin.fill(0);
    for (int y = 0; y < H; ++y) {
        const uchar* s = edge.constScanLine(y);
        uchar* d = bin.scanLine(y);
        for (int x = 0; x < W; ++x) if (s[x] >= thr) d[x] = 255;
    }
    // 闭运算连接弱边缘断点
    QImage closed = morphClose(bin, 2);

    // 连通域（BFS）
    QVector<QVector<bool>> vis(H, QVector<bool>(W, false));
    QVector<QPair<int,int>> stack;
    stack.reserve(N);
    const qlonglong totalArea = qlonglong(W) * H;
    const qlonglong minArea = static_cast<qlonglong>(areaMinFrac * totalArea);
    const qlonglong maxArea = static_cast<qlonglong>(areaMaxFrac * totalArea);
    for (int y0 = 0; y0 < H; ++y0) {
        for (int x0 = 0; x0 < W; ++x0) {
            if (vis[y0][x0] || !closed.constScanLine(y0)[x0]) continue;
            stack.clear();
            stack.push_back({x0, y0});
            vis[y0][x0] = true;
            int minx = x0, miny = y0, maxx = x0, maxy = y0;
            qlonglong area = 0;
            long long esum = 0;
            while (!stack.isEmpty()) {
                QPair<int,int> p = stack.takeLast();
                int x = p.first, y = p.second;
                const uchar* es_row = edge.constScanLine(y);
                esum += es_row[x];
                if (x < minx) minx = x; if (x > maxx) maxx = x;
                if (y < miny) miny = y; if (y > maxy) maxy = y;
                ++area;
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        int nx = x + dx, ny = y + dy;
                        if (nx >= 0 && ny >= 0 && nx < W && ny < H
                            && !vis[ny][nx] && closed.constScanLine(ny)[nx]) {
                            vis[ny][nx] = true;
                            stack.push_back({nx, ny});
                        }
                    }
            }
            if (area < minArea || area > maxArea) continue;
            int bw = maxx - minx + 1, bh = maxy - miny + 1;
            double aspect = (bh > 0) ? double(bw) / bh : 0;
            if (aspect < aspectMin || aspect > aspectMax) continue;
            double score = (area > 0) ? qBound(0.0, double(esum) / area / 255.0, 1.0) : 0.0;
            QVariantMap r;
            r["x"] = minx; r["y"] = miny; r["w"] = bw; r["h"] = bh;
            r["score"] = score;
            result.append(r);
        }
    }
    // 按 score 降序，最多返回 50 个
    std::sort(result.begin(), result.end(),
              [](const QVariant& a, const QVariant& b) {
                  return a.toMap()["score"].toDouble() > b.toMap()["score"].toDouble();
              });
    if (result.size() > 50) result.erase(result.begin() + 50, result.end());
    return result;
}

} // namespace Prep
