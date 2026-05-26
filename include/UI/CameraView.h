#ifndef CAMERA_VIEW_H
#define CAMERA_VIEW_H

#include <QWidget>
#include <QAction>

class QLabel;
class QTimer;
class QComboBox;

namespace cv {
    class VideoCapture;
    class Mat;
}

class CameraView : public QWidget {
    Q_OBJECT

public:
    explicit CameraView(QWidget* parent = nullptr);
    ~CameraView();

private slots:
    void updateFrame();

private:
    void setupUI();
    void startCamera(int cameraIndex = 0);
    void stopCamera();
    void saveSnapshot();
    void applyCameraSettings(int cameraIndex, int width, int height, int fps);

    QAction* connectAction;
    QAction* disconnectAction;
    QAction* captureAction;
    QAction* recordAction;
    QAction* settingsAction;

    QLabel* m_videoLabel;
    QTimer* m_frameTimer;
    cv::VideoCapture* m_capture;
    cv::Mat* m_lastFrame;
    QComboBox* m_cameraCombo;
    QComboBox* m_resolutionCombo;
    QComboBox* m_fpsCombo;
};

#endif // CAMERA_VIEW_H