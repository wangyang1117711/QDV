#ifndef INFERENCEENGINE_H
#define INFERENCEENGINE_H

#include <QObject>
#include <QString>
#include <QJsonObject>
#include <opencv2/opencv.hpp>

class InferenceEngine : public QObject {
    Q_OBJECT
    
public:
    explicit InferenceEngine(QObject* parent = nullptr);
    ~InferenceEngine();
    
    bool loadModel(const QString& modelPath);
    bool unloadModel();
    
    bool infer(const cv::Mat& input, cv::Mat& output, QJsonObject& result);
    
    bool isModelLoaded() const { return m_modelLoaded; }
    QString currentModelPath() const { return m_modelPath; }
    
signals:
    void modelLoaded(bool success);
    void inferenceCompleted(bool success);
    
private:
    QString m_modelPath;
    bool m_modelLoaded = false;
};

#endif // INFERENCEENGINE_H