#ifndef QDV_DATA_COLLECTION_WIZARD_H
#define QDV_DATA_COLLECTION_WIZARD_H

// ============================================================================
// DataCollectionWizard — 数据收集向导（spec v2 阶段五 Task 13）
//
// 用途：第二层 训练兑底流程的数据准备阶段。
//   用户在 ZeroShotPanel 选中某类别（如 "rust"）后点击"训练专用模型..."按钮，
//   本向导启动，引导用户完成：
//     1) 选择目标类别（自动从入口填充）+ 输出目录（默认 D:\QDV\datasets\<类别名>）
//     2) 选择图像目录 → 加载图像列表
//     3) 矩形框标注（QGraphicsScene + 鼠标拖拽画框，每张图可画多个框）
//     4) 导出 YOLO 格式数据集（images/train,images/val,labels/train,labels/val,data.yaml）
//   完成后通过 datasetExported 信号通知外部启动训练（TrainingBridge::startTraining）
//
// 存储遵守：数据集默认放 D 盘（D:\QDV\datasets\<类别名>），符合 AGENTS.md 存储要求
// ============================================================================

#include <QWizard>
#include <QString>
#include <QStringList>
#include <QList>
#include <QVariantMap>
#include <QPair>
#include <QHash>
#include <QRectF>
#include <QEvent>

class QWizardPage;
class QLineEdit;
class QPushButton;
class QLabel;
class QListWidget;
class QListWidgetItem;
class QSpinBox;
class QDoubleSpinBox;
class QGraphicsView;
class QGraphicsScene;
class QGraphicsPixmapItem;
class QGraphicsRectItem;
class QComboBox;

namespace QDV {

/// 数据收集向导（4 页：类别/输出目录 → 图像选择 → 标注 → 导出）
class DataCollectionWizard : public QWizard {
    Q_OBJECT
public:
    explicit DataCollectionWizard(QWidget* parent = nullptr);
    ~DataCollectionWizard() override;

    /// 设置初始类别信息（由 ZeroShotDetectView 从 PromptLibrary entry 注入）
    /// @param entry  QVariantMap: {name, prompt, cnName, scene, hasSpecializedModel, specializedModelPath}
    void setTargetEntry(const QVariantMap& entry);

    /// 训练参数（向导完成后由外部读取，传给 TrainingBridge::startTraining）
    struct TrainParams {
        QString datasetManifestPath;   // 数据清单 manifest 路径（YOLO 格式）
        QString datasetRootDir;        // 数据集根目录
        QString outputDir;             // 训练输出目录
        QString modelType;             // 模型类型（"yolov8n" 等）
        int     numEpochs   = 30;
        int     batchSize   = 8;
        double  learningRate = 0.001;
        double  valSplit    = 0.2;
        QString className;             // 类别名（与 prompt 对应）
        QStringList allLabels;         // 所有类别标签（单类训练时仅 1 项）
    };

    /// 向导完成后获取训练参数（外部读取后调 TrainingBridge）
    TrainParams trainParams() const { return m_trainParams; }

    /// 导出 YOLO 格式数据集（由外部在向导 Accepted 后调用）
    /// @param errOut  失败时写入错误原因
    /// @return true=导出成功，m_trainParams 已填充；false=导出失败，errOut 含原因
    bool exportDataset(QString* errOut);

    /// 检查是否有任何图像被标注（外部用于启用/禁用 Finish 按钮）
    bool hasAnyAnnotation() const;

signals:
    /// 数据集导出完成信号（外部调用 exportDataset 成功后发射）
    /// @param params  训练参数（含数据集路径与训练超参）
    void datasetExported(const QDV::DataCollectionWizard::TrainParams& params);

protected:
    /// 事件过滤器：拦截 QGraphicsScene 的鼠标事件实现拖拽画框
    bool eventFilter(QObject* obj, QEvent* event) override;

private slots:
    // Step 1: 类别 & 输出目录
    void onBrowseOutputDir();

    // Step 2: 图像选择
    void onBrowseImageDir();
    void onRefreshImageList();

    // Step 3: 标注
    // 注：鼠标拖拽画框逻辑统一在 eventFilter 中处理，无需独立的 mouse slot
    void onImageSelectionChanged();         // 切换当前显示的图像
    void onDeleteSelectedBox();             // 删除当前选中框
    void onClearCurrentImageBoxes();        // 清空当前图所有框
    void onPrevImage();
    void onNextImage();

    // Step 4: 导出
    void onBrowseTrainOutputDir();

private:
    void setupPage1ClassSelect();
    void setupPage2ImageSelect();
    void setupPage3Annotate();
    void setupPage4Export();

    // 加载图像到 QGraphicsScene
    void loadCurrentImage(int index);
    // 在当前图像上绘制已有的标注框
    void redrawBoxesForCurrent(int index);

    // --- Step 1 控件 ---
    QWizardPage*   m_page1 = nullptr;
    QLineEdit*     m_classNameEdit   = nullptr;  // 内部名（如 rust）
    QLineEdit*     m_classPromptEdit = nullptr;  // 英文提示词
    QLineEdit*     m_cnNameEdit      = nullptr;  // 中文名
    QLineEdit*     m_outputDirEdit   = nullptr;  // 数据集输出目录

    // --- Step 2 控件 ---
    QWizardPage*   m_page2 = nullptr;
    QLineEdit*     m_imageDirEdit = nullptr;
    QPushButton*   m_browseImageBtn = nullptr;
    QListWidget*   m_imageListWidget = nullptr;
    QLabel*        m_imageCountLabel = nullptr;

    // --- Step 3 控件 ---
    QWizardPage*   m_page3 = nullptr;
    QGraphicsView*   m_graphicsView = nullptr;
    QGraphicsScene*  m_scene        = nullptr;
    QGraphicsPixmapItem* m_pixmapItem = nullptr;
    QLabel*          m_annotStatusLabel = nullptr;
    QPushButton*     m_prevImgBtn   = nullptr;
    QPushButton*     m_nextImgBtn   = nullptr;
    QPushButton*     m_delBoxBtn    = nullptr;
    QPushButton*     m_clearBoxesBtn = nullptr;
    QLabel*          m_imgPosLabel  = nullptr;  // "3 / 25"

    // --- Step 4 控件 ---
    QWizardPage*   m_page4 = nullptr;
    QLineEdit*     m_trainOutputDirEdit = nullptr;
    QComboBox*     m_modelTypeCombo = nullptr;
    QSpinBox*      m_epochSpin      = nullptr;
    QSpinBox*      m_batchSpin      = nullptr;
    QDoubleSpinBox* m_lrSpin        = nullptr;
    QDoubleSpinBox* m_valSplitSpin  = nullptr;
    QLabel*        m_exportHintLabel = nullptr;

    // --- 数据 ---
    QVariantMap m_entry;          // 入口注入的类别信息
    QStringList m_imagePaths;     // 图像绝对路径列表

    // 标注数据：图像路径 → 该图的标注框列表（像素坐标）
    // 每个框 {x, y, w, h}（QRectF），classId 固定为 0（单类训练）
    QHash<QString, QList<QRectF>> m_annotations;

    // 当前正在绘制的框（鼠标按下到释放之间）
    QGraphicsRectItem* m_currentDrawingRect = nullptr;
    QPointF            m_drawStartPos;

    int m_currentImageIndex = -1;

    TrainParams m_trainParams;  // 完成后供外部读取
};

} // namespace QDV

#endif // QDV_DATA_COLLECTION_WIZARD_H
