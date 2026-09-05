// =====================================================================
// Exporters.cpp — 导出辅助函数实现
// =====================================================================
#include "Exporters.h"
#include "AnnotationSession.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QImage>
#include <QPainter>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSaveFile>
#include <algorithm>
#include <cmath>
#include <cstdint>

// 字符串读取：缺失返回默认值
static QString strOr(const QVariantMap& m, const QString& k, const QString& def = QString()) {
    return m.contains(k) ? m.value(k).toString() : def;
}

namespace Qdv {

void normRect(int x, int y, int w, int h, int W, int H,
              double& cx, double& cy, double& nw, double& nh)
{
    double dx = x + w / 2.0, dy = y + h / 2.0;
    cx = std::clamp(dx / std::max(W, 1), 0.0, 1.0);
    cy = std::clamp(dy / std::max(H, 1), 0.0, 1.0);
    nw = std::clamp(double(w) / std::max(W, 1), 0.0, 1.0);
    nh = std::clamp(double(h) / std::max(H, 1), 0.0, 1.0);
}

double polyArea(const QVariantList& pts)
{
    double a = 0;
    int n = pts.size();
    for (int i = 0; i < n; ++i) {
        const QVariantMap p1 = pts[i].toMap();
        const QVariantMap p2 = pts[(i + 1) % n].toMap();
        a += p1.value("x").toDouble() * p2.value("y").toDouble()
           - p2.value("x").toDouble() * p1.value("y").toDouble();
    }
    return std::fabs(a) / 2.0;
}

QList<QPair<QString, QString>> planSubsets(const AnnotationSession* s,
                                           const QVariantMap& options)
{
    const double valRatio = options.value("valRatio", 0.2).toDouble();
    const uint32_t seed = static_cast<uint32_t>(options.value("seed", 12345).toUInt());
    const QString mode = options.value("subsetMode", "auto").toString();

    const QVariantList imgs = s->images();
    QList<QPair<QString, QString>> plan;
    plan.reserve(imgs.size());
    int idx = 0;
    for (const QVariant& v : imgs) {
        const QVariantMap img = v.toMap();
        QString sub = strOr(img, "subset", "unassigned");
        if (mode == "flag" && (sub == "train" || sub == "val")) {
            plan.push_back({sub, sub});
        } else {
            uint32_t h = seed ^ 0x9E3779B9u;
            for (QChar c : img.value("fileName").toString()) {
                h ^= static_cast<uint32_t>(c.unicode());
                h *= 16777619u;
            }
            double r = (h % 100000) / 100000.0;
            QString assigned = (r < valRatio) ? "val" : "train";
            plan.push_back({sub, assigned});
        }
        ++idx;
    }
    return plan;
}

QString copyImage(const QString& srcPath, const QString& destDir, bool copy)
{
    QDir().mkpath(destDir);
    QFileInfo fi(srcPath);
    QString dest = destDir + "/" + fi.fileName();
    if (copy) {
        if (QFile::exists(dest)) QFile::remove(dest);
        QFile::copy(srcPath, dest);
    }
    return fi.fileName();
}

QString resolveImagePath(const QString& storedPath, const QString& fileName)
{
    // 1) 原路径仍有效 -> 原样返回（正常情况零开销）
    if (QFile::exists(storedPath)) return storedPath;

    QFileInfo fi(storedPath);
    QString name = fileName.isEmpty() ? fi.fileName() : fileName;
    if (name.isEmpty()) return storedPath;

    // 2) 失效：到父目录自身查找
    QDir parent(fi.absolutePath());
    if (parent.exists(name)) return parent.filePath(name);

    // 3) 失效：到父目录的直接子目录中按文件名查找
    //    （应对图片被搬入日期子目录等情况，如 处理图\20250404\xxx.bmp）
    const QFileInfoList subs = parent.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo& sub : subs) {
        const QString cand = sub.absoluteFilePath() + "/" + name;
        if (QFile::exists(cand)) return cand;
    }

    // 仍找不到 -> 保留原路径（由上层走加载失败提示）
    return storedPath;
}

void writeLabelsJson(const QString& dir, const QStringList& labels)
{
    QDir().mkpath(dir);
    QJsonObject o;
    QJsonArray arr;
    for (const QString& l : labels) arr.append(l);
    o["labels"] = arr;
    QSaveFile f(dir + "/labels.json");
    { bool _ok = f.open(QIODevice::WriteOnly); Q_UNUSED(_ok); }
    f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
    f.commit();
}

void writeCategoryLabelsJson(const QString& dir, const QStringList& labels)
{
    QDir().mkpath(dir);
    QJsonArray arr;
    for (const QString& l : labels) arr.append(l);
    QSaveFile f(dir + "/category_labels.json");
    { bool _ok = f.open(QIODevice::WriteOnly); Q_UNUSED(_ok); }
    f.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
    f.commit();
}

void writeDataYaml(const QString& path, const QString& absRoot,
                   const QStringList& labels, bool hasMasks)
{
    QFile f(path);
    { bool _ok = f.open(QIODevice::WriteOnly); Q_UNUSED(_ok); }
    QTextStream ts(&f);
    ts.setEncoding(QStringConverter::Utf8);
    ts << "path: " << absRoot << "\n";
    ts << "train: images/train\n";
    ts << "val: images/val\n";
    if (hasMasks) ts << "masks: masks/train\n";
    ts << "\n";
    ts << "nc: " << labels.size() << "\n";
    ts << "names: [";
    for (int i = 0; i < labels.size(); ++i)
        ts << (i ? ", " : "") << labels[i];
    ts << "]\n";
    f.close();
}

} // namespace Qdv
