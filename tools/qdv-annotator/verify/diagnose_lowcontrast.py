#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
诊断脚本 — 低对比度工业图辅助标注（视觉算法专家 SOP 第一步：量化诊断）

背景：
  QDV 标注工具面向 1200×1200 8bpp 灰度工业金属件（低对比度、弱边缘、
  表面缺陷检测场景）。本脚本对 tools 上层「测试图/」目录的 7 张 BMP 做
  量化诊断，复刻 C++ Prep::analyze() 的统计量（均值 / 标准差 / 动态范围 /
  Sobel 边缘密度），独立验证 C++ 侧的默认阈值与 CLAHE / unsharp 参数是否合理。

约束：沙箱 Python 无 numpy / OpenCV，故用纯标准库解析 BMP 并计算。
算法与 ImagePreprocess.cpp::analyze() 严格对齐，便于交叉核对。

用法：
  python diagnose_lowcontrast.py [测试图目录] [输出JSON]

阈值（与 C++ 一致，8bpp）：
  lowContrast : stddev < 28.0
  weakEdge    : edgeDensity < 0.06   (edgeDensity = mean(Sobel/4)/255)

输出：
  1) 控制台表格（每张图一行 + 汇总）
  2) 推荐预处理模式（auto 决策依据）
  3) 可选 JSON（供后续文档/CI 引用）
