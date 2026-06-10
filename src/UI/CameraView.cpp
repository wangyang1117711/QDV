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
#include <QPushButton>
#include <QPixmap>
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
    importAction = toolbar->addAction("导入图片");
    prevImageAction = toolbar->addAction("上一张");
    prevImageAction->setEnabled(false);
    nextImageAction = toolbar->addAction("下一张");
    nextImageAction->setEnabled(false);
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
    m_videoLabel->setStyleSheet("background-color: #1e1e1e; border: 1px solid #444; color: #aaa; font-size: 16px;");
    m_videoLabel->setText("未连接相机\n请点击\"连接相机\"开始预览\n或点击\"导入图片\"加载本地图像");

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

        if (m_capture && m_capture->isOpened()) {
            connectAction->setEnabled(false);
            disconnectAction->setEnabled(true);
            captureAction->setEnabled(true);
            settingsAction->setEnabled(true);
            importAction->setEnabled(false);
            prevImageAction->setEnabled(false);
            nextImageAction->setEnabled(false);
            statusLabel->setText("已连接");
            statusLabel->setStyleSheet("color: #4CAF50; font-size: 14px; font-weight: bold;");
        } else {
            statusLabel->setText("连接失败");
            statusLabel->setStyleSheet("color: #F44336; font-size: 14px; font-weight: bold;");
            QMessageBox::StandardButton reply = QMessageBox::question(this,
                "相机连接失败",
                "无法连接到相机设备。\n\n是否切换到图片导入模式？\n\n提示：您可以批量导入本地图片进行检测。",
                QMessageBox::Yes | QMessageBox::No,
                QMessageBox::Yes);
            if (reply == QMessageBox::Yes) {
                importImages();
            } else {
                m_videoLabel->setText("相机连接失败\n请检查相机设备或点击\"导入图片\"加载本地图像");
            }
        }
#else
        QMessageBox::information(this, "提示", "相机功能需要OpenCV支持");
        QMessageBox::StandardButton reply = QMessageBox::question(this,
            "相机不可用",
            "当前环境不支持相机功能。\n\n是否切换到图片导入模式？\n\n您可以批量导入本地图片进行检测。",
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::Yes);
        if (reply == QMessageBox::Yes) {
            importImages();
        }
#endif
    });

    connect(disconnectAction, &QAction::triggered, [this, statusLabel]() {
        stopCamera();
        exitImageMode();
        connectAction->setEnabled(true);
        disconnectAction->setEnabled(false);
        captureAction->setEnabled(false);
        settingsAction->setEnabled(false);
        importAction->setEnabled(true);
        prevImageAction->setEnabled(false);
        nextImageAction->setEnabled(false);
        statusLabel->setText("未连接");
        statusLabel->setStyleSheet("color: #F44336; font-size: 14px; font-weight: bold;");
        m_videoLabel->setText("未连接相机\n请点击\"连接相机\"开始预览\n或点击\"导入图片\"加载本地图像");
    });

    connect(captureAction, &QAction::triggered, [this]() {
        saveSnapshot();
    });

    connect(importAction, &QAction::triggered, this, &CameraView::importImages);

    connect(prevImageAction, &QAction::triggered, this, &CameraView::showPreviousImage);
    connect(nextImageAction, &QAction::triggered, this, &CameraView::showNextImage);
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
    // 清理最后一帧缓存，防止跨相机/图像模式切换时残留旧帧
    if (m_lastFrame) {
        delete m_lastFrame;
        m_lastFrame = nullptr;
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

void CameraView::importImages() {
    QStringList files = QFileDialog::getOpenFileNames(this,
        "批量导入图片",
        QString(),
        "图片文件 (*.png *.jpg *.jpeg *.bmp *.tiff *.tif *.webp);;所有文件 (*.*)");

    if (files.isEmpty()) return;

    stopCamera();
    m_capture = nullptr;

    m_importedImages.clear();
    m_importedImages = files;
    m_currentImageIndex = 0;

    enterImageMode();

    QMessageBox::information(this, "导入成功",
        QString("已成功导入 %1 张图片。\n\n"
                "使用「上一张」/「下一张」按钮浏览图片。\n"
                "当前显示：%2")
            .arg(m_importedImages.size())
            .arg(QFileInfo(m_importedImages.first()).fileName()));
}

void CameraView::loadImageFile(const QString& filePath) {
    m_videoLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

#ifdef HAS_OPENCV
    if (m_lastFrame) {
        delete m_lastFrame;
        m_lastFrame = nullptr;
    }

    cv::Mat img = cv::imread(filePath.toStdString(), cv::IMREAD_COLOR);
    if (img.empty()) {
        m_videoLabel->setText(QString("无法加载图片：%1\n文件可能已损坏或格式不支持").arg(filePath));
        return;
    }

    m_lastFrame = new cv::Mat();
    img.copyTo(*m_lastFrame);

    cv::cvtColor(img, img, cv::COLOR_BGR2RGB);

    QImage qimg(img.data, img.cols, img.rows, img.step, QImage::Format_RGB888);
    QImage deepCopy = qimg.copy();

    QSize displaySize = m_videoLabel->size();
    if (displaySize.width() < 100 || displaySize.height() < 100) {
        displaySize = QSize(640, 480);
    }

    QPixmap pixmap = QPixmap::fromImage(
        deepCopy.scaled(displaySize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    m_videoLabel->setPixmap(pixmap);
#else
    QPixmap pixmap(filePath);
    if (pixmap.isNull()) {
        m_videoLabel->setText(QString("无法加载图片：%1\n文件可能已损坏或格式不支持").arg(filePath));
        return;
    }

    QSize displaySize = m_videoLabel->size();
    if (displaySize.width() < 100 || displaySize.height() < 100) {
        displaySize = QSize(640, 480);
    }

    m_videoLabel->setPixmap(pixmap.scaled(displaySize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
#endif
}

void CameraView::displayCurrentImage() {
    if (m_importedImages.isEmpty() || m_currentImageIndex < 0 ||
        m_currentImageIndex >= m_importedImages.size()) {
        m_videoLabel->setText("无图片可显示");
        return;
    }

    QString filePath = m_importedImages[m_currentImageIndex];
    loadImageFile(filePath);

    QString statusText = QString("图片模式 | %1 / %2 | %3")
        .arg(m_currentImageIndex + 1)
        .arg(m_importedImages.size())
        .arg(QFileInfo(filePath).fileName());
    m_videoLabel->setToolTip(statusText);
}

void CameraView::showPreviousImage() {
    if (m_importedImages.isEmpty()) return;
    m_currentImageIndex = (m_currentImageIndex - 1 + m_importedImages.size()) % m_importedImages.size();
    displayCurrentImage();
}

void CameraView::showNextImage() {
    if (m_importedImages.isEmpty()) return;
    m_currentImageIndex = (m_currentImageIndex + 1) % m_importedImages.size();
    displayCurrentImage();
}

void CameraView::enterImageMode() {
    m_isImageMode = true;
    m_frameTimer->stop();
    connectAction->setEnabled(true);
    disconnectAction->setEnabled(true);
    captureAction->setEnabled(true);
    importAction->setEnabled(true);
    prevImageAction->setEnabled(m_importedImages.size() > 1);
    nextImageAction->setEnabled(m_importedImages.size() > 1);
    displayCurrentImage();
}

void CameraView::exitImageMode() {
    m_isImageMode = false;
    m_importedImages.clear();
    m_currentImageIndex = -1;
    m_videoLabel->setToolTip("");
}