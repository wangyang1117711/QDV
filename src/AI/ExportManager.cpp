#include "AI/ExportManager.h"
#include "Core/Logger.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QTextStream>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDateTime>
#include <QSet>
#include <opencv2/opencv.hpp>

namespace QDV {

// 单例静态成员初始化
ExportManager* ExportManager::s_instance = nullptr;

ExportManager* ExportManager::instance() {
    if (!s_instance) {
        s_instance = new ExportManager();
    }
    return s_instance;
}

ExportManager::ExportManager(QObject* parent)
    : QObject(parent)
{
}

ExportManager::~ExportManager()
{
}

// ---------------------------------------------------------------------------
// 85:15 等距轮转采样
// ---------------------------------------------------------------------------
QPair<QList<int>, QList<int>> ExportManager::splitTrainVal(int totalCount, double valRatio) const {
    QList<int> trainIndices;
    QList<int> valIndices;

    // <10 张全进 train（避免极小验证集无统计意义）
    if (totalCount < 10) {
        for (int i = 0; i < totalCount; ++i) {
            trainIndices.append(i);
        }
        return qMakePair(trainIndices, valIndices);
    }

    // step = max(1, int(1.0/valRatio))，每 step 张取 1 张进 val
    int step = qMax(1, static_cast<int>(1.0 / valRatio));
    for (int i = 0; i < totalCount; ++i) {
        if (i % step == 0) {
            valIndices.append(i);
        } else {
            trainIndices.append(i);
        }
    }
    return qMakePair(trainIndices, valIndices);
}

// ---------------------------------------------------------------------------
// 读取图像尺寸（QFile + cv::imdecode 兼容中文路径）
// ---------------------------------------------------------------------------
QSize ExportManager::readImageSize(const QString& path) const {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QDV::Logger::warn(QString("ExportManager: 无法打开图像文件: %1").arg(path));
        return QSize();
    }
    QByteArray data = file.readAll();
    file.close();

    // 内存解码（绕过 OpenCV imread 中文路径 bug）
    cv::Mat image = cv::imdecode(cv::Mat(1, data.size(), CV_8UC1, data.data()), cv::IMREAD_UNCHANGED);
    if (image.empty()) {
        QDV::Logger::warn(QString("ExportManager: 图像解码失败: %1").arg(path));
        return QSize();
    }
    // cv::Mat: rows=height, cols=width
    return QSize(image.cols, image.rows);
}

