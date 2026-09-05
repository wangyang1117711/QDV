// =====================================================================
// AnnotationSession.h — QDV 数据标注工具 后端会话控制器
//
// 设计目标：
// - 与 QDV 项目无缝集成：标签消费格式对齐 operators.json 的
//   categoryLabels（字符串数组），模型注册格式对齐 models/labels.json
//   的 {"labels":[...]}；坐标统一使用【图像像素坐标】存储，避免任何
//   显示缩放带来的坐标漂移。
// - 任务类型覆盖现有 6 个 AI 算子：
//     classification  -> AiClassify
//     detection       -> YoloDetect / DetectObjectsDl / ZeroShotDetect
//     segmentation    -> SegmentDl
//     ocr             -> DLOCR / Ocr
//     keypoint        -> (扩展位，预留给未来姿态/关键点算子)
//
// 标注项（annotation）统一用 QVariantMap 表达，shape 字段区分几何形态：
//   rect      : {shape, labelId, x,y,w,h}                  (检测/文字框)
//   polygon   : {shape, labelId, points:[{x,y},...]}       (分割)
//   point     : {shape, labelId, x,y}                      (关键点)
//   text      : {shape, labelId, x,y,w,h, text}            (OCR 转写)
//   class     : (分类任务不使用几何，而是图像级 classLabels:[id,...])
// 坐标为整数图像像素；labelId 为 labels 数组下标。
// =====================================================================
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QJsonObject>
#include <QSize>
#include <QVector>

class TrainingBridge; // 标训闭环训练桥（src/TrainingBridge.h）

class AnnotationSession : public QObject
{
    Q_OBJECT
    // ---- 暴露给 QML 的属性 ----
    Q_PROPERTY(QString taskType READ taskType WRITE setTaskType NOTIFY taskTypeChanged)
    Q_PROPERTY(QStringList labels READ labels NOTIFY labelsChanged)
    Q_PROPERTY(QStringList labelColors READ labelColors NOTIFY labelsChanged)
    Q_PROPERTY(QVariantList images READ images NOTIFY imagesChanged)
    Q_PROPERTY(int currentImageIndex READ currentImageIndex WRITE setCurrentImageIndex NOTIFY currentImageIndexChanged)
    Q_PROPERTY(QVariantMap currentImage READ currentImage NOTIFY currentImageChanged)
    Q_PROPERTY(QString projectPath READ projectPath NOTIFY projectPathChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(int annotatedCount READ annotatedCount NOTIFY imagesChanged)

    // ---- 预处理 / 弱边缘增强辅助标注 ----
    Q_PROPERTY(bool preprocessEnabled READ preprocessEnabled WRITE setPreprocessEnabled NOTIFY preprocessEnabledChanged)
    Q_PROPERTY(QString preprocessMode READ preprocessMode WRITE setPreprocessMode NOTIFY preprocessModeChanged)
    Q_PROPERTY(bool edgeOverlayEnabled READ edgeOverlayEnabled WRITE setEdgeOverlayEnabled NOTIFY edgeOverlayEnabledChanged)
    Q_PROPERTY(QString enhancedViewPath READ enhancedViewPath NOTIFY enhancedViewPathChanged)
    Q_PROPERTY(QString edgeOverlayPath READ edgeOverlayPath NOTIFY edgeOverlayPathChanged)
    Q_PROPERTY(QVariantList candidateRegions READ candidateRegions NOTIFY candidateRegionsChanged)
    Q_PROPERTY(QVariantMap currentStats READ currentStats NOTIFY currentStatsChanged)

    // ---- 标训一键闭环（训练状态）----
    Q_PROPERTY(bool isTraining READ isTraining NOTIFY isTrainingChanged)

    // ---- 撤销/重做 + 未保存提示 ----
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY undoRedoChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY undoRedoChanged)
    Q_PROPERTY(bool dirty READ dirty NOTIFY dirtyChanged)

    // ---- 当前图选中标注索引（画布/列表双向联动真源，切图重置为 -1）----
    Q_PROPERTY(int selectedAnnotationIndex READ selectedAnnotationIndex WRITE setSelectedAnnotationIndex NOTIFY selectedAnnotationIndexChanged)

public:
    explicit AnnotationSession(QObject* parent = nullptr);

    // ---- 任务类型 ----
    enum class Task { Classification, Detection, Segmentation, Ocr, Keypoint };
    Q_ENUM(Task)
    static Task taskFromName(const QString& s);
    static QString taskToName(Task t);

