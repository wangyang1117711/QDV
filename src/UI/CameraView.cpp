#include "CameraView.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QToolBar>
#include <QAction>
#include <QLabel>
#include <QTimer>
#include <QComboBox>
#include <QGroupBox>
#include <QFormLayout>
#include <QImage>
#include <QFileDialog>
#include <QFileInfo>
#include <QDateTime>
#include <QMessageBox>
#include <QPainter>
#ifdef HAS_OPENCV
#include <opencv2/opencv.hpp>
#include <opencv2/imgproc.hpp>
#endif

CameraView::CameraView(QWidget* parent) : QWidget(parent), m_capture(nullptr), m_lastFrame(nullptr) {
    setupUI();
}

CameraView::~CameraView() {
    stopCamera();
    delete m_lastFrame;
}

void CameraView::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    QToolBar* toolbar = new QToolBar();
    toolbar->setStyleSheet(R"(
        QToolBar {
            background-color: #2d2d2d;
            border-bottom: 1px solid #444;
            padding: 4px 8px;
            spacing: 6px;
        }
        QToolBar QToolButton {
            background: transparent;
            border: 1px solid transparent;
            border-radius: 4px;
            padding: 6px 12px;
            color: #e0e0e0;
            font-size: 13px;
        }
        QToolBar QToolButton:hover {
            background-color: #555;
            border-color: #555;
        }
        QToolBar QToolButton:disabled {
            color: #666;
        }
    )");

    connectAction = toolbar->addAction("连接相机");
    disconnectAction = toolbar->addAction("断开相机");
    disconnectAction->setEnabled(false);
    captureAction = toolbar->addAction("拍照");
    captureAction->setEnabled(false);
    recordAction = toolbar->addAction("录像");
    recordAction->setEnabled(false);
    recordAction->setToolTip("即将推出");
    settingsAction = toolbar->addAction("相机设置");
    settingsAction->setEnabled(false);

    mainLayout->addWidget(toolbar);

    QWidget* contentWidget = new QWidget();
    QHBoxLayout* contentLayout = new QHBoxLayout(contentWidget);
    contentLayout->setContentsMargins(8, 8, 8, 8);
    contentLayout->setSpacing(8);

    QGroupBox* controlGroup = new QGroupBox("相机控制");
    QFormLayout* controlLayout = new QFormLayout(controlGroup);
    controlLayout->setSpacing(12);
    controlLayout->setContentsMargins(12, 20, 12, 12);

    m_cameraCombo = new QComboBox();
    m_cameraCombo->addItems({"Camera 0", "Camera 1"});
    m_cameraCombo->setFixedWidth(160);
    QLabel* cameraLabel = new QLabel("选择相机:");
    cameraLabel->setStyleSheet("font-weight: bold;");
    controlLayout->addRow(cameraLabel, m_cameraCombo);

    m_resolutionCombo = new QComboBox();
    m_resolutionCombo->addItems({"640x480", "800x600", "1280x720", "1920x1080"});
    m_resolutionCombo->setCurrentText("1280x720");
    m_resolutionCombo->setFixedWidth(160);
    QLabel* resolutionLabel = new QLabel("分辨率:");
    resolutionLabel->setStyleSheet("font-weight: bold;");
    controlLayout->addRow(resolutionLabel, m_resolutionCombo);

    m_fpsCombo = new QComboBox();
    m_fpsCombo->addItems({"15", "30", "60"});
    m_fpsCombo->setCurrentText("30");
    m_fpsCombo->setFixedWidth(160);
    QLabel* fpsLabel = new QLabel("帧率:");
    fpsLabel->setStyleSheet("font-weight: bold;");
    controlLayout->addRow(fpsLabel, m_fpsCombo);

    QLabel* statusLabel = new QLabel("未连接");
    statusLabel->setStyleSheet("color: #F44336; font-size: 14px; font-weight: bold;");
    controlLayout->addRow(new QLabel("状态:"), statusLabel);

    contentLayout->addWidget(controlGroup);

    m_videoLabel = new QLabel();
    m_videoLabel->setMinimumSize(640, 480);
    m_videoLabel->setAlignment(Qt::AlignCenter);
    m_videoLabel->setStyleSheet("background-color: #1e1e1e; border: 1px solid #444;");
    m_videoLabel->setText("未连接相机\n请点击\"连接相机\"开始预览");
    m_videoLabel->setStyleSheet("background-color: #1e1e1e; border: 1px solid #444; color: #aaa; font-size: 16px;");

    contentLayout->addWidget(m_videoLabel, 1);

    mainLayout->addWidget(contentWidget);

    m_frameTimer = new QTimer(this);
    connect(m_frameTimer, &QTimer::timeout, this, &CameraView::updateFrame);

    connect(connectAction, &QAction::triggered, [this, statusLabel]() {
#ifdef HAS_OPENCV
        int cameraIndex = m_cameraCombo->currentText().remove("Camera ").toInt();
        QString res = m_resolutionCombo->currentText();
        QStringList parts = res.split('x');
        int width = parts.value(0).toInt();
        int height = parts.value(1).toInt();
        int fps = m_fpsCombo->currentText().toInt();
        startCamera(cameraIndex);
        applyCameraSettings(cameraIndex, width, height, fps);
        connectAction->setEnabled(false);
        disconnectAction->setEnabled(true);
        captureAction->setEnabled(true);
        settingsAction->setEnabled(true);
        statusLabel->setText("已连接");
        statusLabel->setStyleSheet("color: #4CAF50; font-size: 14px; font-weight: bold;");
#else
        QMessageBox::information(this, "提示", "相机功能需要OpenCV支持");
#endif
    });

    connect(disconnectAction, &QAction::triggered, [this, statusLabel]() {
        stopCamera();
        connectAction->setEnabled(true);
        disconnectAction->setEnabled(false);
        captureAction->setEnabled(false);
        settingsAction->setEnabled(false);
        statusLabel->setText("未连接");
        statusLabel->setStyleSheet("color: #F44336; font-size: 14px; font-weight: bold;");
        m_videoLabel->setText("未连接相机\n请点击\"连接相机\"开始预览");
    });

    connect(captureAction, &QAction::triggered, [this]() {
        saveSnapshot();
    });
}

