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
    // v5.3.3：相机设置对话框
    void showCameraSettings();

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
    // v5.3.3：实时应用相机参数到已打开的 VideoCapture
    void applyRuntimeCameraSetting(int propId, double value);
    // v5.3.8：刷新相机信息面板（句柄/IP/接口）
    void updateCameraInfoUI();

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

    // v5.3.8：相机信息面板控件
    QLabel* m_handleLabel = nullptr;
    QLabel* m_ipLabel = nullptr;
    QLabel* m_interfaceLabel = nullptr;
    QPushButton* m_copyHandleButton = nullptr;

    QStringList m_importedImages;
    int m_currentImageIndex = -1;
    bool m_isImageMode = false;

    // v5.3.8：当前已连接相机的元信息
    QString m_currentHandle;
    QString m_currentIp;
    QString m_currentInterface;
};

#endif // CAMERA_VIEW_H