    QString taskType() const;
    void setTaskType(const QString& t);

    // ---- 标签管理 ----
    QStringList labels() const;
    QStringList labelColors() const;
    Q_INVOKABLE int addLabel(const QString& name, const QString& color = QString());
    Q_INVOKABLE bool renameLabel(int id, const QString& name);
    Q_INVOKABLE bool removeLabel(int id);
    Q_INVOKABLE QString colorForLabel(int id) const;

    // ---- 数据集导入 ----
    Q_INVOKABLE bool loadImageFolder(const QString& folder,
                                     const QStringList& filters = QStringList());
    Q_INVOKABLE bool addImage(const QString& path);

    // ---- 标注读写（坐标为图像像素）----
    Q_INVOKABLE void addAnnotation(const QVariantMap& ann);
    Q_INVOKABLE void updateAnnotation(int index, const QVariantMap& ann);
    Q_INVOKABLE void removeAnnotation(int index);
    Q_INVOKABLE QVariantList currentAnnotations() const;

    // 分类任务：图像级标签
    Q_INVOKABLE void setImageClassLabels(const QVariantList& labelIds);
    Q_INVOKABLE QVariantList imageClassLabels() const;

    // 单图子集划分（train/val，用于导出切分）
    Q_INVOKABLE void setImageSubset(int imageIndex, const QString& subset);

    // ---- 撤销/重做 + 未保存提示 ----
    bool canUndo() const;
    bool canRedo() const;
    bool dirty() const;
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE void clearDirty();   // 丢弃未保存更改标记（“不保存”退出时调用）

    // ---- 当前图选中标注索引 ----
    int selectedAnnotationIndex() const;
    void setSelectedAnnotationIndex(int i);

    // ---- 通用属性访问 ----
    QVariantList images() const;
    int currentImageIndex() const;
    void setCurrentImageIndex(int i);
    QVariantMap currentImage() const;
    QString projectPath() const;
    QString lastError() const;
    int annotatedCount() const;

    // ---- 导出 / 保存 / 打开 ----
    // format: "yolo_detect" | "yolo_seg" | "classification" | "ocr" | "coco" | "qdvann"
    // options: {valRatio, seed, copyImages(bool), withMasks(bool), subsetMode("auto"|"flag"),
    //           trainDir, valDir, imageFormat("png")}
    Q_INVOKABLE bool exportDataset(const QString& format,
                                   const QString& outDir,
                                   const QVariantMap& options = QVariantMap());
    Q_INVOKABLE bool saveProject(const QString& path);
    Q_INVOKABLE bool openProject(const QString& path);
    // 从 Label Studio / CVAT 导出的 COCO 导入
    Q_INVOKABLE bool importCoco(const QString& cocoPath);

    // ---- 统计 ----
    Q_INVOKABLE QVariantMap statistics() const;

    // ---- 预处理 / 弱边缘增强辅助标注 ----
    bool preprocessEnabled() const;
    void setPreprocessEnabled(bool v);
    QString preprocessMode() const;          // "auto" | "none" | "clahe" | "unsharp" | "edge"
    void setPreprocessMode(const QString& m);
    bool edgeOverlayEnabled() const;
    void setEdgeOverlayEnabled(bool v);
    QString enhancedViewPath() const;        // 增强图临时 PNG（file://），未启用则为空
    QString edgeOverlayPath() const;         // 边缘叠加层临时 PNG（file://）
    QVariantList candidateRegions() const;   // 当前图候选框 {x,y,w,h,score}
    QVariantMap currentStats() const;         // 量化统计（均值/标准差/边缘密度…）

    // 当前原图量化诊断（视觉算法专家依据）
    Q_INVOKABLE QVariantMap analyzeCurrentImage() const;
    // 基于（增强后）图计算候选框，存入 candidateRegions
    Q_INVOKABLE void suggestRegionsForCurrent();
    // 将全部候选框作为标注接受（labelId=激活标签）
    Q_INVOKABLE void acceptAllCandidates(int labelId);
    Q_INVOKABLE void clearCandidates();