// ---------------------------------------------------------------------------
// 复制图像到目标目录
// ---------------------------------------------------------------------------
bool ExportManager::copyImage(const QString& src, const QString& dst) const {
    // 确保目标目录存在
    QFileInfo dstInfo(dst);
    QDir().mkpath(dstInfo.absolutePath());

    if (!QFile::copy(src, dst)) {
        QDV::Logger::warn(QString("ExportManager: 复制图像失败: %1 -> %2").arg(src).arg(dst));
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// YOLO 标签文件生成
// ---------------------------------------------------------------------------
bool ExportManager::writeYoloLabel(const QString& labelPath,
                                    const QList<Annotation>& annotations,
                                    const QSize& imageSize) const {
    // 确保目标目录存在
    QFileInfo info(labelPath);
    QDir().mkpath(info.absolutePath());

    QFile file(labelPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QDV::Logger::warn(QString("ExportManager: 无法写入标签文件: %1").arg(labelPath));
        return false;
    }

    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);

    const int imgW = imageSize.width();
    const int imgH = imageSize.height();

    for (const Annotation& ann : annotations) {
        if (ann.type == AnnotationType::Polygon) {
            // polygon 格式: class_id x1 y1 x2 y2 ... (归一化)
            QStringList coords;
            for (const QPointF& pt : ann.polygon) {
                double x = (imgW > 0) ? (pt.x() / imgW) : 0.0;
                double y = (imgH > 0) ? (pt.y() / imgH) : 0.0;
                // 裁剪到 [0, 1]
                x = qBound(0.0, x, 1.0);
                y = qBound(0.0, y, 1.0);
                coords << QString::number(x, 'f', 6)
                       << QString::number(y, 'f', 6);
            }
            if (!coords.isEmpty()) {
                out << ann.classId << " " << coords.join(" ") << "\n";
            }
        } else {
            // bbox 格式: [x, y, w, h] 绝对像素 → YOLO 中心点归一化
            if (ann.bbox.size() != 4) {
                continue;
            }
            double x = ann.bbox[0];
            double y = ann.bbox[1];
            double w = ann.bbox[2];
            double h = ann.bbox[3];

            // 转换为 YOLO 中心点格式并归一化
            double cx = (imgW > 0) ? ((x + w / 2.0) / imgW) : 0.0;
            double cy = (imgH > 0) ? ((y + h / 2.0) / imgH) : 0.0;
            double nw = (imgW > 0) ? (w / imgW) : 0.0;
            double nh = (imgH > 0) ? (h / imgH) : 0.0;

            // 裁剪到 [0, 1]
            cx = qBound(0.0, cx, 1.0);
            cy = qBound(0.0, cy, 1.0);
            nw = qBound(0.0, nw, 1.0);
            nh = qBound(0.0, nh, 1.0);

            out << ann.classId << " "
                << QString::number(cx, 'f', 6) << " "
                << QString::number(cy, 'f', 6) << " "
                << QString::number(nw, 'f', 6) << " "
                << QString::number(nh, 'f', 6) << "\n";
        }
    }

    // 空标注 → 0 字节文件（背景样本）：不写入任何内容
    file.close();
    return true;
}

// ---------------------------------------------------------------------------
// data.yaml 生成
// ---------------------------------------------------------------------------
bool ExportManager::writeDataYaml(const QString& path, const QString& rootDir,
                                   const QString& trainRelPath, const QString& valRelPath,
                                   int numClasses, const QStringList& classNames) const {
    QFileInfo info(path);
    QDir().mkpath(info.absolutePath());

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QDV::Logger::warn(QString("ExportManager: 无法写入 data.yaml: %1").arg(path));
        return false;
    }

    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);

    out << "path: " << rootDir << "\n";
    out << "train: " << trainRelPath << "\n";
    out << "val: " << valRelPath << "\n";
    out << "nc: " << numClasses << "\n";
    out << "names:\n";
    for (int i = 0; i < classNames.size(); ++i) {
        out << "  " << i << ": " << classNames[i] << "\n";
    }

    file.close();
    return true;
}

