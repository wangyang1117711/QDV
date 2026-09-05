// =====================================================================
// AnnotationSession.cpp — 会话控制器实现
// =====================================================================
#include "AnnotationSession.h"
#include "Exporters.h"
#include "ImagePreprocess.h"
#include "TrainingBridge.h"

#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QImageReader>
#include <QTextStream>
#include <QImage>
#include <QPainter>
#include <QUuid>
#include <QJsonDocument>
#include <QJsonArray>
#include <QSaveFile>
#include <QPair>
#include <QMap>
#include <QHash>
#include <algorithm>
#include <cmath>

// labelId 读取：缺失返回 -1（label 0 是合法类别，不能用 0 作哨兵）
static int lidOf(const QVariantMap& m) {
    return m.contains("labelId") ? m.value("labelId").toInt() : -1;
}

// 字符串读取：缺失返回默认值
static QString strOr(const QVariantMap& m, const QString& k, const QString& def = QString()) {
    return m.contains(k) ? m.value(k).toString() : def;
}
static QString strOr(const QJsonObject& j, const QString& k, const QString& def = QString()) {
    return j.contains(k) ? j.value(k).toString() : def;
}

AnnotationSession::AnnotationSession(QObject* parent)
    : QObject(parent)
{
    // 标训闭环：训练桥信号转发到本会话信号，供 QML 监听
    m_trainer = new TrainingBridge(this);
    connect(m_trainer, &TrainingBridge::trainingProgress,
            this, &AnnotationSession::trainProgress);
    connect(m_trainer, &TrainingBridge::trainingCompleted,
            this, [this](const QVariantMap& r) {
                m_isTraining = false;
                emit isTrainingChanged();
                emit trainCompleted(r);
            });
    connect(m_trainer, &TrainingBridge::trainingError,
            this, [this](const QString& phase, const QString& message) {
                // 训练出错（含心跳超时被 kill / 子进程异常退出）时必须复位训练中状态，
                // 否则 isTraining 恒为 true 且无 progress，UI 会永久停在“等待训练开始”，
                // 只能重启程序才能恢复。
                if (m_isTraining) {
                    m_isTraining = false;
                    emit isTrainingChanged();
                }
                emit trainError(phase, message);
            });
    connect(m_trainer, &TrainingBridge::logOutput,
            this, &AnnotationSession::trainLog);
}

// ---------------- 任务类型 ----------------
AnnotationSession::Task AnnotationSession::taskFromName(const QString& s)
{
    if (s == "classification") return Task::Classification;
    if (s == "detection")     return Task::Detection;
    if (s == "segmentation")  return Task::Segmentation;
    if (s == "ocr")           return Task::Ocr;
    if (s == "keypoint")      return Task::Keypoint;
    return Task::Detection;
}
QString AnnotationSession::taskToName(Task t)
{
    switch (t) {
    case Task::Classification: return "classification";
    case Task::Detection:      return "detection";
    case Task::Segmentation:   return "segmentation";
    case Task::Ocr:            return "ocr";
    case Task::Keypoint:       return "keypoint";
    }
    return "detection";
}

QString AnnotationSession::taskType() const { return m_taskType; }
void AnnotationSession::setTaskType(const QString& t)
{
    if (t == m_taskType) return;
    m_taskType = t;
    emit taskTypeChanged();
}

// ---------------- 标签管理 ----------------
QStringList AnnotationSession::labels() const { return m_labels; }
QStringList AnnotationSession::labelColors() const { return m_labelColors; }

QString AnnotationSession::colorForLabel(int id) const
{
    if (id >= 0 && id < m_labelColors.size()) return m_labelColors[id];
    return "#7C4DFF";
}

static QString nextColor(int idx)
{
    // 复用项目 DesignTokens 分类色族，保证视觉一致
    static const QStringList palette = {
        "#FFB74D","#7986CB","#4DB6AC","#AED581","#4DD0E1","#64B5F6",
        "#CE93D8","#F06292","#FF8A65","#90A4AE","#81C784","#EF5350",
        "#BA68C8","#4FC3F7","#FFF176","#A1887F"
    };
    return palette[idx % palette.size()];
}

int AnnotationSession::addLabel(const QString& name, const QString& color)
{
    pushUndo("addLabel");
    m_labels.append(name);
    m_labelColors.append(color.isEmpty() ? nextColor(m_labels.size() - 1) : color);
    emit labelsChanged();
    return m_labels.size() - 1;
}
bool AnnotationSession::renameLabel(int id, const QString& name)
{
    if (id < 0 || id >= m_labels.size()) return false;
    pushUndo("renameLabel");
    m_labels[id] = name;
    emit labelsChanged();
    return true;
}
bool AnnotationSession::removeLabel(int id)
{
    if (id < 0 || id >= m_labels.size()) return false;
    pushUndo("removeLabel");
    m_labels.removeAt(id);
    m_labelColors.removeAt(id);
    // 修正所有标注的 labelId
    for (QVariant& v : m_images) {
        QVariantMap img = v.toMap();
        QVariantList anns = img.value("annotations").toList();
        for (QVariant& a : anns) {
            QVariantMap ann = a.toMap();
            int lid = lidOf(ann);
            if (lid == id) ann["labelId"] = -1;
            else if (lid > id) ann["labelId"] = lid - 1;
            a = ann;
        }
        img["annotations"] = anns;
        v = img;
    }
    emit labelsChanged();
    emit imagesChanged();
    return true;
}

// ---------------- 数据集导入 ----------------
bool AnnotationSession::loadImageFolder(const QString& folder,
                                        const QStringList& filters)
{
    QDir dir(folder);
    if (!dir.exists()) { setLastError("目录不存在: " + folder); return false; }

    QStringList nameFilters = filters;
    if (nameFilters.isEmpty()) {
        nameFilters << "*.bmp" << "*.png" << "*.jpg" << "*.jpeg" << "*.tif" << "*.tiff";
    }
    const auto entries = dir.entryInfoList(nameFilters, QDir::Files, QDir::Name);
    if (entries.isEmpty()) { setLastError("未找到图像文件: " + folder); return false; }

    for (const QFileInfo& fi : entries) {
        QImageReader r(fi.absoluteFilePath());
        QSize sz = r.size();
        QVariantMap img;
        img["id"]    = QUuid::createUuid().toString();
        img["path"]  = fi.absoluteFilePath();
        img["fileName"] = fi.fileName();
        img["width"] = sz.isValid() ? sz.width() : 0;
        img["height"]= sz.isValid() ? sz.height() : 0;
        img["annotations"] = QVariantList();
        img["classLabels"] = QVariantList();
        img["subset"] = "unassigned";
        m_images.append(img);
    }
    m_current = m_images.isEmpty() ? -1 : 0;
    emit imagesChanged();
    emit currentImageIndexChanged();
    emit currentImageChanged();
    updatePreprocess();
    resetHistory(); // 新数据集：清空撤销历史 + 清除未保存标记
    return true;
}

bool AnnotationSession::addImage(const QString& path)
{
    QFileInfo fi(path);
    if (!fi.exists()) { setLastError("文件不存在: " + path); return false; }
    pushUndo("addImage");
    QImageReader r(path);
    QSize sz = r.size();
    QVariantMap img;
    img["id"] = QUuid::createUuid().toString();
    img["path"] = path;
    img["fileName"] = fi.fileName();
    img["width"] = sz.isValid() ? sz.width() : 0;
    img["height"]= sz.isValid() ? sz.height() : 0;
    img["annotations"] = QVariantList();
    img["classLabels"] = QVariantList();
    img["subset"] = "unassigned";
    m_images.append(img);
    emit imagesChanged();
    return true;
}

