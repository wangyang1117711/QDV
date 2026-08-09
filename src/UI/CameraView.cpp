#include "CameraView.h"
#include "Core/CameraConfig.h"
#include "Vision/OpenFramegrabberTool.h"
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
#include "Core/PathValidator.h"  // S6 修复：路径校验
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QSlider>
#include <QDialog>
#include <QDialogButtonBox>
#include <QApplication>
#include <QClipboard>
#ifdef HAS_OPENCV
#include <opencv2/opencv.hpp>
#include <opencv2/imgproc.hpp>
#endif

// v5.3.3：CameraConfig 静态成员定义
QMutex CameraConfig::s_mutex;

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
            padding: 3px 12px;
            spacing: 6px;
        }
        QToolBar QToolButton {
            background: transparent;
            border: 1px solid transparent;
            border-radius: 4px;
            padding: 4px 14px;
            color: #e0e0e0;
            font-size: 12px;
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
    contentLayout->setContentsMargins(12, 8, 12, 8);
    contentLayout->setSpacing(12);

    QGroupBox* controlGroup = new QGroupBox("相机控制");
    QFormLayout* controlLayout = new QFormLayout(controlGroup);
    controlLayout->setSpacing(8);
    controlLayout->setContentsMargins(12, 16, 12, 12);

    m_cameraCombo = new QComboBox();
    m_cameraCombo->addItems({"Camera 0", "Camera 1"});
    m_cameraCombo->setFixedWidth(140);
    QLabel* cameraLabel = new QLabel("选择相机:");
    cameraLabel->setStyleSheet("font-weight: bold;");
    controlLayout->addRow(cameraLabel, m_cameraCombo);

    m_resolutionCombo = new QComboBox();
    m_resolutionCombo->addItems({"640x480", "800x600", "1280x720", "1920x1080"});
    m_resolutionCombo->setCurrentText("1280x720");
    m_resolutionCombo->setFixedWidth(140);
    QLabel* resolutionLabel = new QLabel("分辨率:");
    resolutionLabel->setStyleSheet("font-weight: bold;");
    controlLayout->addRow(resolutionLabel, m_resolutionCombo);

    m_fpsCombo = new QComboBox();
    m_fpsCombo->addItems({"15", "30", "60"});
    m_fpsCombo->setCurrentText("30");
    m_fpsCombo->setFixedWidth(140);
    QLabel* fpsLabel = new QLabel("帧率:");
    fpsLabel->setStyleSheet("font-weight: bold;");
    controlLayout->addRow(fpsLabel, m_fpsCombo);

    QLabel* statusLabel = new QLabel("未连接");
    statusLabel->setStyleSheet("color: #F44336; font-size: 14px; font-weight: bold;");
    controlLayout->addRow(new QLabel("状态:"), statusLabel);

    contentLayout->addWidget(controlGroup);

    // v5.3.8：相机信息面板（句柄 / IP / 接口 / 复制句柄）
    QGroupBox* infoGroup = new QGroupBox("相机信息");
    QFormLayout* infoLayout = new QFormLayout(infoGroup);
    infoLayout->setSpacing(8);
    infoLayout->setContentsMargins(12, 16, 12, 12);

    m_handleLabel = new QLabel("未连接");
    m_handleLabel->setStyleSheet("color: #F44336; font-weight: bold;");
    m_handleLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    infoLayout->addRow("句柄:", m_handleLabel);

    m_ipLabel = new QLabel("-");
    m_ipLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    infoLayout->addRow("IP 地址:", m_ipLabel);

    m_interfaceLabel = new QLabel("-");
    infoLayout->addRow("接口:", m_interfaceLabel);

    m_copyHandleButton = new QPushButton("复制句柄");
    m_copyHandleButton->setEnabled(false);
    m_copyHandleButton->setToolTip("将当前相机句柄复制到剪贴板，便于采集图像算子引用");
    connect(m_copyHandleButton, &QPushButton::clicked, [this]() {
        if (!m_currentHandle.isEmpty()) {
            QApplication::clipboard()->setText(m_currentHandle);
            QMessageBox::information(this, "已复制", QString("相机句柄已复制：%1").arg(m_currentHandle));
        }
    });
    infoLayout->addRow(m_copyHandleButton);

    contentLayout->addWidget(infoGroup);

    m_videoLabel = new QLabel();
    m_videoLabel->setMinimumSize(480, 360);
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

    // v5.3.3：相机设置按钮 —— 弹出设置对话框，实时调整曝光/Gain/分辨率等
    connect(settingsAction, &QAction::triggered, this, &CameraView::showCameraSettings);

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
        // v5.3.7：将已打开的相机句柄注册到全局表，供 OpenFramegrabberTool/GrabImageTool 复用
        QString handle = OpenFramegrabberTool::makeHandle("DirectShow", cameraIndex);
        OpenFramegrabberTool::registerExternalCamera(handle, m_capture, "DirectShow", cameraIndex);

        // v5.3.8：记录当前相机信息并刷新信息面板
        m_currentHandle = handle;
        m_currentInterface = "DirectShow";
        // DirectShow/USB 相机无真实网络 IP，优先显示 CameraConfig 中配置的 IP，否则显示 N/A
        CameraConfig cfg = CameraConfig::getGlobal();
        m_currentIp = cfg.ip.isEmpty() ? QStringLiteral("N/A (本地设备)") : cfg.ip;
        updateCameraInfoUI();
    }
