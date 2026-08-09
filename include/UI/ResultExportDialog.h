#ifndef RESULT_EXPORT_DIALOG_H
#define RESULT_EXPORT_DIALOG_H

// ============================================================================
// ResultExportDialog — 结果导出对话框（spec v2 阶段六 Task 14）
// 支持三种导出格式：
//   1) JSON        —— 含 imagePath/roi/detections/anomalyScore/confidence/elapsedMs/modelName/timestamp
//   2) CSV         —— 表头固定，每个检测框一行
//   3) 图片叠加标注 —— 在原图上绘制检测框 + 标签 + 置信度 + ROI，保存为 PNG/JPG
// 默认输出路径建议 D 盘（遵守 AGENTS.md 存储要求）
// ============================================================================

#include <QDialog>
#include <QJsonObject>
#include <QVariantMap>
#include <QString>
#include <QTemporaryDir>

#include <opencv2/core/mat.hpp>

class QRadioButton;
class QButtonGroup;
class QLineEdit;
class QPushButton;
class QComboBox;
class QLabel;

class ResultExportDialog : public QDialog {
    Q_OBJECT
public:
    explicit ResultExportDialog(QWidget* parent = nullptr);
    ~ResultExportDialog() override = default;

    /// 导入待导出的结果数据
    /// @param results  检测结果 JSON（含 detections 数组、anomalyScore、confidence 等）
    /// @param image    原图（用于图片叠加标注；为空时仅导出 JSON/CSV）
    /// @param metadata 元数据：imagePath/roi/elapsedMs/modelName/timestamp 等
    void setResults(const QJsonObject& results, const cv::Mat& image,
                    const QVariantMap& metadata);

private slots:
    void onBrowsePath();        // 浏览输出目录
    void onFormatChanged(int);  // 格式切换：图片格式时启用图片选项
    void onExport();            // 执行导出

private:
    void setupUI();
    void setDefaultOutputPath();  // 默认 D 盘导出目录

    // --- 三种导出实现 ---
    bool exportJson(const QString& filePath, QString* err);
    bool exportCsv(const QString& filePath, QString* err);
    bool exportAnnotatedImage(const QString& filePath, QString* err);

    // --- 工具 ---
    // 在原图上绘制检测框 + 标签 + 置信度 + ROI
    void drawAnnotations(cv::Mat& canvas) const;
    // 拼接默认文件名（含时间戳，避免覆盖）
    QString defaultFileName(const QString& suffix) const;
    // 从 m_results / m_metadata 提取检测项列表
    // 每项含 cx/cy/w/h/confidence/classId/className（与 ZeroShotDetectTool 输出一致）
    QVariantList extractDetections() const;
    // CSV 字段转义：含逗号/引号/换行时用双引号包裹，内部引号翻倍
    QString escapeCsv(const QString& s) const;

    // --- UI 控件 ---
    QRadioButton* m_jsonRadio    = nullptr;  // JSON 格式
    QRadioButton* m_csvRadio     = nullptr;  // CSV 格式
    QRadioButton* m_imageRadio   = nullptr;  // 图片叠加标注
    QButtonGroup* m_formatGroup  = nullptr;

    QLineEdit*    m_pathEdit     = nullptr;  // 输出路径（目录或文件）
    QPushButton*  m_browseBtn    = nullptr;  // 浏览按钮
    QComboBox*    m_imageFormatCombo = nullptr;  // 图片格式：PNG/JPG
    QLabel*       m_imageFormatLabel = nullptr;
    QLabel*       m_infoLabel    = nullptr;  // 顶部摘要（图像/检测框数量）

    // --- 数据 ---
    QJsonObject   m_results;     // 检测结果 JSON
    cv::Mat       m_image;       // 原图
    QVariantMap   m_metadata;    // 元数据（imagePath/roi/elapsedMs/modelName/timestamp）

    // 临时目录：用于保存导出过程中产生的中间文件（如需要）
    QTemporaryDir m_tempDir;
};

#endif // RESULT_EXPORT_DIALOG_H