// ---------------- 撤销/重做 + 未保存提示 ----------------
bool AnnotationSession::canUndo() const { return !m_undoStack.isEmpty(); }
bool AnnotationSession::canRedo() const { return !m_redoStack.isEmpty(); }
bool AnnotationSession::dirty() const { return m_dirty; }

void AnnotationSession::pushUndo(const QString& op)
{
    m_undoStack.append(StateSnapshot{op, m_images, m_labels, m_labelColors, m_current});
    m_redoStack.clear();
    emit undoRedoChanged();
    setDirty(true);
}

void AnnotationSession::applyState(const StateSnapshot& s)
{
    m_images = s.images;
    m_labels = s.labels;
    m_labelColors = s.labelColors;
    m_current = s.currentIndex;
    if (m_current >= m_images.size())
        m_current = m_images.isEmpty() ? -1 : m_images.size() - 1;
    emit labelsChanged();
    emit imagesChanged();
    emit currentImageIndexChanged();
    emit currentImageChanged();
    setSelectedAnnotationIndex(-1); // 状态恢复后旧选中索引可能失效
    updatePreprocess();
}

void AnnotationSession::undo()
{
    if (m_undoStack.isEmpty()) return;
    // 当前状态快照压入 redo 栈（供 redo 恢复）
    m_redoStack.append(StateSnapshot{m_undoStack.last().op, m_images, m_labels, m_labelColors, m_current});
    StateSnapshot prev = m_undoStack.takeLast();
    applyState(prev);
    emit undoRedoChanged();
    setDirty(true); // undo 后相对磁盘仍是“已更改”
}

void AnnotationSession::redo()
{
    if (m_redoStack.isEmpty()) return;
    m_undoStack.append(StateSnapshot{m_redoStack.last().op, m_images, m_labels, m_labelColors, m_current});
    StateSnapshot next = m_redoStack.takeLast();
    applyState(next);
    emit undoRedoChanged();
    setDirty(true);
}

void AnnotationSession::setDirty(bool d)
{
    if (m_dirty == d) return;
    m_dirty = d;
    emit dirtyChanged();
}

void AnnotationSession::clearDirty() { setDirty(false); }

// ---- 当前图选中标注索引（画布/列表双向联动真源）----
int AnnotationSession::selectedAnnotationIndex() const { return m_selectedAnnotation; }
void AnnotationSession::setSelectedAnnotationIndex(int i)
{
    if (i == m_selectedAnnotation) return;
    m_selectedAnnotation = i;
    emit selectedAnnotationIndexChanged();
}

void AnnotationSession::resetHistory()
{
    m_undoStack.clear();
    m_redoStack.clear();
    emit undoRedoChanged();
    setDirty(false);
}

// ---------------- 标注读写 ----------------
void AnnotationSession::addAnnotation(const QVariantMap& ann)
{
    if (m_current < 0 || m_current >= m_images.size()) return;
    pushUndo("addAnnotation");
    QVariantMap img = m_images[m_current].toMap();
    QVariantList anns = img.value("annotations").toList();
    anns.append(ann);
    img["annotations"] = anns;
    m_images[m_current] = img;
    emit currentImageChanged();
    emit imagesChanged();
}
void AnnotationSession::updateAnnotation(int index, const QVariantMap& ann)
{
    if (m_current < 0 || m_current >= m_images.size()) return;
    QVariantMap img = m_images[m_current].toMap();
    QVariantList anns = img.value("annotations").toList();
    if (index < 0 || index >= anns.size()) return;
    pushUndo("updateAnnotation");
    anns[index] = ann;
    img["annotations"] = anns;
    m_images[m_current] = img;
    emit currentImageChanged();
    emit imagesChanged();
}
void AnnotationSession::removeAnnotation(int index)
{
    if (m_current < 0 || m_current >= m_images.size()) return;
    QVariantMap img = m_images[m_current].toMap();
    QVariantList anns = img.value("annotations").toList();
    if (index < 0 || index >= anns.size()) return;
    pushUndo("removeAnnotation");
    anns.removeAt(index);
    img["annotations"] = anns;
    m_images[m_current] = img;
    emit currentImageChanged();
    emit imagesChanged();
}
QVariantList AnnotationSession::currentAnnotations() const
{
    if (m_current < 0 || m_current >= m_images.size()) return QVariantList();
    return m_images[m_current].toMap().value("annotations").toList();
}

void AnnotationSession::setImageClassLabels(const QVariantList& labelIds)
{
    if (m_current < 0 || m_current >= m_images.size()) return;
    pushUndo("setImageClassLabels");
    QVariantMap img = m_images[m_current].toMap();
    img["classLabels"] = labelIds;
    m_images[m_current] = img;
    emit currentImageChanged();
    emit imagesChanged();
}
QVariantList AnnotationSession::imageClassLabels() const
{
    if (m_current < 0 || m_current >= m_images.size()) return QVariantList();
    return m_images[m_current].toMap().value("classLabels").toList();
}

void AnnotationSession::setImageSubset(int imageIndex, const QString& subset)
{
    if (imageIndex < 0 || imageIndex >= m_images.size()) return;
    pushUndo("setImageSubset");
    QVariantMap img = m_images[imageIndex].toMap();
    img["subset"] = subset;
    m_images[imageIndex] = img;
    emit imagesChanged();
}

// ---------------- 属性访问 ----------------
QVariantList AnnotationSession::images() const { return m_images; }
int AnnotationSession::currentImageIndex() const { return m_current; }
void AnnotationSession::setCurrentImageIndex(int i)
{
    if (i < 0 || i >= m_images.size()) return; // 越界保护
    if (i == m_current) return;
    m_current = i;
    setSelectedAnnotationIndex(-1); // 切图后上一张的选中索引不再有效
    emit currentImageIndexChanged();
    emit currentImageChanged();
    updatePreprocess();
}
QVariantMap AnnotationSession::currentImage() const
{
    if (m_current < 0 || m_current >= m_images.size()) return QVariantMap();
    return m_images[m_current].toMap();
}
QString AnnotationSession::projectPath() const { return m_projectPath; }
QString AnnotationSession::lastError() const { return m_lastError; }
int AnnotationSession::annotatedCount() const
{
    int n = 0;
    for (const QVariant& v : m_images) {
        const QVariantMap img = v.toMap();
        Task t = taskFromName(m_taskType);
        if (t == Task::Classification) {
            if (!img.value("classLabels").toList().isEmpty()) ++n;
        } else {
            if (!img.value("annotations").toList().isEmpty()) ++n;
        }
    }
    return n;
}

QVariantMap AnnotationSession::statistics() const
{
    QVariantMap stat;
    stat["total"] = m_images.size();
    stat["annotated"] = annotatedCount();
    stat["labels"] = m_labels.size();
    // 每类数量
    QVariantMap perClass;
    for (int i = 0; i < m_labels.size(); ++i) perClass[m_labels[i]] = 0;
    for (const QVariant& v : m_images) {
        const QVariantMap img = v.toMap();
        Task t = taskFromName(m_taskType);
        if (t == Task::Classification) {
            for (const QVariant& lid : img.value("classLabels").toList())
                perClass[m_labels[lid.toInt()]] = perClass[m_labels[lid.toInt()]].toInt() + 1;
        } else {
            for (const QVariant& a : img.value("annotations").toList()) {
                int lid = lidOf(a.toMap());
                if (lid >= 0 && lid < m_labels.size())
                    perClass[m_labels[lid]] = perClass[m_labels[lid]].toInt() + 1;
            }
        }
    }
    stat["perClass"] = perClass;
    return stat;
}