// ---------------------------------------------------------------------------
// COCO 标注 JSON 生成
// ---------------------------------------------------------------------------
QJsonObject ExportManager::buildCocoAnnotation(const QList<ExportImageEntry>& entries,
                                                const QStringList& classNames,
                                                const QList<int>& trainIndices,
                                                const QList<int>& valIndices) const {
    QJsonObject root;

    // info 段
    QJsonObject info;
    info["description"] = QStringLiteral("QDV Export");
    info["version"] = QStringLiteral("1.0");
    info["year"] = 2026;
    root["info"] = info;

    // licenses 段
    QJsonArray licenses;
    QJsonObject license;
    license["id"] = 1;
    license["name"] = QStringLiteral("QDV");
    licenses.append(license);
    root["licenses"] = licenses;

    // categories 段
    QJsonArray categories;
    for (int i = 0; i < classNames.size(); ++i) {
        QJsonObject cat;
        cat["id"] = i;
        cat["name"] = classNames[i];
        categories.append(cat);
    }
    root["categories"] = categories;

    // images + annotations 段
    QJsonArray imagesArray;
    QJsonArray annotationsArray;
    int annotationId = 1;  // COCO 标注 id 从 1 开始

    // 合并 train + val 索引（COCO 不强制分目录，但记录 split 信息）
    QList<int> allIndices = trainIndices + valIndices;

    QSet<int> trainSet;
    for (int idx : trainIndices) {
        trainSet.insert(idx);
    }

    int imageId = 1;  // COCO 图像 id 从 1 开始
    for (int idx : allIndices) {
        if (idx < 0 || idx >= entries.size()) {
            continue;
        }

        const ExportImageEntry& entry = entries[idx];
        QSize imgSize = readImageSize(entry.path);
        if (imgSize.isEmpty()) {
            QDV::Logger::warn(QString("ExportManager: 跳过无法读取尺寸的图像: %1").arg(entry.path));
            continue;
        }

        QFileInfo imgInfo(entry.path);
        QString fileName = imgInfo.fileName();

        // images 条目
        QJsonObject imgObj;
        imgObj["id"] = imageId;
        imgObj["file_name"] = fileName;
        imgObj["width"] = imgSize.width();
        imgObj["height"] = imgSize.height();
        // 附加 split 字段（非 COCO 标准，但便于后续区分）
        imgObj["split"] = trainSet.contains(idx) ? QStringLiteral("train") : QStringLiteral("val");
        imagesArray.append(imgObj);

        // annotations 条目
        for (const Annotation& ann : entry.annotations) {
            QJsonObject annObj;
            annObj["id"] = annotationId++;
            annObj["image_id"] = imageId;
            annObj["category_id"] = ann.classId;

            if (ann.type == AnnotationType::Polygon) {
                // polygon: segmentation 字段
                QJsonArray seg;
                for (const QPointF& pt : ann.polygon) {
                    seg.append(pt.x());
                    seg.append(pt.y());
                }
                annObj["segmentation"] = seg;

                // polygon 的 bbox 取外接矩形
                if (!ann.polygon.isEmpty()) {
                    double minX = ann.polygon[0].x();
                    double minY = ann.polygon[0].y();
                    double maxX = ann.polygon[0].x();
                    double maxY = ann.polygon[0].y();
                    for (const QPointF& pt : ann.polygon) {
                        minX = qMin(minX, pt.x());
                        minY = qMin(minY, pt.y());
                        maxX = qMax(maxX, pt.x());
                        maxY = qMax(maxY, pt.y());
                    }
                    double w = maxX - minX;
                    double h = maxY - minY;
                    QJsonArray bbox;
                    bbox.append(minX);
                    bbox.append(minY);
                    bbox.append(w);
                    bbox.append(h);
                    annObj["bbox"] = bbox;
                    annObj["area"] = w * h;
                }
            } else {
                // bbox: [x, y, w, h] 绝对像素坐标
                if (ann.bbox.size() == 4) {
                    QJsonArray bbox;
                    bbox.append(ann.bbox[0]);
                    bbox.append(ann.bbox[1]);
                    bbox.append(ann.bbox[2]);
                    bbox.append(ann.bbox[3]);
                    annObj["bbox"] = bbox;
                    annObj["area"] = ann.bbox[2] * ann.bbox[3];  // w * h
                }
            }

            annotationsArray.append(annObj);
        }

        ++imageId;
    }

    root["images"] = imagesArray;
    root["annotations"] = annotationsArray;
    return root;
}

