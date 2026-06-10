#include "../catch2/catch2_minimal.hpp"
#include "AI/ClassificationMetrics.h"
#include <QList>

using namespace QDV;

TEST_CASE("Metrics Accuracy Perfect", "[metrics]") {
    std::vector<int> yTrue = {0, 1, 2, 0, 1, 2};
    std::vector<int> yPred = {0, 1, 2, 0, 1, 2};
    double acc = ClassificationMetrics::accuracy(yTrue, yPred);
    REQUIRE(acc == 1.0);
}

TEST_CASE("Metrics Accuracy Zero", "[metrics]") {
    std::vector<int> yTrue = {0, 1, 2};
    std::vector<int> yPred = {1, 2, 0};
    double acc = ClassificationMetrics::accuracy(yTrue, yPred);
    REQUIRE(acc == 0.0);
}

TEST_CASE("Metrics Precision", "[metrics]") {
    std::vector<int> yTrue = {0, 0, 1, 1, 2, 2};
    std::vector<int> yPred = {0, 1, 1, 1, 2, 0};
    double p = ClassificationMetrics::precision(yTrue, yPred, 1);
    REQUIRE(p >= 0.0);
    REQUIRE(p <= 1.0);
}

TEST_CASE("Metrics Recall", "[metrics]") {
    std::vector<int> yTrue = {0, 0, 1, 1, 2, 2};
    std::vector<int> yPred = {0, 1, 1, 1, 2, 0};
    double r = ClassificationMetrics::recall(yTrue, yPred, 1);
    REQUIRE(r >= 0.0);
    REQUIRE(r <= 1.0);
}

TEST_CASE("Metrics F1 Score", "[metrics]") {
    std::vector<int> yTrue = {0, 0, 1, 1, 2, 2};
    std::vector<int> yPred = {0, 1, 1, 1, 2, 0};
    double f1 = ClassificationMetrics::f1Score(yTrue, yPred, 1);
    REQUIRE(f1 >= 0.0);
    REQUIRE(f1 <= 1.0);
}

TEST_CASE("Metrics Confusion Matrix Shape", "[metrics]") {
    std::vector<int> yTrue = {0, 1, 2, 0, 1, 2};
    std::vector<int> yPred = {0, 1, 2, 0, 1, 2};
    cv::Mat cm = ClassificationMetrics::confusionMatrix(yTrue, yPred, 3);
    REQUIRE(cm.rows == 3);
    REQUIRE(cm.cols == 3);
}

TEST_CASE("Metrics Macro F1", "[metrics]") {
    std::vector<int> yTrue = {0, 0, 1, 1, 2, 2};
    std::vector<int> yPred = {0, 1, 1, 1, 2, 0};
    double macro = ClassificationMetrics::macroF1(yTrue, yPred, 3);
    REQUIRE(macro >= 0.0);
    REQUIRE(macro <= 1.0);
}

TEST_CASE("Metrics Micro F1", "[metrics]") {
    std::vector<int> yTrue = {0, 0, 1, 1, 2, 2};
    std::vector<int> yPred = {0, 1, 1, 1, 2, 0};
    double micro = ClassificationMetrics::microF1(yTrue, yPred, 3);
    REQUIRE(micro >= 0.0);
    REQUIRE(micro <= 1.0);
}

TEST_CASE("Metrics Qt Accuracy", "[metrics][qt]") {
    QList<int> yTrue = {0, 1, 2, 0, 1, 2};
    QList<int> yPred = {0, 1, 2, 0, 1, 2};
    double acc = ClassificationMetrics::accuracy(yTrue, yPred);
    REQUIRE(acc == 1.0);
}

TEST_CASE("Metrics Qt Precision", "[metrics][qt]") {
    QList<int> yTrue = {0, 0, 1, 1, 2, 2};
    QList<int> yPred = {0, 1, 1, 1, 2, 0};
    double p = ClassificationMetrics::precision(yTrue, yPred, 1);
    REQUIRE(p >= 0.0);
}

TEST_CASE("Metrics Confusion Matrix to Json", "[metrics][qt]") {
    std::vector<int> yTrue = {0, 1, 2, 0, 1, 2};
    std::vector<int> yPred = {0, 1, 2, 0, 1, 2};
    cv::Mat cm = ClassificationMetrics::confusionMatrix(yTrue, yPred, 3);
    QJsonArray json = ClassificationMetrics::confusionMatrixToJson(cm);
    REQUIRE_FALSE(json.isEmpty());
    REQUIRE(json.size() == 3);
}