// ---------------- 存储 ----------------
void AnnotationSession::setLastError(const QString& e)
{
    m_lastError = e;
    emit lastErrorChanged();
}

bool AnnotationSession::saveProject(const QString& path)
{
    QJsonObject root;
    root["version"] = "1.0";
    root["app"] = "QDV Annotator";
    root["taskType"] = m_taskType;
    root["labels"] = QJsonArray::fromStringList(m_labels);
    root["labelColors"] = QJsonArray::fromStringList(m_labelColors);

    QJsonArray imgs;
    for (const QVariant& v : m_images) {
        const QVariantMap img = v.toMap();
        QJsonObject o;
        o["id"] = img.value("id").toString();
        o["path"] = img.value("path").toString();
        o["fileName"] = img.value("fileName").toString();
        o["width"] = img.value("width").toInt();
        o["height"] = img.value("height").toInt();
        o["subset"] = img.value("subset").toString();
        QJsonArray anns;
        for (const QVariant& a : img.value("annotations").toList()) {
            const QVariantMap ann = a.toMap();
            QJsonObject ao;
            ao["shape"] = ann.value("shape").toString();
            ao["labelId"] = lidOf(ann);
            // 方向矩形（带矢量方向）：保留中心/角度/半长/半宽/根数，方向信息不丢失
            if (ann.value("shape").toString() == "vec_rect") {
                ao["x"] = ann.value("x").toDouble();
                ao["y"] = ann.value("y").toDouble();
                ao["angle"] = ann.value("angle").toDouble();
                ao["length1"] = ann.value("length1").toDouble();
                ao["length2"] = ann.value("length2").toDouble();
                ao["count"] = ann.value("count").toInt();
            }
            if (ann.contains("x")) ao["x"] = ann.value("x").toInt();
            if (ann.contains("y")) ao["y"] = ann.value("y").toInt();
            if (ann.contains("w")) ao["w"] = ann.value("w").toInt();
            if (ann.contains("h")) ao["h"] = ann.value("h").toInt();
            if (ann.contains("text")) ao["text"] = ann.value("text").toString();
            if (ann.contains("points")) {
                QJsonArray pts;
                for (const QVariant& p : ann.value("points").toList()) {
                    const QVariantMap pm = p.toMap();
                    QJsonObject po; po["x"] = pm.value("x").toInt(); po["y"] = pm.value("y").toInt();
                    pts.append(po);
                }
                ao["points"] = pts;
            }
            anns.append(ao);
        }
        o["annotations"] = anns;
        QJsonArray cl;
        for (const QVariant& lid : img.value("classLabels").toList()) cl.append(lid.toInt());
        o["classLabels"] = cl;
        imgs.append(o);
    }
    root["images"] = imgs;

    QJsonDocument doc(root);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) { setLastError("无法写入: " + path); return false; }
    f.write(doc.toJson(QJsonDocument::Indented));
    f.close();
    m_projectPath = path;
    emit projectPathChanged();
    setDirty(false); // 保存成功后清除未保存标记
    return true;
}

bool AnnotationSession::openProject(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) { setLastError("无法打开: " + path); return false; }
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    if (!doc.isObject()) { setLastError("文件格式错误"); return false; }
    const QJsonObject root = doc.object();
    m_taskType = strOr(root, "taskType", "detection");
    m_labels.clear(); m_labelColors.clear();
    for (const QJsonValue& v : root.value("labels").toArray()) m_labels.append(v.toString());
    for (const QJsonValue& v : root.value("labelColors").toArray()) m_labelColors.append(v.toString());
    while (m_labelColors.size() < m_labels.size()) m_labelColors.append(nextColor(m_labelColors.size()));

    m_images.clear();
    for (const QJsonValue& v : root.value("images").toArray()) {
        const QJsonObject o = v.toObject();
        QVariantMap img;
        img["id"] = o.value("id").toString();
        // 路径失效时自动回退解析（图片移入子目录等场景），保证导入后图片仍可显示
        img["path"] = Qdv::resolveImagePath(o.value("path").toString(),
                                            o.value("fileName").toString());
        img["fileName"] = o.value("fileName").toString();
        img["width"] = o.value("width").toInt();
        img["height"] = o.value("height").toInt();
        img["subset"] = strOr(o, "subset", "unassigned");
        QVariantList anns;
        for (const QJsonValue& av : o.value("annotations").toArray()) {
            const QJsonObject ao = av.toObject();
            QVariantMap ann;
            ann["shape"] = ao.value("shape").toString();
            ann["labelId"] = ao.contains("labelId") ? ao.value("labelId").toInt() : -1;
            // 向量矩形：读回中心/角度/半长/半宽/根数（与导出对称，避免方向信息丢失）
            if (ann["shape"].toString() == "vec_rect") {
                ann["x"] = ao.value("x").toDouble();
                ann["y"] = ao.value("y").toDouble();
                ann["angle"] = ao.value("angle").toDouble();
                ann["length1"] = ao.value("length1").toDouble();
                ann["length2"] = ao.value("length2").toDouble();
                ann["count"] = ao.value("count").toInt();
            } else if (ao.contains("x")) ann["x"] = ao.value("x").toInt();
            else if (ao.contains("y")) ann["y"] = ao.value("y").toInt();
            else if (ao.contains("w")) ann["w"] = ao.value("w").toInt();
            else if (ao.contains("h")) ann["h"] = ao.value("h").toInt();
            if (ao.contains("text")) ann["text"] = ao.value("text").toString();
            if (ao.contains("points")) {
                QVariantList pts;
                for (const QJsonValue& pv : ao.value("points").toArray()) {
                    const QJsonObject po = pv.toObject();
                    QVariantMap pm; pm["x"] = po.value("x").toInt(); pm["y"] = po.value("y").toInt();
                    pts.append(pm);
                }
                ann["points"] = pts;
            }
            anns.append(ann);
        }
        img["annotations"] = anns;
        QVariantList cl;
        for (const QJsonValue& cv : o.value("classLabels").toArray()) cl.append(cv.toInt());
        img["classLabels"] = cl;
        m_images.append(img);
    }
    m_current = m_images.isEmpty() ? -1 : 0;
    m_projectPath = path;
    emit taskTypeChanged(); emit labelsChanged(); emit imagesChanged();
    emit currentImageIndexChanged(); emit currentImageChanged(); emit projectPathChanged();
    updatePreprocess();
    resetHistory(); // 打开工程：清空撤销历史 + 清除未保存标记
    return true;
}