void CameraView::startCamera(int cameraIndex) {
#ifdef HAS_OPENCV
    stopCamera();
    m_capture = new cv::VideoCapture(cameraIndex);
    if (m_capture->isOpened()) {
        m_frameTimer->start(33);
    }
#endif
}

void CameraView::stopCamera() {
    m_frameTimer->stop();
#ifdef HAS_OPENCV
    if (m_capture) {
        m_capture->release();
        delete m_capture;
        m_capture = nullptr;
    }
#endif
}

void CameraView::updateFrame() {
#ifdef HAS_OPENCV
    if (!m_capture || !m_capture->isOpened()) return;

    cv::Mat frame;
    *m_capture >> frame;
    if (frame.empty()) return;

    if (!m_lastFrame) {
        m_lastFrame = new cv::Mat();
    }
    frame.copyTo(*m_lastFrame);

    cv::cvtColor(frame, frame, cv::COLOR_BGR2RGB);
    QImage img(frame.data, frame.cols, frame.rows, frame.step, QImage::Format_RGB888);
    QPixmap pixmap = QPixmap::fromImage(img.scaled(m_videoLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    m_videoLabel->setPixmap(pixmap);
#endif
}

void CameraView::saveSnapshot() {
#ifdef HAS_OPENCV
    if (!m_lastFrame || m_lastFrame->empty()) {
        QMessageBox::warning(this, "提示", "没有可保存的画面，请先连接相机");
        return;
    }

    QString filePath = QFileDialog::getSaveFileName(this, "保存快照", "", "PNG Images (*.png);;JPEG Images (*.jpg)");
    if (filePath.isEmpty()) return;

    cv::imwrite(filePath.toStdString(), *m_lastFrame);
#endif
}

void CameraView::applyCameraSettings(int cameraIndex, int width, int height, int fps) {
#ifdef HAS_OPENCV
    if (!m_capture || !m_capture->isOpened()) return;

    m_capture->set(cv::CAP_PROP_FRAME_WIDTH, width);
    m_capture->set(cv::CAP_PROP_FRAME_HEIGHT, height);
    m_capture->set(cv::CAP_PROP_FPS, fps);
    (void)cameraIndex;
#endif
}