"""
import os
import sys
import json
import struct
import math

# ----------------------------- BMP 解析（纯标准库）-----------------------------
def read_bmp_gray(path):
    """返回 (W, H, pixels: list[list[int]])，像素为 0~255 灰度（已用调色板 R 通道）。"""
    with open(path, "rb") as f:
        data = f.read()
    if len(data) < 54:
        raise ValueError("文件过小，不是合法 BMP")
    # BITMAPFILEHEADER
    bf_type = data[0:2]
    if bf_type != b"BM":
        raise ValueError("不是 BMP 文件 (magic=%r)" % bf_type)
    bf_offbits = struct.unpack_from("<I", data, 10)[0]
    # BITMAPINFOHEADER
    bi_size = struct.unpack_from("<I", data, 14)[0]
    bi_width = struct.unpack_from("<i", data, 18)[0]
    bi_height = struct.unpack_from("<i", data, 22)[0]
    bi_bpp = struct.unpack_from("<H", data, 28)[0]
    bi_compression = struct.unpack_from("<I", data, 30)[0]
    if bi_compression != 0:
        raise ValueError("仅支持未压缩 BMP (compression=%d)" % bi_compression)
    if bi_bpp != 8:
        raise ValueError("仅支持 8bpp 灰度 BMP (bpp=%d)" % bi_bpp)

    # 调色板：从 offset 14+bi_size 起，256 项 × 4 字节 (B,G,R,Reserved)
    pal_off = 14 + bi_size
    palette = []
    for i in range(256):
        o = pal_off + i * 4
        b, g, r, _ = data[o], data[o + 1], data[o + 2], data[o + 3]
        # 工业 8bpp 多为灰度调色板，取 R 作为灰度值（与 C++ 处理一致）
        palette.append(r)

    # 像素：bottom-up，每行字节数 4 字节对齐
    row_bytes = (bi_width + 3) // 4 * 4
    H = abs(bi_height)
    W = bi_width
    pixels = [[0] * W for _ in range(H)]
    for y in range(H):
        # bottom-up：文件中第 0 行是图像最底行
        file_row = (H - 1 - y) if bi_height > 0 else y
        base = bf_offbits + file_row * row_bytes
        for x in range(W):
            idx = data[base + x]
            pixels[y][x] = palette[idx]
    return W, H, pixels


# ----------------------------- 统计（对齐 Prep::analyze）-----------------------------
def sobel_mag(a, b, c):
    """a/b/c 为相邻三行同列邻居的灰度，返回 Sobel 幅值（与 C++ 一致，mag/4 截断到 255）。"""
    gx = -a[0] - 2 * b[0] - c[0] + a[2] + 2 * b[2] + c[2]
    gy = -a[0] - 2 * a[1] - a[2] + c[0] + 2 * c[1] + c[2]
    mag = int(math.sqrt(gx * gx + gy * gy))
    return min(255, mag // 4)


def analyze(pixels, W, H):
    n = W * H
    s = 0
    mn, mx = 255, 0
    for y in range(H):
        row = pixels[y]
        for x in range(W):
            v = row[x]
            s += v
            if v < mn:
                mn = v
            if v > mx:
                mx = v
    mean = s / n
    var = 0.0
    for y in range(H):
        row = pixels[y]
        for x in range(W):
            d = row[x] - mean
            var += d * d
    stddev = math.sqrt(var / n)

    # 边缘密度：只在 1..H-2 × 1..W-2 计算（与 C++ 一致）
    edge_sum = 0
    inner = (W - 2) * (H - 2)
    if inner <= 0:
        edge_density = 0.0
    else:
        for y in range(1, H - 1):
            a = pixels[y - 1]
            b = pixels[y]
            c = pixels[y + 1]
            for x in range(1, W - 1):
                m = sobel_mag((a[x - 1], a[x], a[x + 1]),
                              (b[x - 1], b[x], b[x + 1]),
                              (c[x - 1], c[x], c[x + 1]))
                edge_sum += m
        edge_density = (edge_sum / inner) / 255.0

    low_contrast = stddev < 28.0
    weak_edge = edge_density < 0.06
    return {
        "width": W, "height": H,
        "mean": mean, "stddev": stddev,
        "min": mn, "max": mx,
        "edgeDensity": edge_density,
        "lowContrast": low_contrast,
        "weakEdge": weak_edge,
    }


def recommend(st):
    """依据量化结果给出 auto 模式下的预处理建议（与 AnnotationSession 的 auto 决策一致）。"""
    if st["lowContrast"] or st["weakEdge"]:
        return "clahe (tile=150, clipLimit=2.0)"
    return "unsharp (radius=2, amount=1.2)"


def main():
    default_dir = os.path.join(os.path.dirname(__file__), "..", "..", "..", "测试图")
    default_dir = os.path.abspath(default_dir)
    img_dir = sys.argv[1] if len(sys.argv) > 1 else default_dir
    out_json = sys.argv[2] if len(sys.argv) > 2 else None

    if not os.path.isdir(img_dir):
        print("目录不存在: %s" % img_dir)
        sys.exit(1)

    files = sorted(f for f in os.listdir(img_dir) if f.lower().endswith(".bmp"))
    if not files:
        print("未找到 BMP: %s" % img_dir)
        sys.exit(1)

    print("=" * 92)
    print("低对比度工业图量化诊断  —  视觉算法专家 SOP(Step1: 量化先行)")
    print("目录: %s" % img_dir)
    print("=" * 92)
    hdr = ("%-34s %5s %7s %7s %5s %5s %9s %6s %6s  %-26s"
           % ("文件名", "W×H", "均值", "标准差", "min", "max", "边缘密度", "低对比", "弱边缘", "auto 推荐"))
    print(hdr)
    print("-" * 92)

    rows = []
    n_low = n_weak = 0
    for f in files:
        path = os.path.join(img_dir, f)
        try:
            W, H, px = read_bmp_gray(path)
        except Exception as e:
            print("%-34s 解析失败: %s" % (f, e))
            continue
        st = analyze(px, W, H)
        if st["lowContrast"]:
            n_low += 1
        if st["weakEdge"]:
            n_weak += 1
        rec = recommend(st)
        row = (f, "%dx%d" % (W, H), "%.1f" % st["mean"], "%.1f" % st["stddev"],
               st["min"], st["max"], "%.4f" % st["edgeDensity"],
               "Y" if st["lowContrast"] else "-",
               "Y" if st["weakEdge"] else "-", rec)
        print("%-34s %5s %7s %7s %5s %5s %9s %6s %6s  %-26s" % row)
        rows.append({"file": f, "stats": st, "recommend": rec})

    print("-" * 92)
    print("汇总: 共 %d 张 | 低对比度 %d 张 | 弱边缘 %d 张"
          % (len(rows), n_low, n_weak))

    # 阈值合理性评估
    stddevs = [r["stats"]["stddev"] for r in rows]
    edges = [r["stats"]["edgeDensity"] for r in rows]
    print("标准差范围: %.1f ~ %.1f（阈值 28.0）" % (min(stddevs), max(stddevs)))
    print("边缘密度范围: %.4f ~ %.4f（阈值 0.06）" % (min(edges), max(edges)))
    if min(stddevs) >= 28.0:
        print("[提示] 当前样本不存在 stddev<28 的低对比图，'auto→clahe' 分支可能未被触发，"
              "但阈值仍合理（保留对更弱图的覆盖）。")
    if min(edges) >= 0.06:
        print("[提示] 当前样本边缘密度均≥0.06，弱边缘判定未触发；如后续出现更平滑工件可下调阈值。")
    print("=" * 92)

    if out_json:
        with open(out_json, "w", encoding="utf-8") as fp:
            json.dump({
                "dir": img_dir,
                "threshold": {"stddev": 28.0, "edgeDensity": 0.06},
                "summary": {"total": len(rows), "lowContrast": n_low, "weakEdge": n_weak},
                "images": rows,
            }, fp, ensure_ascii=False, indent=2)
        print("已写出 JSON: %s" % out_json)


if __name__ == "__main__":
    main()