// ---------------- 导出 / 导入（成员实现，调用 Qdv 辅助函数）----------------
bool AnnotationSession::exportDataset(const QString& format,
                                      const QString& outDir,
                                      const QVariantMap& options)
{
    if (format == "qdvann") {
        return saveProject(outDir); // 原生工程文件
    }

    const QStringList labels = m_labels;
    if (labels.isEmpty()) {
        setLastError("请先添加至少一个标签后再导出");
        return false;
    }

    QDir().mkpath(outDir);
    const QString absRoot = QFileInfo(outDir).absoluteFilePath();
    const bool copyImages = options.value("copyImages", true).toBool();
    const QVariantList imgs = m_images;
    const auto plan = Qdv::planSubsets(this, options);

    if (format == "yolo_detect") {
        Qdv::writeDataYaml(absRoot + "/dataset.yaml", absRoot, labels, false);
        Qdv::writeLabelsJson(absRoot, labels);
        Qdv::writeCategoryLabelsJson(absRoot, labels);
        // 建议2：旋转(vec_rect)标注防护。轴对齐 YOLO 无法表达角度，若工程只含
        // vec_rect 而没有 rect，直接报错并引导改用 yolo_obb，避免"静默产出空标签"。
        {
            bool hasVec = false, hasRect = false;
            for (const QVariant& v : imgs) {
                for (const QVariant& a : v.toMap().value("annotations").toList()) {
                    const QString s = a.toMap().value("shape").toString();
                    if (s == "vec_rect") hasVec = true;
                    else if (s == "rect") hasRect = true;
                }
            }
            if (hasVec && !hasRect) {
                setLastError("检测到旋转(vec_rect)标注，轴对齐 YOLO 无法表达角度。请改用 yolo_obb 导出格式。");
                return false;
            }
        }
        // 关键：先确保 images/<sub> 和 labels/<sub> 目录存在，否则 QSaveFile 父目录缺失会静默失败
        if (!QDir().mkpath(absRoot + "/images/train") ||
            !QDir().mkpath(absRoot + "/images/val")  ||
            !QDir().mkpath(absRoot + "/labels/train") ||
            !QDir().mkpath(absRoot + "/labels/val")) {
            setLastError("无法创建数据集子目录: " + absRoot + "/{images,labels}/{train,val}");
            return false;
        }
        for (int i = 0; i < imgs.size(); ++i) {
            const QVariantMap img = imgs[i].toMap();
            const QString sub = plan[i].second;
            const int W = img.value("width").toInt();
            const int H = img.value("height").toInt();
            QFileInfo fi(img.value("path").toString());
            QString stem = fi.completeBaseName();
            Qdv::copyImage(img.value("path").toString(), absRoot + "/images/" + sub, copyImages);
            const QString lblPath = absRoot + "/labels/" + sub + "/" + stem + ".txt";
            QSaveFile tf(lblPath);
            if (!tf.open(QIODevice::WriteOnly)) {
                setLastError("无法写入 label 文件: " + lblPath + " (" + tf.errorString() + ")");
                return false;
            }
            QTextStream ts(&tf); ts.setEncoding(QStringConverter::Utf8);
            for (const QVariant& a : img.value("annotations").toList()) {
                const QVariantMap ann = a.toMap();
                if (ann.value("shape").toString() != "rect") continue;
                int lid = lidOf(ann);
                if (lid < 0) continue;
                double cx, cy, nw, nh;
                Qdv::normRect(ann.value("x").toInt(), ann.value("y").toInt(),
                              ann.value("w").toInt(), ann.value("h").toInt(), W, H, cx, cy, nw, nh);
                ts << lid << " " << QString::number(cx, 'f', 6) << " "
                   << QString::number(cy, 'f', 6) << " "
                   << QString::number(nw, 'f', 6) << " "
                   << QString::number(nh, 'f', 6) << "\n";
            }
            if (!tf.commit()) {
                setLastError("commit label 文件失败: " + lblPath);
                return false;
            }
        }
        return true;
    }

    // 核心新增：yolo_obb —— 读取 vec_rect(旋转矩形) 写 4 角点 OBB（YOLOv8-OBB 格式）。
    // 规格对齐本项目 obb_dataset / auto_label_server.py：`cls x1 y1 x2 y2 x3 y3 x4 y4` 全部 0-1 归一化，
    // 4 角点逆时针，class = 根数(count)-1（对"每类=一种根数"的计数任务，索引与 labels 对齐）。
    // 不触碰 yolo_detect / yolo_train.py 的既有训练链路。
    if (format == "yolo_obb") {
        Qdv::writeDataYaml(absRoot + "/dataset.yaml", absRoot, labels, false);
        Qdv::writeLabelsJson(absRoot, labels);
        Qdv::writeCategoryLabelsJson(absRoot, labels);
        QDir().mkpath(absRoot + "/masks"); // 占位，防止部分消费端误判 seg，无需实际掩码
        if (!QDir().mkpath(absRoot + "/images/train") ||
            !QDir().mkpath(absRoot + "/images/val")  ||
            !QDir().mkpath(absRoot + "/labels/train") ||
            !QDir().mkpath(absRoot + "/labels/val")) {
            setLastError("无法创建 yolo_obb 子目录: " + absRoot);
            return false;
        }

        // 统计（供非空校验 + dataset_report.json）
        qint64 annTotal = 0;
        int labeledImages = 0;
        QHash<int, int> clsCount;          // class 索引 -> 数量
        QMap<QString, int> sizeDist;       // "WxH" -> 图像数

        for (int i = 0; i < imgs.size(); ++i) {
            const QVariantMap img = imgs[i].toMap();
            const int W = img.value("width").toInt();
            const int H = img.value("height").toInt();
            sizeDist[QString("%1x%2").arg(W).arg(H)]++;
            const QString sub = plan[i].second;
            QFileInfo fi(img.value("path").toString());
            QString stem = fi.completeBaseName();
            Qdv::copyImage(img.value("path").toString(), absRoot + "/images/" + sub, copyImages);
            const QString lblPath = absRoot + "/labels/" + sub + "/" + stem + ".txt";
            QSaveFile tf(lblPath);
            if (!tf.open(QIODevice::WriteOnly)) {
                setLastError("无法写入 label: " + lblPath + " (" + tf.errorString() + ")");
                return false;
            }
            QTextStream ts(&tf); ts.setEncoding(QStringConverter::Utf8);
            int wrote = 0;
            const QVariantList anns = img.value("annotations").toList();
            // 该图以 vec_rect 为主时按旋转矩形导出；否则退化为轴对齐 rect（兼容混用工程）
            bool anyVec = false;
            for (const QVariant& a : anns)
                if (a.toMap().value("shape").toString() == "vec_rect") { anyVec = true; break; }
            for (const QVariant& a : anns) {
                const QVariantMap ann = a.toMap();
                if (ann.value("shape").toString() == "vec_rect") {
                    double cx = ann.value("x").toDouble(), cy = ann.value("y").toDouble();
                    double l1 = ann.value("length1").toDouble(), l2 = ann.value("length2").toDouble();
                    double th = ann.value("angle").toDouble() * (M_PI / 180.0);
                    int cls = ann.value("count").toInt() - 1;   // class = 根数 - 1
                    if (cls < 0 || cls >= (int)labels.size()) cls = lidOf(ann); // 越界回退 labelId
                    if (cls < 0 || cls >= (int)labels.size()) continue;
                    const double c = std::cos(th), s = std::sin(th);
                    // 局部角点 (±l1,±l2)，旋转后 4 角点（与 obb_dataset 对齐）
                    const double corner[4][2] = {{ l1, l2}, { l1,-l2}, {-l1,-l2}, {-l1, l2}};
                    ts << cls;
                    for (int k = 0; k < 4; ++k) {
                        double X = cx + corner[k][0] * c - corner[k][1] * s;
                        double Y = cy + corner[k][0] * s + corner[k][1] * c;
                        // 归一化后 clamp 到 [0,1]：边缘丝线的旋转角点可能轻微越界，
                        // 超出 ultralytics 校验容差(±0.01)会导致整个标签文件被判 corrupt 而丢弃
                        double nx = X / std::max(W, 1);
                        double ny = Y / std::max(H, 1);
                        if (nx < 0.0) nx = 0.0; else if (nx > 1.0) nx = 1.0;
                        if (ny < 0.0) ny = 0.0; else if (ny > 1.0) ny = 1.0;
                        ts << " " << QString::number(nx, 'f', 6)
                           << " " << QString::number(ny, 'f', 6);
                    }
                    ts << "\n";
                    clsCount[cls]++;
                    ++wrote;
                } else if (!anyVec && ann.value("shape").toString() == "rect") {
                    // 退化：工程无 vec_rect 时，按轴对齐矩形写出（OBB 即退化 4 角点）
                    int lid = lidOf(ann);
                    if (lid < 0) continue;
                    int x = ann.value("x").toInt(), y = ann.value("y").toInt();
                    int w = ann.value("w").toInt(), h = ann.value("h").toInt();
                    ts << lid;
                    const int c[4][2] = {{x,y},{x+w,y},{x+w,y+h},{x,y+h}};
                    for (int k = 0; k < 4; ++k) {
                        double nx = double(c[ k ][0]) / std::max(W,1);
                        double ny = double(c[ k ][1]) / std::max(H,1);
                        if (nx < 0.0) nx = 0.0; else if (nx > 1.0) nx = 1.0;
                        if (ny < 0.0) ny = 0.0; else if (ny > 1.0) ny = 1.0;
                        ts << " " << QString::number(nx, 'f', 6)
                           << " " << QString::number(ny, 'f', 6);
                    }
                    ts << "\n";
                    clsCount[lid]++;
                    ++wrote;
                }
            }
            if (!tf.commit()) { setLastError("commit label 失败: " + lblPath); return false; }
            if (wrote > 0) labeledImages++;
            annTotal += wrote;
        }

        // 建议3：非空校验 —— 非空标签为 0 时禁止产出"伪完成"数据集
        if (labeledImages == 0) {
            setLastError("yolo_obb 导出失败：非空标签为 0，工程内无可导出的 OBB/矩形标注，请检查标注内容。");
            return false;
        }

        // 建议3：生成 dataset_report.json
        QJsonObject rep;
        rep["format"] = "yolo_obb";
        rep["total_images"] = imgs.size();
        rep["labeled_images"] = labeledImages;
        rep["nonempty_label_count"] = labeledImages;
        rep["empty_label_ratio"] = imgs.isEmpty() ? 1.0
                                  : double(imgs.size() - labeledImages) / double(imgs.size());
        rep["annotations_total"] = annTotal;
        QJsonObject cc;
        for (auto it = clsCount.begin(); it != clsCount.end(); ++it) {
            QString n = (it.key() >= 0 && it.key() < (int)labels.size())
                        ? labels[it.key()]
                        : QString::number(it.key());
            cc[n] = it.value();
        }
        rep["class_counts"] = cc;
        QJsonObject sd;
        for (auto it = sizeDist.begin(); it != sizeDist.end(); ++it) sd[it.key()] = it.value();
        rep["image_size_distribution"] = sd;
        QSaveFile rf(absRoot + "/dataset_report.json");
        if (rf.open(QIODevice::WriteOnly)) {
            rf.write(QJsonDocument(rep).toJson(QJsonDocument::Indented));
            rf.commit();
        }
        return true;
    }

    if (format == "yolo_seg") {
        const bool withMasks = options.value("withMasks", true).toBool();
        Qdv::writeDataYaml(absRoot + "/dataset.yaml", absRoot, labels, withMasks);
        Qdv::writeLabelsJson(absRoot, labels);
        Qdv::writeCategoryLabelsJson(absRoot, labels);
        if (!QDir().mkpath(absRoot + "/images/train") ||
            !QDir().mkpath(absRoot + "/images/val")  ||
            !QDir().mkpath(absRoot + "/labels/train") ||
            !QDir().mkpath(absRoot + "/labels/val")  ||
            !QDir().mkpath(absRoot + "/masks/train") ||
            !QDir().mkpath(absRoot + "/masks/val")) {
            setLastError("无法创建 yolo_seg 子目录: " + absRoot);
            return false;
        }
        for (int i = 0; i < imgs.size(); ++i) {
            const QVariantMap img = imgs[i].toMap();
            const QString sub = plan[i].second;
            const int W = img.value("width").toInt();
            const int H = img.value("height").toInt();
            QFileInfo fi(img.value("path").toString());
            QString stem = fi.completeBaseName();
            Qdv::copyImage(img.value("path").toString(), absRoot + "/images/" + sub, copyImages);
            const QString lblPath = absRoot + "/labels/" + sub + "/" + stem + ".txt";
            QSaveFile tf(lblPath);
            if (!tf.open(QIODevice::WriteOnly)) {
                setLastError("无法写入 label: " + lblPath);
                return false;
            }
            QTextStream ts(&tf); ts.setEncoding(QStringConverter::Utf8);
            QImage mask(W, H, QImage::Format_Grayscale8); mask.fill(0);
            QPainter painter(&mask);
            for (const QVariant& a : img.value("annotations").toList()) {
                const QVariantMap ann = a.toMap();
                int lid = lidOf(ann);
                if (lid < 0) continue;
                if (ann.value("shape").toString() == "polygon") {
                    const QVariantList pts = ann.value("points").toList();
                    if (pts.size() < 3) continue;
                    ts << lid;
                    QPolygon poly;
                    for (const QVariant& p : pts) {
                        const QVariantMap pm = p.toMap();
                        int px = pm.value("x").toInt(), py = pm.value("y").toInt();
                        ts << " " << QString::number(px / double(std::max(W,1)), 'f', 6)
                           << " " << QString::number(py / double(std::max(H,1)), 'f', 6);
                        poly.append(QPoint(px, py));
                    }
                    ts << "\n";
                    if (withMasks && !poly.isEmpty()) {
                        painter.setBrush(QColor::fromRgb(lid + 1, lid + 1, lid + 1));
                        painter.setPen(Qt::NoPen); painter.drawPolygon(poly);
                    }
                } else if (ann.value("shape").toString() == "rect") {
                    int x = ann.value("x").toInt(), y = ann.value("y").toInt();
                    int w = ann.value("w").toInt(), h = ann.value("h").toInt();
                    ts << lid;
                    int corners[][2] = {{x,y},{x+w,y},{x+w,y+h},{x,y+h}};
                    for (int c = 0; c < 4; ++c)
                        ts << " " << QString::number(corners[c][0]/double(std::max(W,1)),'f',6)
                           << " " << QString::number(corners[c][1]/double(std::max(H,1)),'f',6);
                    ts << "\n";
                    if (withMasks) {
                        painter.setBrush(QColor::fromRgb(lid + 1, lid + 1, lid + 1));
                        painter.setPen(Qt::NoPen); painter.drawRect(x, y, w, h);
                    }
                }
            }
            tf.commit();
            if (withMasks) mask.save(absRoot + "/masks/" + sub + "/" + stem + ".png");
        }
        return true;
    }

    if (format == "classification") {
        Qdv::writeLabelsJson(absRoot, labels);
        for (int i = 0; i < imgs.size(); ++i) {
            const QVariantMap img = imgs[i].toMap();
            const QString sub = plan[i].second;
            for (const QVariant& lidv : img.value("classLabels").toList()) {
                int lid = lidv.toInt();
                if (lid < 0 || lid >= labels.size()) continue;
                Qdv::copyImage(img.value("path").toString(),
                               absRoot + "/" + sub + "/" + labels[lid], copyImages);
            }
        }
        return true;
    }

    if (format == "ocr") {
        Qdv::writeLabelsJson(absRoot, labels);
        if (!QDir().mkpath(absRoot + "/images/train") ||
            !QDir().mkpath(absRoot + "/images/val")  ||
            !QDir().mkpath(absRoot + "/labels/train") ||
            !QDir().mkpath(absRoot + "/labels/val")) {
            setLastError("无法创建 ocr 子目录: " + absRoot);
            return false;
        }
        QSaveFile gt(absRoot + "/gt.txt");
        if (!gt.open(QIODevice::WriteOnly)) {
            setLastError("无法写入 gt.txt: " + gt.errorString());
            return false;
        }
        QTextStream gts(&gt); gts.setEncoding(QStringConverter::Utf8);
        for (int i = 0; i < imgs.size(); ++i) {
            const QVariantMap img = imgs[i].toMap();
            const QString sub = plan[i].second;
            const int W = img.value("width").toInt();
            const int H = img.value("height").toInt();
            QFileInfo fi(img.value("path").toString());
            QString stem = fi.completeBaseName();
            QString relName = Qdv::copyImage(img.value("path").toString(),
                                            absRoot + "/images/" + sub, copyImages);
            QJsonObject jo; jo["image_width"] = W; jo["image_height"] = H;
            QJsonArray anns; QStringList texts;
            for (const QVariant& a : img.value("annotations").toList()) {
                const QVariantMap ann = a.toMap();
                if (ann.value("shape").toString() != "rect" || lidOf(ann) < 0) continue;
                QJsonObject ao; QJsonArray bbox;
                bbox.append(ann.value("x").toInt()); bbox.append(ann.value("y").toInt());
                bbox.append(ann.value("w").toInt()); bbox.append(ann.value("h").toInt());
                ao["bbox"] = bbox;
                QString txt = ann.value("text").toString();
                ao["text"] = txt;
                ao["label"] = labels[lidOf(ann)];
                anns.append(ao);
                if (!txt.isEmpty()) texts.append(txt);
            }
            jo["annotations"] = anns;
            QSaveFile jf(absRoot + "/labels/" + sub + "/" + stem + ".json");
            if (!jf.open(QIODevice::WriteOnly)) {
                setLastError("无法写入 ocr json: " + jf.errorString());
                return false;
            }
            jf.write(QJsonDocument(jo).toJson(QJsonDocument::Indented));
            jf.commit();
            gts << (sub + "/" + relName) << "\t" << texts.join(" ") << "\n";
        }
        gt.commit();
        return true;
    }

    if (format == "coco") {
        for (const QString& sub : {QString("train"), QString("val")}) {
            QJsonObject coco;
            coco["info"] = QJsonObject{{"description", "QDV Annotator export"}, {"version", "1.0"}};
            QJsonArray cats;
            for (int i = 0; i < labels.size(); ++i)
                cats.append(QJsonObject{{"id", i + 1}, {"name", labels[i]}, {"supercategory", "none"}});
            coco["categories"] = cats;
            QJsonArray cimages, canns;
            int imgId = 1, annId = 1;
            for (int i = 0; i < imgs.size(); ++i) {
                if (plan[i].second != sub) continue;
                const QVariantMap img = imgs[i].toMap();
                const int W = img.value("width").toInt();
                const int H = img.value("height").toInt();
                if (W == 0 || H == 0) continue;
                QJsonObject cim; cim["id"] = imgId;
                cim["file_name"] = QFileInfo(img.value("path").toString()).fileName();
                cim["width"] = W; cim["height"] = H;
                cimages.append(cim);
                for (const QVariant& a : img.value("annotations").toList()) {
                    const QVariantMap ann = a.toMap();
                    int lid = lidOf(ann);
                    if (lid < 0) continue;
                    QJsonObject cao; cao["id"] = annId; cao["image_id"] = imgId;
                    cao["category_id"] = lid + 1; cao["iscrowd"] = 0;
                    if (ann.value("shape").toString() == "polygon") {
                        QJsonArray seg;
                        for (const QVariant& p : ann.value("points").toList()) {
                            const QVariantMap pm = p.toMap();
                            seg.append(pm.value("x").toDouble()); seg.append(pm.value("y").toDouble());
                        }
                        cao["segmentation"] = QJsonArray{seg};
                        cao["bbox"] = QJsonArray{0,0,0,0};
                        cao["area"] = Qdv::polyArea(ann.value("points").toList());
                    } else if (ann.value("shape").toString() == "rect") {
                        int x = ann.value("x").toInt(), y = ann.value("y").toInt();
                        int w = ann.value("w").toInt(), h = ann.value("h").toInt();
                        cao["bbox"] = QJsonArray{x, y, w, h};
                        cao["area"] = w * h;
                        cao["segmentation"] = QJsonArray{QJsonArray{
                            x, y, x + w, y, x + w, y + h, x, y + h}};
                    } else continue;
                    canns.append(cao); ++annId;
                }
                ++imgId;
            }
            coco["images"] = cimages; coco["annotations"] = canns;
            if (!QDir().mkpath(absRoot + "/images/train") ||
                !QDir().mkpath(absRoot + "/images/val")  ||
                !QDir().mkpath(absRoot + "/annotations")) {
                setLastError("无法创建 coco 子目录: " + absRoot);
                return false;
            }
            QSaveFile f(absRoot + "/annotations/instances_" + sub + ".json");
            if (!f.open(QIODevice::WriteOnly)) {
                setLastError("无法写入 coco json: " + f.errorString());
                return false;
            }
            f.write(QJsonDocument(coco).toJson(QJsonDocument::Indented));
            f.commit();
            for (int i = 0; i < imgs.size(); ++i)
                if (plan[i].second == sub)
                    Qdv::copyImage(imgs[i].toMap().value("path").toString(),
                                   absRoot + "/images/" + sub, copyImages);
        }
        return true;
    }

    setLastError("未知导出格式: " + format);
    return false;
}

