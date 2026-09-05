// =====================================================================
// Exporters.h — 标注结果导出的辅助函数（供 AnnotationSession 成员调用）
//
// 导出格式与 QDV 现有 AI 算子的对接关系（见 README）：
//   yolo_detect   -> YoloDetect / DetectObjectsDl / ZeroShotDetect
//   yolo_seg      -> SegmentDl
//   classification-> AiClassify
//   ocr           -> DLOCR / Ocr
//   coco          -> 与 Label Studio / CVAT 互通
// =====================================================================
#pragma once

#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

// 前置声明，避免引入完整头（导出主逻辑在 AnnotationSession 成员内）
class AnnotationSession;

namespace Qdv {

// 归一化矩形 -> YOLO (cx,cy,w,h) 绝对比例，裁剪到 [0,1]
void normRect(int x, int y, int w, int h, int W, int H,
              double& cx, double& cy, double& nw, double& nh);

// 多边形面积（shoelace）
double polyArea(const QVariantList& pts);

// 计算每个图像的 train/val 子集（自动或按 flag）
QList<QPair<QString, QString>> planSubsets(const AnnotationSession* s,
                                           const QVariantMap& options);

// 将源图复制到 destDir（或仅返回相对名），返回文件名
QString copyImage(const QString& srcPath, const QString& destDir, bool copy);

// 解析图片有效路径：原路径存在则原样返回；否则在父目录与其直接子目录中
// 按文件名查找（应对"图片被移入日期子目录"等导致 qdvann 路径失效的情况），
// 仍找不到则返回原路径。
QString resolveImagePath(const QString& storedPath, const QString& fileName);

// 写 QDV 模型注册格式 {"labels":[...]}
void writeLabelsJson(const QString& dir, const QStringList& labels);

// 写算子 categoryLabels 参数直接可用的纯数组 ["a","b"]
void writeCategoryLabelsJson(const QString& dir, const QStringList& labels);

// 写 Ultralytics data.yaml
void writeDataYaml(const QString& path, const QString& absRoot,
                   const QStringList& labels, bool hasMasks);

} // namespace Qdv
