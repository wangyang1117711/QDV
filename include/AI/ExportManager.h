#ifndef EXPORTMANAGER_H
#define EXPORTMANAGER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QList>
#include <QJsonObject>
#include <QJsonArray>
#include <QSize>
#include <QPair>
#include <QPointF>

/**
 * @brief 数据集导出管理器（单例）
 *
 * 支持 YOLO 和 COCO 两种导出格式：
 * - YOLO: images/{train,val} + labels/{train,val} + data.yaml
 * - COCO: images/ + annotations.json（含 bbox/polygon）
 *
 * 85:15 等距轮转采样划分训练/验证集
 *
 * 注意：置于 QDV 命名空间下，避免与 TrainingInference 模块的同名
 * ExportManager（CSV/JSON 推理结果导出）发生链接冲突。
 */

namespace QDV {

// 标注类型
enum class AnnotationType {
    BBox = 0,      // 矩形框 [x, y, w, h]
    Polygon = 1    // 多边形 [x1,y1,x2,y2,...]
};

// 单个标注
struct Annotation {
    int classId = 0;
    AnnotationType type = AnnotationType::BBox;
    QList<double> bbox;      // [x, y, w, h] 绝对像素坐标（BBox 类型）
    QList<QPointF> polygon;  // 多边形顶点（Polygon 类型）
};

// 单个图像条目
struct ExportImageEntry {
    QString path;              // 图像文件路径
    QList<Annotation> annotations;  // 标注列表（空=背景样本）
};

// 导出结果
struct ExportResult {
    bool success = false;
    QString outputDir;
    QString dataYamlPath;      // YOLO: data.yaml 路径
    QString annotationsPath;   // COCO: annotations.json 路径
    int trainCount = 0;
    int valCount = 0;
    int totalCount = 0;
    QString errorMessage;
};

class ExportManager : public QObject {
    Q_OBJECT

public:
    static ExportManager* instance();

    /**
     * @brief 导出为 YOLO 格式
     * @param entries 图像条目列表
     * @param classNames 类别名列表
     * @param outputDir 输出目录
     * @param validationSplit 验证集比例（默认 0.15）
     * @return 导出结果
     */
    ExportResult exportYOLO(const QList<ExportImageEntry>& entries,
                             const QStringList& classNames,
                             const QString& outputDir,
                             double validationSplit = 0.15);

    /**
     * @brief 导出为 COCO 格式
     * @param entries 图像条目列表
     * @param classNames 类别名列表
     * @param outputDir 输出目录
     * @param validationSplit 验证集比例
     * @return 导出结果
     */
    ExportResult exportCOCO(const QList<ExportImageEntry>& entries,
                             const QStringList& classNames,
                             const QString& outputDir,
                             double validationSplit = 0.15);

signals:
    void exportProgress(int current, int total, const QString& message);
    void exportCompleted(const ExportResult& result);

private:
    explicit ExportManager(QObject* parent = nullptr);
    ~ExportManager();

    static ExportManager* s_instance;

    // 85:15 等距轮转采样
    QPair<QList<int>, QList<int>> splitTrainVal(int totalCount, double valRatio) const;

    // 读取图像尺寸（QFile + cv::imdecode 兼容中文路径）
    QSize readImageSize(const QString& path) const;

    // 复制图像到目标目录
    bool copyImage(const QString& src, const QString& dst) const;

    // YOLO 标签文件生成
    bool writeYoloLabel(const QString& labelPath, const QList<Annotation>& annotations,
                        const QSize& imageSize) const;

    // COCO 标注 JSON 生成
    QJsonObject buildCocoAnnotation(const QList<ExportImageEntry>& entries,
                                     const QStringList& classNames,
                                     const QList<int>& trainIndices,
                                     const QList<int>& valIndices) const;

    // data.yaml 生成
    bool writeDataYaml(const QString& path, const QString& rootDir,
                       const QString& trainRelPath, const QString& valRelPath,
                       int numClasses, const QStringList& classNames) const;
};

} // namespace QDV

#endif // EXPORTMANAGER_H