bool AnnotationSession::importCoco(const QString& cocoPath)
{
    QFile f(cocoPath);
    if (!f.open(QIODevice::ReadOnly)) { setLastError("无法打开: " + cocoPath); return false; }
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    if (!doc.isObject()) { setLastError("COCO 文件格式错误"); return false; }
    const QJsonObject root = doc.object();

    QMap<int, QString> catName;
    for (const QJsonValue& v : root.value("categories").toArray()) {
        const QJsonObject o = v.toObject();
        catName[o.value("id").toInt()] = o.value("name").toString();
    }
    QStringList labels = m_labels;
    QMap<int, int> catToLid;
    for (auto it = catName.begin(); it != catName.end(); ++it) {
        int lid = labels.indexOf(it.value());
        if (lid < 0) lid = addLabel(it.value());
        catToLid[it.key()] = lid;
    }

    QMap<int, QString> imgPath;
    for (const QJsonValue& v : root.value("images").toArray()) {
        const QJsonObject o = v.toObject();
        imgPath[o.value("id").toInt()] = o.value("file_name").toString();
    }

    QMap<int, QVariantList> byImage;
    for (const QJsonValue& v : root.value("annotations").toArray()) {
        const QJsonObject o = v.toObject();
        int iid = o.value("image_id").toInt();
        int lid = catToLid.value(o.value("category_id").toInt(), -1);
        if (lid < 0) continue;
        QVariantMap ann; ann["labelId"] = lid;
        const QJsonArray bbox = o.value("bbox").toArray();
        if (bbox.size() == 4) {
            ann["shape"] = "rect";
            ann["x"] = bbox[0].toInt(); ann["y"] = bbox[1].toInt();
            ann["w"] = bbox[2].toInt(); ann["h"] = bbox[3].toInt();
        }
        const QJsonArray seg = o.value("segmentation").toArray();
        if (!seg.isEmpty() && seg[0].isArray()) {
            const QJsonArray poly = seg[0].toArray();
            if (poly.size() >= 6) {
                ann["shape"] = "polygon";
                QVariantList pts;
                for (int k = 0; k + 1 < poly.size(); k += 2) {
                    QVariantMap pm; pm["x"] = poly[k].toInt(); pm["y"] = poly[k+1].toInt();
                    pts.append(pm);
                }
                ann["points"] = pts;
            }
        }
        byImage[iid].append(ann);
    }

    setTaskType("detection");
    for (const QJsonValue& v : root.value("images").toArray()) {
        const QJsonObject o = v.toObject();
        QString path = imgPath.value(o.value("id").toInt());
        if (path.isEmpty() || !QFile::exists(path)) continue;
        addImage(path);
        for (const QVariant& a : byImage.value(o.value("id").toInt()))
            addAnnotation(a.toMap());
    }
    return true;
}