#endif
}

void CameraView::stopCamera() {
    m_frameTimer->stop();
#ifdef HAS_OPENCV
    if (m_capture) {
        // v5.3.7：先从全局表注销（不 delete cap），再 release/delete
        QStringList handles = OpenFramegrabberTool::listCameraHandles();
        for (const QString& h : handles) {
            cv::VideoCapture* cap = OpenFramegrabberTool::acquireCamera(h);
            if (cap == m_capture) {
                OpenFramegrabberTool::unregisterExternalCamera(h);
                break;
            }
        }
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
    // v5.3.8：断开连接后清空相机信息
    m_currentHandle.clear();
    m_currentIp.clear();
    m_currentInterface.clear();
    updateCameraInfoUI();
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

    // S6 修复：清理路径，防止路径穿越与非法字符
    filePath = QDV::PathValidator::sanitize(filePath);
    if (filePath.isEmpty()) {
        QMessageBox::warning(this, "错误", "保存路径无效（包含非法字符或路径穿越）");
        return;
    }

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

    // S6 修复：清理每个路径，过滤掉包含路径穿越或非法字符的条目
    QStringList sanitizedFiles;
    sanitizedFiles.reserve(files.size());
    for (const QString& f : files) {
        QString cleaned = QDV::PathValidator::sanitize(f);
        if (!cleaned.isEmpty()) {
            sanitizedFiles.append(cleaned);
        }
    }
    if (sanitizedFiles.isEmpty()) {
        QMessageBox::warning(this, "错误", "所有选定路径均无效（包含非法字符或路径穿越）");
        return;
    }

    stopCamera();
    m_capture = nullptr;

    m_importedImages.clear();
    m_importedImages = sanitizedFiles;
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

// v5.3.3：相机设置对话框 —— 实时调整曝光/Gain/分辨率/白平衡等
void CameraView::showCameraSettings() {
#ifdef HAS_OPENCV
    if (!m_capture || !m_capture->isOpened()) {
        QMessageBox::information(this, "相机设置", "相机未连接，请先连接相机。");
        return;
    }

    QDialog dlg(this);
    dlg.setWindowTitle("相机设置");
    dlg.setMinimumWidth(380);

    QFormLayout* form = new QFormLayout(&dlg);
    form->setSpacing(10);
    form->setContentsMargins(16, 16, 16, 16);

    // 读取当前值
    int curWidth = static_cast<int>(m_capture->get(cv::CAP_PROP_FRAME_WIDTH));
    int curHeight = static_cast<int>(m_capture->get(cv::CAP_PROP_FRAME_HEIGHT));
    double curFps = m_capture->get(cv::CAP_PROP_FPS);
    double curExposure = m_capture->get(cv::CAP_PROP_EXPOSURE);
    double curGain = m_capture->get(cv::CAP_PROP_GAIN);
    double curBrightness = m_capture->get(cv::CAP_PROP_BRIGHTNESS);
    double curContrast = m_capture->get(cv::CAP_PROP_CONTRAST);
    int autoExp = static_cast<int>(m_capture->get(cv::CAP_PROP_AUTO_EXPOSURE));

    // 分辨率宽
    QSpinBox* widthSpin = new QSpinBox(&dlg);
    widthSpin->setRange(160, 7680);
    widthSpin->setValue(curWidth > 0 ? curWidth : 1280);
    form->addRow("分辨率宽 (px):", widthSpin);

    // 分辨率高
    QSpinBox* heightSpin = new QSpinBox(&dlg);
    heightSpin->setRange(120, 4320);
    heightSpin->setValue(curHeight > 0 ? curHeight : 720);
    form->addRow("分辨率高 (px):", heightSpin);

    // 帧率
    QDoubleSpinBox* fpsSpin = new QDoubleSpinBox(&dlg);
    fpsSpin->setRange(1, 240);
    fpsSpin->setValue(curFps > 0 ? curFps : 30);
    form->addRow("帧率 (fps):", fpsSpin);

    // 自动曝光开关
    QCheckBox* autoExpCheck = new QCheckBox("自动曝光", &dlg);
    autoExpCheck->setChecked(autoExp == 3);  // 3=自动模式（DCAM/V4L2），1=手动
    form->addRow(autoExpCheck);

    // 曝光时间
    QDoubleSpinBox* exposureSpin = new QDoubleSpinBox(&dlg);
    exposureSpin->setRange(1, 1000000);
    exposureSpin->setDecimals(0);
    exposureSpin->setValue(curExposure > 0 ? curExposure : 5000);
    exposureSpin->setSuffix(" us");
    form->addRow("曝光时间:", exposureSpin);

    // Gain
    QDoubleSpinBox* gainSpin = new QDoubleSpinBox(&dlg);
    gainSpin->setRange(0, 64);
    gainSpin->setDecimals(2);
    gainSpin->setValue(curGain >= 0 ? curGain : 1.0);
    gainSpin->setSuffix(" dB");
    form->addRow("增益 (Gain):", gainSpin);

    // 亮度
    QDoubleSpinBox* brightnessSpin = new QDoubleSpinBox(&dlg);
    brightnessSpin->setRange(0, 255);
    brightnessSpin->setValue(curBrightness >= 0 ? curBrightness : 128);
    form->addRow("亮度:", brightnessSpin);

    // 对比度
    QDoubleSpinBox* contrastSpin = new QDoubleSpinBox(&dlg);
    contrastSpin->setRange(0, 255);
    contrastSpin->setValue(curContrast >= 0 ? curContrast : 128);
    form->addRow("对比度:", contrastSpin);

    // 当前状态显示
    QLabel* statusLabel = new QLabel(&dlg);
    statusLabel->setStyleSheet("color: #888; font-size: 11px;");
    statusLabel->setText(QString("当前: %1x%2 @ %3fps\n曝光=%4  Gain=%5  亮度=%6  对比度=%7")
                             .arg(curWidth).arg(curHeight)
                             .arg(curFps, 0, 'f', 1)
                             .arg(curExposure, 0, 'f', 0)
                             .arg(curGain, 0, 'f', 2)
                             .arg(curBrightness, 0, 'f', 0)
                             .arg(curContrast, 0, 'f', 0));
    form->addRow(statusLabel);

    // 自动曝光联动
    QObject::connect(autoExpCheck, &QCheckBox::toggled, [&](bool checked) {
        exposureSpin->setEnabled(!checked);
        applyRuntimeCameraSetting(cv::CAP_PROP_AUTO_EXPOSURE, checked ? 3.0 : 1.0);
    });

    // 实时预览：滑块/旋钮变化时即时应用
    QObject::connect(widthSpin, QOverload<int>::of(&QSpinBox::valueChanged), [&](int v) {
        applyRuntimeCameraSetting(cv::CAP_PROP_FRAME_WIDTH, v);
    });
    QObject::connect(heightSpin, QOverload<int>::of(&QSpinBox::valueChanged), [&](int v) {
        applyRuntimeCameraSetting(cv::CAP_PROP_FRAME_HEIGHT, v);
    });
    QObject::connect(fpsSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), [&](double v) {
        applyRuntimeCameraSetting(cv::CAP_PROP_FPS, v);
    });
    QObject::connect(exposureSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), [&](double v) {
        if (!autoExpCheck->isChecked()) {
            applyRuntimeCameraSetting(cv::CAP_PROP_EXPOSURE, v);
        }
    });
    QObject::connect(gainSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), [&](double v) {
        applyRuntimeCameraSetting(cv::CAP_PROP_GAIN, v);
    });
    QObject::connect(brightnessSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), [&](double v) {
        applyRuntimeCameraSetting(cv::CAP_PROP_BRIGHTNESS, v);
    });
    QObject::connect(contrastSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), [&](double v) {
        applyRuntimeCameraSetting(cv::CAP_PROP_CONTRAST, v);
    });

    // 按钮
    QDialogButtonBox* btns = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Reset, &dlg);
    form->addRow(btns);

    QObject::connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    QObject::connect(btns->button(QDialogButtonBox::Reset), &QPushButton::clicked, [&]() {
        // 重置为对话框初始值
        widthSpin->setValue(curWidth > 0 ? curWidth : 1280);
        heightSpin->setValue(curHeight > 0 ? curHeight : 720);
        fpsSpin->setValue(curFps > 0 ? curFps : 30);
        exposureSpin->setValue(curExposure > 0 ? curExposure : 5000);
        gainSpin->setValue(curGain >= 0 ? curGain : 1.0);
        brightnessSpin->setValue(curBrightness >= 0 ? curBrightness : 128);
        contrastSpin->setValue(curContrast >= 0 ? curContrast : 128);
    });

    // v5.3.3：对话框确认后同步到全局 CameraConfig，供 OpenFramegrabberTool 读取
    QObject::connect(&dlg, &QDialog::accepted, [&]() {
        CameraConfig cfg;
        cfg.width = widthSpin->value();
        cfg.height = heightSpin->value();
        cfg.fps = fpsSpin->value();
        cfg.exposure = static_cast<int>(exposureSpin->value());
        cfg.gain = gainSpin->value();
        cfg.triggerMode = autoExpCheck->isChecked() ? "Continuous" : "Software";
        cfg.pixelFormat = "Mono8";  // CameraView 不支持像素格式选择，保持默认
        cfg.brightness = brightnessSpin->value();
        cfg.contrast = contrastSpin->value();
        CameraConfig::setGlobal(cfg);
    });

    dlg.exec();
#else
    QMessageBox::information(this, "相机设置", "未编译 OpenCV 支持，无法调整相机参数。");
#endif
}

// v5.3.3：实时应用相机参数到已打开的 VideoCapture
void CameraView::applyRuntimeCameraSetting(int propId, double value) {
#ifdef HAS_OPENCV
    if (!m_capture || !m_capture->isOpened()) return;
    m_capture->set(propId, value);
#endif
}

// v5.3.8：刷新相机信息面板（句柄 / IP / 接口 / 复制按钮状态）
void CameraView::updateCameraInfoUI() {
    if (m_currentHandle.isEmpty()) {
        m_handleLabel->setText("未连接");
        m_handleLabel->setStyleSheet("color: #F44336; font-weight: bold;");
        m_ipLabel->setText("-");
        m_interfaceLabel->setText("-");
        m_copyHandleButton->setEnabled(false);
    } else {
        m_handleLabel->setText(m_currentHandle);
        m_handleLabel->setStyleSheet("color: #4CAF50; font-weight: bold;");
        m_ipLabel->setText(m_currentIp);
        m_interfaceLabel->setText(m_currentInterface);
        m_copyHandleButton->setEnabled(true);
    }
}
