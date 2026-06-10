#ifndef CLASSIFICATION_METRICS_H
#define CLASSIFICATION_METRICS_H

#include <opencv2/opencv.hpp>
#include <vector>
#include <string>
#include <stdexcept>
#include <QList>
#include <QJsonObject>
#include <QJsonArray>
#include <QVariantMap>

namespace QDV {

class ClassificationMetrics {
public:
    // 原有 C++ 接口（保持向后兼容）
    static double accuracy(const std::vector<int>& yTrue, const std::vector<int>& yPred);

    static double precision(const std::vector<int>& yTrue, const std::vector<int>& yPred, int classId);

    static double recall(const std::vector<int>& yTrue, const std::vector<int>& yPred, int classId);

    static double f1Score(const std::vector<int>& yTrue, const std::vector<int>& yPred, int classId);

    static cv::Mat confusionMatrix(const std::vector<int>& yTrue, const std::vector<int>& yPred, int numClasses);

    static double macroF1(const std::vector<int>& yTrue, const std::vector<int>& yPred, int numClasses);

    static double microF1(const std::vector<int>& yTrue, const std::vector<int>& yPred, int numClasses);

    // 新增 Qt 友好接口
    static double accuracy(const QList<int>& yTrue, const QList<int>& yPred);

    static double precision(const QList<int>& yTrue, const QList<int>& yPred, int classId);

    static double recall(const QList<int>& yTrue, const QList<int>& yPred, int classId);

    static double f1Score(const QList<int>& yTrue, const QList<int>& yPred, int classId);

    static QJsonArray confusionMatrixToJson(const cv::Mat& matrix);

    static QVariantMap calculateAllMetrics(const QList<int>& yTrue, const QList<int>& yPred, int numClasses);

    static QJsonObject metricsToJson(const QVariantMap& metrics);

private:
    static int totalTruePositives(const cv::Mat& cm, int numClasses);
    static int totalFalsePositives(const cv::Mat& cm, int numClasses);
    static int totalFalseNegatives(const cv::Mat& cm, int numClasses);
    
    static void validateInput(const std::vector<int>& yTrue, const std::vector<int>& yPred);
};

} // namespace QDV

#endif // CLASSIFICATION_METRICS_H