// =====================================================================
// 预处理 / 弱边缘增强辅助标注
// =====================================================================
bool AnnotationSession::preprocessEnabled() const { return m_preprocessEnabled; }
void AnnotationSession::setPreprocessEnabled(bool v)
{
    if (v == m_preprocessEnabled) return;
    m_preprocessEnabled = v;
    emit preprocessEnabledChanged();
    updatePreprocess();
}

QString AnnotationSession::preprocessMode() const { return m_preprocessMode; }
void AnnotationSession::setPreprocessMode(const QString& m)
{
    if (m == m_preprocessMode) return;
    m_preprocessMode = m;
    emit preprocessModeChanged();
    updatePreprocess();
}

bool AnnotationSession::edgeOverlayEnabled() const { return m_edgeOverlayEnabled; }
void AnnotationSession::setEdgeOverlayEnabled(bool v)
{
    if (v == m_edgeOverlayEnabled) return;
    m_edgeOverlayEnabled = v;
    emit edgeOverlayEnabledChanged();
    updatePreprocess();
}

QString AnnotationSession::enhancedViewPath() const { return m_enhancedViewPath; }
QString AnnotationSession::edgeOverlayPath() const { return m_edgeOverlayPath; }
QVariantList AnnotationSession::candidateRegions() const { return m_candidateRegions; }
QVariantMap AnnotationSession::currentStats() const { return m_currentStats; }