    // ---- 标训一键闭环 ----
    bool isTraining() const;
    // 当前工程是否含"有向矩形(vec_rect)"标注（用于标训闭环前的提示分流：
    // 轴对齐 yolo_detect 无法表达角度，需引导改用 yolo_obb 导出）
    Q_INVOKABLE bool hasVecRectAnnotations() const;
    // 导出 YOLO 数据集并拉起 training/yolo_train.py 训练
    //   outDir  : 数据集 + 模型输出目录（会写入 dataset.yaml / images / labels / 模型）
    //   options : {pythonPath, modelType, numEpochs, batchSize, imageSize, learningRate}
    // 返回 true 表示已成功拉起训练；false 表示前置校验失败（见 lastError）
    Q_INVOKABLE bool exportAndTrain(const QString& outDir, const QVariantMap& options = QVariantMap());
    // 使用已导出的 yolo_obb 数据集直接训练（跳过导出；不触碰 exportAndTrain 的 yolo_detect 链路）
    //   dataYaml: 已导出数据集的 dataset.yaml 绝对路径
    //   outDir   : 模型输出目录
    //   options  : 同 exportAndTrain
    Q_INVOKABLE bool trainFromDataYaml(const QString& dataYaml,
                                       const QString& outDir,
                                       const QVariantMap& options = QVariantMap());
    // 取消正在进行的训练
    Q_INVOKABLE void cancelTraining();
    // 检查 Python 环境是否可用（用于启动前预检，返回 {ok, detail, version, cuda}）
    Q_INVOKABLE QVariantMap checkPythonEnv(const QString& pythonPath) const;
    // 兼容旧接口：仅检查 --version
    Q_INVOKABLE bool checkPython(const QString& pythonPath) const;

signals:
    void taskTypeChanged();
    void labelsChanged();
    void imagesChanged();
    void currentImageIndexChanged();
    void currentImageChanged();
    void projectPathChanged();
    void lastErrorChanged();

    // ---- 预处理 / 弱边缘增强辅助标注 ----
    void preprocessEnabledChanged();
    void preprocessModeChanged();
    void edgeOverlayEnabledChanged();
    void enhancedViewPathChanged();
    void edgeOverlayPathChanged();
    void candidateRegionsChanged();
    void currentStatsChanged();

    // ---- 撤销/重做 + 未保存提示 ----
    void undoRedoChanged();
    void dirtyChanged();

    // ---- 当前图选中标注索引 ----
    void selectedAnnotationIndexChanged();

    // ---- 标训一键闭环 ----
    void isTrainingChanged();
    void trainProgress(const QVariantMap& progress);
    void trainCompleted(const QVariantMap& result);
    void trainError(const QString& phase, const QString& message);
    void trainLog(const QString& message);

private:
    void setLastError(const QString& e);
    QVariantMap& imageRef(int i);
    const QVariantMap& imageConst(int i) const;

    // ---- 撤销/重做：全量状态快照（QVariantList/QStringList 隐式共享，开销小）----
    struct StateSnapshot {
        QString op;                 // 操作名（诊断/日志用）
        QVariantList images;        // 变更前 m_images 快照
        QStringList labels;         // 变更前 m_labels
        QStringList labelColors;    // 变更前 m_labelColors
        int currentIndex = -1;      // 变更前 m_current
    };
    void pushUndo(const QString& op);         // 变更前调用：快照入 undo 栈 + 清 redo + 置脏
    void applyState(const StateSnapshot& s);  // 恢复快照并全量发信号
    void setDirty(bool d);
    void resetHistory();                      // 载入新数据集/打开工程：清空历史 + 清脏
    QVector<StateSnapshot> m_undoStack;
    QVector<StateSnapshot> m_redoStack;
    bool m_dirty = false;
    int m_selectedAnnotation = -1;

    void updatePreprocess(); // 重算增强图/边缘叠加/统计并发信号

    QString m_taskType = "detection";
    QStringList m_labels;
    QStringList m_labelColors;
    QVariantList m_images;          // 每项为 QVariantMap
    int m_current = -1;
    QString m_projectPath;
    QString m_lastError;

    // ---- 预处理 / 弱边缘增强辅助标注 ----
    bool m_preprocessEnabled = false;
    QString m_preprocessMode = "auto";   // auto | none | clahe | unsharp | edge
    bool m_edgeOverlayEnabled = false;
    QString m_enhancedViewPath;          // file:///... 增强图临时 PNG
    QString m_edgeOverlayPath;           // file:///... 边缘叠加层临时 PNG
    QVariantList m_candidateRegions;     // 当前图候选框 {x,y,w,h,score}
    QVariantMap m_currentStats;          // 量化统计
    QString m_lastEnhancedFile;          // 上一增强图临时文件（用于清理）
    QString m_lastOverlayFile;           // 上一叠加层临时文件（用于清理）

    // ---- 标训一键闭环 ----
    TrainingBridge* m_trainer = nullptr;
    bool m_isTraining = false;
};
