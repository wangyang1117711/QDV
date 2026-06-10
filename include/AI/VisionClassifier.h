#pragma once

#include <opencv2/opencv.hpp>
#include <string>
#include <vector>
#include <memory>
#include "AI/InferenceEngine.h"

namespace QDV {

struct ClassifyParams {
    int inputWidth = 224;
    int inputHeight = 224;
    cv::Scalar mean = cv::Scalar(0.485, 0.456, 0.406);
    double std = 1.0 / 255.0;
    bool swapRB = true;
    int topK = 5;
};

struct ClassificationResult {
    int classId = -1;
    std::string className;
    double confidence = 0.0;
    double preprocessMs = 0.0;
    double inferenceMs = 0.0;
};

class VisionClassifier {
public:
    VisionClassifier();
    ~VisionClassifier();

    bool loadModel(const std::string& onnxPath, const std::vector<std::string>& labels);
    std::vector<ClassificationResult> classify(const cv::Mat& image, const ClassifyParams& params = ClassifyParams());
    void warmup(int iterations = 3);
    bool isLoaded() const;
    const std::vector<std::string>& getLabels() const;

private:
    std::unique_ptr<InferenceEngine> m_engine;
    std::vector<std::string> m_labels;
    bool m_modelLoaded = false;
};

} // namespace QDV
