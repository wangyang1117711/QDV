#ifndef TOOLCHAIN_VERIFIER_H
#define TOOLCHAIN_VERIFIER_H

#include <QObject>
#include <QString>
#include <QList>
#include <QMap>
#include <functional>
#include <opencv2/opencv.hpp>

class VisionTool;
class ToolResult;

struct ToolVerifyResult {
    QString toolName;
    bool passed = false;
    QString message;
    qint64 elapsedMs = 0;
};

class ToolChainVerifier {
public:
    ToolChainVerifier();

    QList<ToolVerifyResult> verifyAll();
    ToolVerifyResult verifyTemplateMatch();
    ToolVerifyResult verifyEdgeDetect();
    ToolVerifyResult verifyBlobDetect();
    ToolVerifyResult verifyColorDetect();
    ToolVerifyResult verifyThreshold();
    ToolVerifyResult verifyImagePreprocess();
    ToolVerifyResult verifyContourAnalyze();
    ToolVerifyResult verifyGeometryMeasure();
    ToolVerifyResult verifyLineCircleDetect();
    ToolVerifyResult verifyImageArithmetic();
    ToolVerifyResult verifyImageTransform();
    ToolVerifyResult verifyImageMerge();
    ToolVerifyResult verifyBranchControl();

    int passedCount() const;
    int totalCount() const;

private:
    cv::Mat createTestImage(int width, int height);
    cv::Mat createColorTestImage();
    cv::Mat createPatternTestImage();
    cv::Mat createGeometricTestImage();
    cv::Mat createLineCircleTestImage();

    QList<ToolVerifyResult> m_results;
};

#endif