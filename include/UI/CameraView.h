#ifndef CAMERA_VIEW_H
#define CAMERA_VIEW_H

#include <QWidget>
#include <QAction>
#include <QStringList>

class QLabel;
class QTimer;
class QComboBox;
class QPushButton;

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
    void importImages();
    void showPreviousImage();
    void showNextImage();

private:
    void setupUI();
    void startCamera(int cameraIndex = 0);
    void stopCamera();
    void saveSnapshot();
    void applyCameraSettings(int cameraIndex, int width, int height, int fps);
    void loadImageFile(const QString& filePath);
    void displayCurrentImage();
    void enterImageMode();
    void exitImageMode();

    QAction* connectAction;
    QAction* disconnectAction;
    QAction* captureAction;
    QAction* recordAction;
    QAction* settingsAction;
    QAction* importAction;
    QAction* prevImageAction;
    QAction* nextImageAction;

    QLabel* m_videoLabel;
    QTimer* m_frameTimer;
    cv::VideoCapture* m_capture;
    cv::Mat* m_lastFrame;
    QComboBox* m_cameraCombo;
    QComboBox* m_resolutionCombo;
    QComboBox* m_fpsCombo;

    QStringList m_importedImages;
    int m_currentImageIndex = -1;
    bool m_isImageMode = false;
};

#endif // CAMERA_VIEW_H