// =====================================================================
// ImagePreprocess.h — 低对比度工业图辅助标注的图像预处理（纯 Qt/C++，无 OpenCV）
//
// 设计：针对本项目 1200×1200 8bpp 灰度工业金属件（低对比度、弱边缘、
// 表面缺陷检测场景）提供：
//   - CLAHE       限制对比度自适应直方图均衡（局部对比度拉伸，不放大噪声）
//   - unsharp     非锐化掩膜（高通，强调弱边缘）
//   - sobelEdge   边缘幅值图 / 彩色边缘叠加层（辅助看清弱边界）
//   - analyze     量化统计（均值/标准差/边缘密度）—— 视觉算法专家的诊断依据
//   - suggestRegions  连通域候选框（基于增强+边缘二值化），供"弱边缘增强辅助标注"
// 所有函数返回与输入同尺寸的 QImage，保证标注坐标对齐不受影响。
// =====================================================================
#pragma once

#include <QImage>
#include <QVariantMap>
#include <QVariantList>

namespace Prep {

// 转为 8bpp 灰度（已是灰度则直接返回）
QImage toGray(const QImage& src);

// CLAHE：tile=分块边长(px)，clipLimit=裁剪系数(直方图被裁剪到 clipLimit*块像素/256)
// 块间 CDF 做双线性插值，避免块边界伪影。
QImage clahe(const QImage& src, int tile = 150, double clipLimit = 2.0);

// 盒式模糊近似高斯（iterations 次分离盒模糊，半径越大越平滑）
QImage gaussianBlur(const QImage& src, int radius = 2, int iterations = 2);

// 非锐化掩膜：sharpened = src + amount*(src - blur)，强调高频/弱边缘
QImage unsharp(const QImage& src, int radius = 2, double amount = 1.2);

// Sobel 边缘幅值图（8bpp，已按 1/4 归一化到 0~255）
QImage sobelMagnitude(const QImage& src);

// 彩色边缘叠加层（ARGB32，透明底 + 指定色 edge 的 alpha=幅值），用于画布叠加显示
QImage sobelOverlay(const QImage& src, const QColor& edge = QColor(0, 229, 255),
                    int threshold = 24);

// 量化统计：供"自动模式"与视觉算法专家诊断
struct ImageStats {
    int width = 0, height = 0;
    double mean = 0;        // 灰度均值
    double stddev = 0;      // 标准差（对比度代理）
    int minv = 0, maxv = 0; // 动态范围
    double edgeDensity = 0; // 平均 Sobel 幅值 /255（弱边缘代理，越大边缘越明显）
    bool lowContrast = false; // stddev 低于阈值
    bool weakEdge = false;    // edgeDensity 低于阈值
};
ImageStats analyze(const QImage& src);

// 候选区域建议（弱边缘增强辅助标注）：
// 对增强图求 Sobel，自适应阈值二值化 → 形态学闭运算 → 连通域 → 过滤后返回候选框。
// 返回 QVariantList，每项 {x,y,w,h,score(0~1)}，score 越高越可能是目标。
QVariantList suggestRegions(const QImage& src,
                            double areaMinFrac = 0.0008,
                            double areaMaxFrac = 0.45,
                            double aspectMin = 0.15,
                            double aspectMax = 8.0);

} // namespace Prep