// [视觉算法专家 SOP] 量化诊断当前原图：返回均值/标准差/边缘密度，并标记
// 低对比度 / 弱边缘。这是后续自动选择 CLAHE/unsharp 参数的依据。
QVariantMap AnnotationSession::analyzeCurrentImage() const
{
    QVariantMap empty;
    if (m_current < 0 || m_current >= m_images.size()) return empty;
    const QString path = m_images[m_current].toMap().value("path").toString();
    QImage src(path);
    if (src.isNull()) return empty;
    const Prep::ImageStats st = Prep::analyze(src);
    QVariantMap m;
    m["width"]      = st.width;
    m["height"]     = st.height;
    m["mean"]       = st.mean;
    m["stddev"]     = st.stddev;
    m["min"]        = st.minv;
    m["max"]        = st.maxv;
    m["edgeDensity"]= st.edgeDensity;
    m["lowContrast"]= st.lowContrast;
    m["weakEdge"]   = st.weakEdge;
    return m;
}

// 重新计算增强显示图 / 边缘叠加层 / 量化统计。所有输出与原图同尺寸，
// 保证标注坐标对齐不受影响。每次写入新的临时文件（带 UUID 文件名），
// 确保 QML 图像 URL 变化从而强制刷新。
void AnnotationSession::updatePreprocess()
{
    // 清理上一轮临时文件，避免 %TEMP% 堆积
    auto cleanup = [](QString& f) {
        if (!f.isEmpty()) { QFile(f).remove(); f.clear(); }
    };
    cleanup(m_lastEnhancedFile);
    cleanup(m_lastOverlayFile);
    m_enhancedViewPath.clear();
    m_edgeOverlayPath.clear();
    m_currentStats.clear();

    if (m_current < 0 || m_current >= m_images.size()) {
        emit enhancedViewPathChanged();
        emit edgeOverlayPathChanged();
        emit currentStatsChanged();
        return;
    }
    const QString path = m_images[m_current].toMap().value("path").toString();
    QImage src(path);
    if (src.isNull()) {
        emit enhancedViewPathChanged();
        emit edgeOverlayPathChanged();
        emit currentStatsChanged();
        return;
    }
    const QImage gray = Prep::toGray(src);

    // ---- 量化统计（视觉算法专家诊断依据）----
    const Prep::ImageStats st = Prep::analyze(src);
    QVariantMap statsMap;
    statsMap["width"]       = st.width;
    statsMap["height"]      = st.height;
    statsMap["mean"]        = st.mean;
    statsMap["stddev"]      = st.stddev;
    statsMap["min"]         = st.minv;
    statsMap["max"]         = st.maxv;
    statsMap["edgeDensity"] = st.edgeDensity;
    statsMap["lowContrast"] = st.lowContrast;
    statsMap["weakEdge"]    = st.weakEdge;
    m_currentStats = statsMap;

    QDir tmp(QDir::tempPath());
    tmp.mkpath("qdv_annotator");
    const QString dir = tmp.filePath("qdv_annotator");
    const QString uid = QUuid::createUuid().toString(QUuid::WithoutBraces);

    // ---- 增强显示图 ----
    if (m_preprocessEnabled) {
        QString mode = m_preprocessMode;
        if (mode == "auto")
            mode = (st.lowContrast || st.weakEdge) ? "clahe" : "unsharp";
        if (mode == "clahe") {
            QImage out = Prep::clahe(gray);
            const QString fp = dir + "/enh_" + uid + ".png";
            if (out.save(fp)) { m_enhancedViewPath = "file:///" + fp; m_lastEnhancedFile = fp; }
        } else if (mode == "unsharp") {
            QImage out = Prep::unsharp(gray);
            const QString fp = dir + "/enh_" + uid + ".png";
            if (out.save(fp)) { m_enhancedViewPath = "file:///" + fp; m_lastEnhancedFile = fp; }
        }
        // "none" / "edge" 不替换显示图，原图直接显示
    }

    // ---- 边缘叠加层（独立开关，叠加在原图之上）----
    if (m_edgeOverlayEnabled) {
        QImage ov = Prep::sobelOverlay(gray, QColor(0, 229, 255), 24);
        const QString fp = dir + "/ov_" + uid + ".png";
        if (ov.save(fp)) { m_edgeOverlayPath = "file:///" + fp; m_lastOverlayFile = fp; }
    }

    emit enhancedViewPathChanged();
    emit edgeOverlayPathChanged();
    emit currentStatsChanged();
}