// ---------------------------------------------------------------------------
// 导出为 YOLO 格式
// ---------------------------------------------------------------------------
ExportResult ExportManager::exportYOLO(const QList<ExportImageEntry>& entries,
                                        const QStringList& classNames,
                                        const QString& outputDir,
                                        double validationSplit)
{
    ExportResult result;
    result.outputDir = outputDir;
    result.totalCount = entries.size();

    QDV::Logger::info(QString("ExportManager: 开始 YOLO 导出，共 %1 张图像，验证集比例 %2")
                      .arg(entries.size()).arg(validationSplit));

    // 输入校验
    if (entries.isEmpty()) {
        result.success = false;
        result.errorMessage = QStringLiteral("图像条目列表为空");
        emit exportCompleted(result);
        return result;
    }

    // ---- 创建目录结构: images/{train,val} + labels/{train,val} ----
    QDir outDir(outputDir);
    QString imagesTrainDir = outDir.absoluteFilePath("images/train");
    QString imagesValDir = outDir.absoluteFilePath("images/val");
    QString labelsTrainDir = outDir.absoluteFilePath("labels/train");
    QString labelsValDir = outDir.absoluteFilePath("labels/val");

    if (!outDir.mkpath("images/train") || !outDir.mkpath("images/val") ||
        !outDir.mkpath("labels/train") || !outDir.mkpath("labels/val")) {
        result.success = false;
        result.errorMessage = QStringLiteral("无法创建输出目录结构");
        emit exportCompleted(result);
        return result;
    }

    // ---- 划分 train/val (85:15 等距轮转采样) ----
    QPair<QList<int>, QList<int>> split = splitTrainVal(entries.size(), validationSplit);
    const QList<int>& trainIndices = split.first;
    const QList<int>& valIndices = split.second;

    QSet<int> valSet;
    for (int idx : valIndices) {
        valSet.insert(idx);
    }

    // ---- 遍历图像，复制并生成标签 ----
    int processed = 0;
    for (int i = 0; i < entries.size(); ++i) {
        const ExportImageEntry& entry = entries[i];
        QFileInfo imgInfo(entry.path);

        if (!imgInfo.exists()) {
            QDV::Logger::warn(QString("ExportManager: 图像文件不存在，跳过: %1").arg(entry.path));
            ++processed;
            emit exportProgress(processed, entries.size(),
                                QStringLiteral("跳过不存在的图像: %1").arg(imgInfo.fileName()));
            continue;
        }

        // 判断属于 train 还是 val
        bool isVal = valSet.contains(i);
        QString imgDstDir = isVal ? imagesValDir : imagesTrainDir;
        QString lblDstDir = isVal ? labelsValDir : labelsTrainDir;

        if (isVal) {
            ++result.valCount;
        } else {
            ++result.trainCount;
        }

        // ---- 复制图像（避免重名冲突） ----
        QString baseName = imgInfo.fileName();
        QString stem = imgInfo.completeBaseName();
        QString suffix = imgInfo.suffix();
        QString imgDstPath = QDir(imgDstDir).absoluteFilePath(baseName);

        // 重名时附加索引后缀
        if (QFile::exists(imgDstPath)) {
            QString newName = QString("%1_%2.%3").arg(stem).arg(i).arg(suffix);
            imgDstPath = QDir(imgDstDir).absoluteFilePath(newName);
        }

        if (!copyImage(entry.path, imgDstPath)) {
            QDV::Logger::warn(QString("ExportManager: 复制图像失败，跳过: %1").arg(entry.path));
            ++processed;
            emit exportProgress(processed, entries.size(),
                                QStringLiteral("复制失败: %1").arg(imgInfo.fileName()));
            continue;
        }

        // ---- 生成标签文件（文件名与图像同名，扩展名 .txt） ----
        QString labelStem = QFileInfo(imgDstPath).completeBaseName();
        QString labelPath = QDir(lblDstDir).absoluteFilePath(labelStem + ".txt");

        // 读取图像尺寸用于 bbox 归一化
        QSize imgSize = readImageSize(entry.path);
        if (imgSize.isEmpty()) {
            // 无法读取尺寸时生成空标签文件（背景样本）
            QFile emptyFile(labelPath);
            if (emptyFile.open(QIODevice::WriteOnly)) {
                emptyFile.close();
            }
        } else {
            writeYoloLabel(labelPath, entry.annotations, imgSize);
        }

        ++processed;
        emit exportProgress(processed, entries.size(),
                            QStringLiteral("已处理: %1").arg(imgInfo.fileName()));
    }

    // ---- 生成 data.yaml ----
    QString yamlPath = outDir.absoluteFilePath("data.yaml");
    if (!writeDataYaml(yamlPath, "./", "images/train", "images/val",
                       classNames.size(), classNames)) {
        result.success = false;
        result.errorMessage = QStringLiteral("生成 data.yaml 失败");
        emit exportCompleted(result);
        return result;
    }
    result.dataYamlPath = yamlPath;

    result.success = true;
    QDV::Logger::info(QString("ExportManager: YOLO 导出完成，train=%1, val=%2")
                      .arg(result.trainCount).arg(result.valCount));

    emit exportCompleted(result);
    return result;
}

