#include "AI/ClassificationMetrics.h"
#include "Core/Logger.h"
#include <cmath>
#include <algorithm>

namespace QDV {

void ClassificationMetrics::validateInput(const std::vector<int>& yTrue, const std::vector<int>& yPred) {
    if (yTrue.empty() || yPred.empty()) {
        throw std::invalid_argument("ClassificationMetrics: yTrue and yPred must not be empty");
    }
    if (yTrue.size() != yPred.size()) {
        throw std::invalid_argument("ClassificationMetrics: yTrue and yPred must have the same size");
    }
}

double ClassificationMetrics::accuracy(const std::vector<int>& yTrue, const std::vector<int>& yPred) {
    validateInput(yTrue, yPred);
    int correct = 0;
    for (size_t i = 0; i < yTrue.size(); ++i) {
        if (yTrue[i] == yPred[i]) correct++;
    }
    return static_cast<double>(correct) / yTrue.size();
}

double ClassificationMetrics::precision(const std::vector<int>& yTrue, const std::vector<int>& yPred, int classId) {
    validateInput(yTrue, yPred);
    int tp = 0, fp = 0;
    for (size_t i = 0; i < yTrue.size(); ++i) {
        if (yPred[i] == classId) {
            if (yTrue[i] == classId) tp++;
            else fp++;
        }
    }
    int denom = tp + fp;
    return (denom == 0) ? 0.0 : static_cast<double>(tp) / denom;
}

double ClassificationMetrics::recall(const std::vector<int>& yTrue, const std::vector<int>& yPred, int classId) {
    validateInput(yTrue, yPred);
    int tp = 0, fn = 0;
    for (size_t i = 0; i < yTrue.size(); ++i) {
        if (yTrue[i] == classId) {
            if (yPred[i] == classId) tp++;
            else fn++;
        }
    }
    int denom = tp + fn;
    return (denom == 0) ? 0.0 : static_cast<double>(tp) / denom;
}

double ClassificationMetrics::f1Score(const std::vector<int>& yTrue, const std::vector<int>& yPred, int classId) {
    double p = precision(yTrue, yPred, classId);
    double r = recall(yTrue, yPred, classId);
    double denom = p + r;
    return (denom == 0.0) ? 0.0 : 2.0 * p * r / denom;
}

cv::Mat ClassificationMetrics::confusionMatrix(const std::vector<int>& yTrue, const std::vector<int>& yPred, int numClasses) {
    validateInput(yTrue, yPred);
    cv::Mat cm = cv::Mat::zeros(numClasses, numClasses, CV_32S);
    for (size_t i = 0; i < yTrue.size(); ++i) {
        int t = yTrue[i];
        int p = yPred[i];
        if (t >= 0 && t < numClasses && p >= 0 && p < numClasses) {
            cm.at<int>(t, p)++;
        }
    }
    return cm;
}

int ClassificationMetrics::totalTruePositives(const cv::Mat& cm, int numClasses) {
    int sum = 0;
    for (int i = 0; i < numClasses; ++i) sum += cm.at<int>(i, i);
    return sum;
}

int ClassificationMetrics::totalFalsePositives(const cv::Mat& cm, int numClasses) {
    int sum = 0;
    for (int i = 0; i < numClasses; ++i) {
        for (int j = 0; j < numClasses; ++j) {
            if (i != j) sum += cm.at<int>(j, i);
        }
    }
    return sum;
}

int ClassificationMetrics::totalFalseNegatives(const cv::Mat& cm, int numClasses) {
    int sum = 0;
    for (int i = 0; i < numClasses; ++i) {
        for (int j = 0; j < numClasses; ++j) {
            if (i != j) sum += cm.at<int>(i, j);
        }
    }
    return sum;
}

double ClassificationMetrics::macroF1(const std::vector<int>& yTrue, const std::vector<int>& yPred, int numClasses) {
    validateInput(yTrue, yPred);
    double sum = 0.0;
    int activeClasses = 0;
    for (int c = 0; c < numClasses; ++c) {
        double f1 = f1Score(yTrue, yPred, c);
        if (f1 > 0.0 || precision(yTrue, yPred, c) > 0.0 || recall(yTrue, yPred, c) > 0.0) {
            sum += f1;
            activeClasses++;
        }
    }
    return (activeClasses == 0) ? 0.0 : sum / activeClasses;
}

double ClassificationMetrics::microF1(const std::vector<int>& yTrue, const std::vector<int>& yPred, int numClasses) {
    validateInput(yTrue, yPred);
    cv::Mat cm = confusionMatrix(yTrue, yPred, numClasses);
    int tpSum = totalTruePositives(cm, numClasses);
    int fpSum = totalFalsePositives(cm, numClasses);
    int fnSum = totalFalseNegatives(cm, numClasses);
    double microP = (tpSum + fpSum == 0) ? 0.0 : static_cast<double>(tpSum) / (tpSum + fpSum);
    double microR = (tpSum + fnSum == 0) ? 0.0 : static_cast<double>(tpSum) / (tpSum + fnSum);
    return (microP + microR == 0.0) ? 0.0 : 2.0 * microP * microR / (microP + microR);
}

// Qt 接口实现
double ClassificationMetrics::accuracy(const QList<int>& yTrue, const QList<int>& yPred) {
    std::vector<int> vTrue(yTrue.begin(), yTrue.end());
    std::vector<int> vPred(yPred.begin(), yPred.end());
    return accuracy(vTrue, vPred);
}

double ClassificationMetrics::precision(const QList<int>& yTrue, const QList<int>& yPred, int classId) {
    std::vector<int> vTrue(yTrue.begin(), yTrue.end());
    std::vector<int> vPred(yPred.begin(), yPred.end());
    return precision(vTrue, vPred, classId);
}

double ClassificationMetrics::recall(const QList<int>& yTrue, const QList<int>& yPred, int classId) {
    std::vector<int> vTrue(yTrue.begin(), yTrue.end());
    std::vector<int> vPred(yPred.begin(), yPred.end());
    return recall(vTrue, vPred, classId);
}

double ClassificationMetrics::f1Score(const QList<int>& yTrue, const QList<int>& yPred, int classId) {
    std::vector<int> vTrue(yTrue.begin(), yTrue.end());
    std::vector<int> vPred(yPred.begin(), yPred.end());
    return f1Score(vTrue, vPred, classId);
}

QJsonArray ClassificationMetrics::confusionMatrixToJson(const cv::Mat& matrix) {
    QJsonArray result;
    for (int i = 0; i < matrix.rows; ++i) {
        QJsonArray row;
        for (int j = 0; j < matrix.cols; ++j) {
            row.append(matrix.at<int>(i, j));
        }
        result.append(row);
    }
    return result;
}

QVariantMap ClassificationMetrics::calculateAllMetrics(const QList<int>& yTrue, const QList<int>& yPred, int numClasses) {
    QVariantMap result;
    
    try {
        std::vector<int> vTrue(yTrue.begin(), yTrue.end());
        std::vector<int> vPred(yPred.begin(), yPred.end());
        
        result["accuracy"] = accuracy(vTrue, vPred);
        result["macroF1"] = macroF1(vTrue, vPred, numClasses);
        result["microF1"] = microF1(vTrue, vPred, numClasses);
        
        QVariantMap perClassMetrics;
        for (int c = 0; c < numClasses; ++c) {
            QVariantMap classMetrics;
            classMetrics["precision"] = precision(vTrue, vPred, c);
            classMetrics["recall"] = recall(vTrue, vPred, c);
            classMetrics["f1Score"] = f1Score(vTrue, vPred, c);
            perClassMetrics[QString::number(c)] = classMetrics;
        }
        result["perClass"] = perClassMetrics;
        
        // 不直接存储 cv::Mat 在 QVariant 中，避免转换问题
        
        Logger::info(QString("ClassificationMetrics: Calculated metrics for %1 samples, %2 classes")
                    .arg(yTrue.size()).arg(numClasses));
    } catch (const std::exception& e) {
        Logger::error(QString("ClassificationMetrics: Error calculating metrics - %1").arg(e.what()));
    }
    
    return result;
}

QJsonObject ClassificationMetrics::metricsToJson(const QVariantMap& metrics) {
    QJsonObject result;
    
    if (metrics.contains("accuracy")) {
        result["accuracy"] = metrics["accuracy"].toDouble();
    }
    if (metrics.contains("macroF1")) {
        result["macroF1"] = metrics["macroF1"].toDouble();
    }
    if (metrics.contains("microF1")) {
        result["microF1"] = metrics["microF1"].toDouble();
    }
    
    if (metrics.contains("perClass")) {
        QJsonObject perClassObj;
        QVariantMap perClass = metrics["perClass"].toMap();
        for (auto it = perClass.begin(); it != perClass.end(); ++it) {
            QVariantMap cm = it.value().toMap();
            QJsonObject cmObj;
            cmObj["precision"] = cm["precision"].toDouble();
            cmObj["recall"] = cm["recall"].toDouble();
            cmObj["f1Score"] = cm["f1Score"].toDouble();
            perClassObj[it.key()] = cmObj;
        }
        result["perClass"] = perClassObj;
    }
    
    return result;
}

} // namespace QDV