// 在（增强后）图上计算候选框（连通域），存入 candidateRegions。
// 弱边缘增强辅助标注的核心：先用预处理把弱边界拉开，再二值化+闭运算+连通域。
void AnnotationSession::suggestRegionsForCurrent()
{
    m_candidateRegions.clear();
    if (m_current < 0 || m_current >= m_images.size()) { emit candidateRegionsChanged(); return; }
    const QString path = m_images[m_current].toMap().value("path").toString();
    QImage src(path);
    if (src.isNull()) { emit candidateRegionsChanged(); return; }

    // 若已开启增强显示，则在增强图上提候选（弱边界更清晰）；否则用原图
    QImage base = src;
    if (!m_enhancedViewPath.isEmpty()) {
        QImage enh(m_enhancedViewPath.mid(8)); // 去掉 "file:///" 前缀
        if (!enh.isNull()) base = enh;
    }
    m_candidateRegions = Prep::suggestRegions(base);
    emit candidateRegionsChanged();
}

// 将全部候选框作为矩形标注接受（labelId = 当前激活标签）
void AnnotationSession::acceptAllCandidates(int labelId)
{
    if (m_current < 0 || m_current >= m_images.size()) return;
    if (m_candidateRegions.isEmpty()) return;
    pushUndo("acceptAllCandidates");
    QVariantList added;
    for (const QVariant& v : m_candidateRegions) {
        const QVariantMap r = v.toMap();
        QVariantMap ann;
        ann["shape"]   = "rect";
        ann["labelId"] = labelId;
        ann["x"]       = r.value("x").toInt();
        ann["y"]       = r.value("y").toInt();
        ann["w"]       = r.value("w").toInt();
        ann["h"]       = r.value("h").toInt();
        added.append(ann);
    }
    QVariantMap img = m_images[m_current].toMap();
    QVariantList anns = img.value("annotations").toList();
    anns.append(added);
    img["annotations"] = anns;
    m_images[m_current] = img;
    m_candidateRegions.clear();
    emit candidateRegionsChanged();
    emit currentImageChanged();
    emit imagesChanged();
}

void AnnotationSession::clearCandidates()
{
    if (m_candidateRegions.isEmpty()) return;
    m_candidateRegions.clear();
    emit candidateRegionsChanged();
}

// =====================================================================
// 标训一键闭环
// =====================================================================
bool AnnotationSession::isTraining() const { return m_isTraining; }

bool AnnotationSession::checkPython(const QString& pythonPath) const
{
    QString err;
    return TrainingBridge::checkPython(pythonPath, err);
}

// 真实检测：ultralytics/torch/CUDA 可用性（推荐用此接口）
QVariantMap AnnotationSession::checkPythonEnv(const QString& pythonPath) const
{
    return TrainingBridge::checkPythonEnvironment(pythonPath);
}

// 导出 YOLO 数据集（dataset.yaml + images/ + labels/）后，直接拉起训练。
// 这是「标注 → 训练」一键闭环的核心入口。
bool AnnotationSession::hasVecRectAnnotations() const
{
    for (const QVariant& v : m_images)
        for (const QVariant& a : v.toMap().value("annotations").toList())
            if (a.toMap().value("shape").toString() == "vec_rect") return true;
    return false;
}

bool AnnotationSession::exportAndTrain(const QString& outDir, const QVariantMap& options)
{
    if (m_isTraining) { setLastError("训练正在进行中，请先取消"); return false; }
    if (m_labels.isEmpty()) { setLastError("请先添加至少一个标签"); return false; }

    // 1) 导出数据集（yolo_detect 已生成 dataset.yaml / images / labels，
    //    完全对齐 training/yolo_train.py 的 consumer 约定）
    if (!exportDataset("yolo_detect", outDir, QVariantMap{{"copyImages", true}})) {
        // lastError 已由 exportDataset 设置
        return false;
    }
    const QString dataYaml = QDir(outDir).absoluteFilePath("dataset.yaml");
    if (!QFile::exists(dataYaml)) {
        setLastError("导出后未找到 dataset.yaml: " + dataYaml);
        return false;
    }

    // 2) 拉起训练（QProcess 异步，UI 不阻塞 —— 即「训练线程」）
    QVariantMap opt = options;
    if (!opt.contains("pythonPath")) opt["pythonPath"] = "python";
    if (!opt.contains("modelType"))  opt["modelType"]  = "yolov8n";
    if (!opt.contains("numEpochs"))  opt["numEpochs"]  = 50;
    if (!opt.contains("batchSize"))  opt["batchSize"]  = 16;
    if (!opt.contains("imageSize"))  opt["imageSize"]  = 640;
    if (!opt.contains("learningRate")) opt["learningRate"] = 0.01;

    m_isTraining = true;
    emit isTrainingChanged();
    const bool ok = m_trainer->startTraining(dataYaml, outDir, opt);
    if (!ok) {
        m_isTraining = false;
        emit isTrainingChanged();
        return false;
    }
    return true;
}

void AnnotationSession::cancelTraining()
{
    if (m_trainer) m_trainer->cancelTraining();
    if (m_isTraining) { m_isTraining = false; emit isTrainingChanged(); }
}

// 使用已导出的 yolo_obb 数据集直接训练：跳过 exportDataset，仅拉起 training/yolo_train.py。
// 供「标训闭环 → 使用已导出的 OBB 数据集」入口调用，与 exportAndTrain(yolo_detect) 完全隔离。
bool AnnotationSession::trainFromDataYaml(const QString& dataYaml,
                                          const QString& outDir,
                                          const QVariantMap& options)
{
    if (m_isTraining) { setLastError("训练正在进行中，请先取消"); return false; }

    // 容错：允许传入"导出目录"或"导出目录/dataset.yaml"两种形式。
    // 传入目录时自动补全 dataset.yaml（与 exportDataset 生成的产物名一致）。
    QString yaml = dataYaml.trimmed();
    if (yaml.isEmpty()) { setLastError("未指定已导出的数据集路径"); return false; }
    if (QFileInfo(yaml).isDir()) {
        const QString candidate = QDir(yaml).absoluteFilePath("dataset.yaml");
        if (QFile::exists(candidate)) yaml = candidate;
    }
    if (yaml.isEmpty() || !QFile::exists(yaml)) {
        setLastError("dataset.yaml 不存在: " + yaml
                     + "\n请选择 yolo_obb 导出目录，或该目录下的 dataset.yaml 文件");
        return false;
    }
    if (QFileInfo(yaml).isDir()) {
        setLastError("dataset.yaml 解析失败: 所选路径为目录且未找到 dataset.yaml: " + yaml);
        return false;
    }

    QVariantMap opt = options;
    if (!opt.contains("pythonPath")) opt["pythonPath"] = "python";
    if (!opt.contains("modelType"))  opt["modelType"]  = "yolov8n-obb"; // OBB 默认权重
    if (!opt.contains("numEpochs"))  opt["numEpochs"]  = 50;
    if (!opt.contains("batchSize"))  opt["batchSize"]  = 16;
    if (!opt.contains("imageSize"))  opt["imageSize"]  = 640;
    if (!opt.contains("learningRate")) opt["learningRate"] = 0.01;

    m_isTraining = true;
    emit isTrainingChanged();
    const bool ok = m_trainer->startTraining(dataYaml, outDir, opt);
    if (!ok) {
        m_isTraining = false;
        emit isTrainingChanged();
        return false;
    }
    return true;
}