// ---------------------------------------------------------------------------
// 导出为 COCO 格式
// ---------------------------------------------------------------------------
ExportResult ExportManager::exportCOCO(const QList<ExportImageEntry>& entries,
                                        const QStringList& classNames,
                                        const QString& outputDir,
                                        double validationSplit)
{
    ExportResult result;
    result.outputDir = outputDir;
    result.totalCount = entries.size();

    QDV::Logger::info(QString("ExportManager: 开始 COCO 导出，共 %1 张图像，验证集比例 %2")
                      .arg(entries.size()).arg(validationSplit));

    // 输入校验
    if (entries.isEmpty()) {
        result.success = false;
        result.errorMessage = QStringLiteral("图像条目列表为空");
        emit exportCompleted(result);
        return result;
    }

    // ---- 创建目录结构: images/ + annotations/ ----
    QDir outDir(outputDir);
    QString imagesDir = outDir.absoluteFilePath("images");
    QString annotationsDir = outDir.absoluteFilePath("annotations");

    if (!outDir.mkpath("images") || !outDir.mkpath("annotations")) {
        result.success = false;
        result.errorMessage = QStringLiteral("无法创建输出目录结构");
        emit exportCompleted(result);
        return result;
    }

    // ---- 划分 train/val (85:15 等距轮转采样) ----
    QPair<QList<int>, QList<int>> split = splitTrainVal(entries.size(), validationSplit);
    const QList<int>& trainIndices = split.first;
    const QList<int>& valIndices = split.second;

    result.trainCount = trainIndices.size();
    result.valCount = valIndices.size();

    // ---- 复制所有图像到 images/ ----
    int processed = 0;
    QList<int> allIndices = trainIndices + valIndices;
    for (int idx : allIndices) {
        if (idx < 0 || idx >= entries.size()) {
            continue;
        }

        const ExportImageEntry& entry = entries[idx];
        QFileInfo imgInfo(entry.path);

        if (!imgInfo.exists()) {
            QDV::Logger::warn(QString("ExportManager: 图像文件不存在，跳过: %1").arg(entry.path));
            ++processed;
            emit exportProgress(processed, entries.size(),
                                QStringLiteral("跳过不存在的图像: %1").arg(imgInfo.fileName()));
            continue;
        }

        // 复制图像（避免重名冲突）
        QString baseName = imgInfo.fileName();
        QString stem = imgInfo.completeBaseName();
        QString suffix = imgInfo.suffix();
        QString imgDstPath = QDir(imagesDir).absoluteFilePath(baseName);

        if (QFile::exists(imgDstPath)) {
            QString newName = QString("%1_%2.%3").arg(stem).arg(idx).arg(suffix);
            imgDstPath = QDir(imagesDir).absoluteFilePath(newName);
        }

        copyImage(entry.path, imgDstPath);

        ++processed;
        emit exportProgress(processed, entries.size(),
                            QStringLiteral("已复制: %1").arg(imgInfo.fileName()));
    }

    // ---- 生成 annotations.json (COCO 格式) ----
    QJsonObject cocoJson = buildCocoAnnotation(entries, classNames, trainIndices, valIndices);

    QString annotationsPath = QDir(annotationsDir).absoluteFilePath("annotations.json");
    QFile jsonFile(annotationsPath);
    if (!jsonFile.open(QIODevice::WriteOnly)) {
        result.success = false;
        result.errorMessage = QStringLiteral("无法创建 annotations.json");
        emit exportCompleted(result);
        return result;
    }

    QJsonDocument doc(cocoJson);
    jsonFile.write(doc.toJson(QJsonDocument::Indented));
    jsonFile.close();

    result.annotationsPath = annotationsPath;
    result.success = true;

    QDV::Logger::info(QString("ExportManager: COCO 导出完成，train=%1, val=%2")
                      .arg(result.trainCount).arg(result.valCount));

    emit exportCompleted(result);
    return result;
}

} // namespace QDV
