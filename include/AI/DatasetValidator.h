#ifndef DATASETVALIDATOR_H
#define DATASETVALIDATOR_H

#include <QObject>
#include <QString>
#include <QList>
#include <QJsonObject>
#include <QJsonArray>
#include <QSize>

/**
 * @brief 数据集四维校验框架
 *
 * 四个维度：
 * 1. Pairing（配对完整性）：文件存在/无标注/类别引用
 * 2. Format（格式坐标）：尺寸/bbox 越界/面积/占比/polygon 顶点
 * 3. Compatibility（模型兼容）：模型存在/尺寸匹配/类别数匹配
 * 4. Distribution（分布统计）：类别无样本/样本<10/不平衡/尺寸变异
 *
 * 健康评分：满分 100，Critical -30 / Error -10 / Warning -2
 * 训练门禁：isHealthy() 无 Critical/Error；canTrain() 无 Critical 且 Error<=5
 */

// 严重级别
enum class Severity {
    Info = 0,
    Warning = 1,
    Error = 2,
    Critical = 3
};

// 校验维度
enum class ValidationCategory {
    Pairing = 0,
    Format = 1,
    Compatibility = 2,
    Distribution = 3
};

// 单个校验项
struct ValidationItem {
    Severity severity = Severity::Info;
    ValidationCategory category = ValidationCategory::Pairing;
    QString title;          // 问题标题
    QString description;    // 详细描述
    QString suggestion;     // 修复建议
    QString affectedFile;   // 受影响的文件（可选）
    int affectedCount = 0;  // 受影响的数量
};

// 校验请求
struct ValidationRequest {
    QString datasetPath;            // 数据集根目录
    QStringList imagePaths;         // 所有图像路径
    QStringList labelPaths;         // 所有标签路径
    QStringList expectedLabels;     // 期望的类别列表
    QSize expectedInputSize;        // 期望的模型输入尺寸
    int expectedNumClasses = 0;     // 期望的类别数
    QString modelPath;              // 模型路径（Compatibility 维度用）
};

// 校验报告
struct ValidationReport {
    QList<ValidationItem> items;    // 所有校验项
    int score = 100;                // 健康评分（0-100）
    int criticalCount = 0;
    int errorCount = 0;
    int warningCount = 0;
    int infoCount = 0;

    // 按维度分组
    QList<ValidationItem> pairingItems() const;
    QList<ValidationItem> formatItems() const;
    QList<ValidationItem> compatibilityItems() const;
    QList<ValidationItem> distributionItems() const;

    // 门禁检查
    bool isHealthy() const;   // 无 Critical/Error
    bool canTrain() const;    // 无 Critical 且 Error<=5

    // 转 JSON
    QJsonObject toJson() const;
};

class DatasetValidator : public QObject {
    Q_OBJECT

public:
    explicit DatasetValidator(QObject* parent = nullptr);
    ~DatasetValidator();

    /**
     * @brief 执行四维校验
     * @param request 校验请求
     * @return 校验报告
     */
    ValidationReport validate(const ValidationRequest& request);

signals:
    void validationProgress(int current, int total, const QString& message);
    void validationCompleted(const ValidationReport& report);

private:
    // 四维校验
    void validatePairing(const ValidationRequest& request, ValidationReport& report);
    void validateFormat(const ValidationRequest& request, ValidationReport& report);
    void validateCompatibility(const ValidationRequest& request, ValidationReport& report);
    void validateDistribution(const ValidationRequest& request, ValidationReport& report);

    // 评分计算
    void calculateScore(ValidationReport& report);

    // 辅助
    bool fileExists(const QString& path) const;
    QSize readImageSize(const QString& path) const;
    QStringList parseLabelFile(const QString& path) const;
};

#endif // DATASETVALIDATOR_H
