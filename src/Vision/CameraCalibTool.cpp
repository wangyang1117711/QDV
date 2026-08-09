#include "CameraCalibTool.h"
#include "Core/Logger.h"
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/calib3d.hpp>
#include <QFile>
#include <QStringList>
#include <QJsonArray>
#include <vector>
#include <cmath>

using namespace QDV;

// =====================================================
// 构造
// =====================================================
CameraCalibTool::CameraCalibTool() {
    m_name = "相机标定";
}

// =====================================================
// 参数配置
// =====================================================
bool CameraCalibTool::configure(const QJsonObject& params) {
    if (params.contains("boardW")) {
        m_boardW = std::max(2, params["boardW"].toInt());
    }
    if (params.contains("boardH")) {
        m_boardH = std::max(2, params["boardH"].toInt());
    }
    if (params.contains("squareSize")) {
        const double v = params["squareSize"].toDouble();
        m_squareSize = (v > 0.0) ? v : 25.0;
    }
    if (params.contains("imagePaths")) {
        m_imagePaths = params["imagePaths"].toString();
    }
    if (params.contains("outputFile")) {
        m_outputFile = params["outputFile"].toString();
    }
    if (params.contains("maxImages")) {
        m_maxImages = std::max(1, params["maxImages"].toInt());
    }
    m_params = params;
    return true;
}

// 兼容中文路径的图像加载（QFile + cv::imdecode）
namespace {
cv::Mat loadCalibImage(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return cv::Mat();
    const QByteArray data = f.readAll();
    return cv::imdecode(std::vector<uchar>(data.begin(), data.end()), cv::IMREAD_COLOR);
}
} // namespace

// =====================================================
// execute：执行张正友法相机标定
// =====================================================
bool CameraCalibTool::execute(const cv::Mat& input, ToolResult& result) {
    try {
        // 1. 解析图像路径（分号分隔）
        QStringList imgList;
        if (!m_imagePaths.isEmpty()) {
            const QStringList parts = m_imagePaths.split(';', Qt::SkipEmptyParts);
            for (const auto& p : parts) {
                const QString trimmed = p.trimmed();
                if (!trimmed.isEmpty()) imgList.append(trimmed);
            }
        }
        if (imgList.isEmpty()) {
            result.ok = false;
            result.data["error"] = "no image paths";
            cv::Mat black(480, 640, CV_8UC3, cv::Scalar(0, 0, 0));
            cv::putText(black, "no image paths", cv::Point(20, 240),
                        cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(0, 0, 255), 2);
            result.overlayImage = black;
            return false;
        }

        // 限制最大图像数
        if (imgList.size() > m_maxImages) {
            imgList = imgList.mid(0, m_maxImages);
            Logger::info(QString("CameraCalibTool: 图像数超过 maxImages=%1，已截断").arg(m_maxImages));
        }

        // 2. 逐图检测棋盘格角点
        const cv::Size boardSize(m_boardW, m_boardH);
        std::vector<std::vector<cv::Point2f>> imagePoints;
        std::vector<std::vector<cv::Point3f>> objectPoints;
        cv::Size imageSize;
        cv::Mat firstOverlay;                    // 首张成功图 + 角点，作为 overlay 备选
        std::vector<cv::Point2f> firstCorners;   // 首张成功图的角点（用于在 input 上重画）

        // 棋盘格物理坐标（Z=0 平面，单位 mm）
        std::vector<cv::Point3f> objectCorners;
        for (int i = 0; i < m_boardH; ++i) {
            for (int j = 0; j < m_boardW; ++j) {
                objectCorners.emplace_back(
                    static_cast<float>(j * m_squareSize),
                    static_cast<float>(i * m_squareSize), 0.0f);
            }
        }

        int validCount = 0;
        for (int idx = 0; idx < imgList.size(); ++idx) {
            cv::Mat img = loadCalibImage(imgList[idx]);
            if (img.empty()) {
                Logger::warn(QString("CameraCalibTool: 无法加载图像 %1").arg(imgList[idx]));
                continue;
            }
            if (imageSize.area() == 0) imageSize = img.size();

            // 转灰度
            cv::Mat gray;
            if (img.channels() == 3) {
                cv::cvtColor(img, gray, cv::COLOR_BGR2GRAY);
            } else {
                gray = img.clone();
            }

            // 检测内角点
            std::vector<cv::Point2f> corners;
            const bool found = cv::findChessboardCorners(gray, boardSize, corners,
                cv::CALIB_CB_ADAPTIVE_THRESH + cv::CALIB_CB_NORMALIZE_IMAGE);
            if (!found) {
                Logger::warn(QString("CameraCalibTool: 角点未找到 %1").arg(imgList[idx]));
                continue;
            }

            // 亚像素精细化（cv::cornerSubPix）
            cv::TermCriteria criteria(cv::TermCriteria::EPS + cv::TermCriteria::MAX_ITER, 30, 0.001);
            cv::cornerSubPix(gray, corners, cv::Size(11, 11), cv::Size(-1, -1), criteria);

            imagePoints.push_back(corners);
            objectPoints.push_back(objectCorners);
            ++validCount;

            // 首张成功图绘制角点作为 overlay 备选
            if (firstOverlay.empty()) {
                firstOverlay = img.clone();
                cv::drawChessboardCorners(firstOverlay, boardSize, corners, true);
                firstCorners = corners;
            }

            Logger::info(QString("CameraCalibTool: 检测成功 %1 (%2/%3)")
                             .arg(imgList[idx]).arg(validCount).arg(imgList.size()));
        }

        // 3. 有效图像数校验（>=3 才能标定）
        if (validCount < 3) {
            result.ok = false;
            result.data["error"] = "insufficient valid images (<3)";
            if (!firstOverlay.empty()) {
                cv::putText(firstOverlay, "insufficient images", cv::Point(10, 30),
                            cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 0, 255), 2);
                result.overlayImage = firstOverlay;
            } else {
                cv::Mat black(480, 640, CV_8UC3, cv::Scalar(0, 0, 0));
                cv::putText(black, "no corners detected", cv::Point(20, 240),
                            cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 0, 255), 2);
                result.overlayImage = black;
            }
            return false;
        }

        // 4. 标定相机内参（张正友法）
        cv::Mat cameraMatrix = cv::Mat::eye(3, 3, CV_64F);
        cv::Mat distCoeffs = cv::Mat::zeros(1, 5, CV_64F);
        std::vector<cv::Mat> rvecs, tvecs;
        // cv::calibrateCamera 返回值为 RMS 重投影误差
        const double rmsError = cv::calibrateCamera(
            objectPoints, imagePoints, imageSize,
            cameraMatrix, distCoeffs, rvecs, tvecs);

        const double fx = cameraMatrix.at<double>(0, 0);
        const double fy = cameraMatrix.at<double>(1, 1);
        const double cx = cameraMatrix.at<double>(0, 2);
        const double cy = cameraMatrix.at<double>(1, 2);

        // 5. 计算重投影误差（逐图 cv::projectPoints 对比，取平均）
        double totalError = 0.0;
        int totalPoints = 0;
        for (size_t i = 0; i < imagePoints.size(); ++i) {
            std::vector<cv::Point2f> projected;
            cv::projectPoints(objectPoints[i], rvecs[i], tvecs[i],
                              cameraMatrix, distCoeffs, projected);
            const auto& observed = imagePoints[i];
            double err = 0.0;
            for (size_t j = 0; j < observed.size(); ++j) {
                const double dx = projected[j].x - observed[j].x;
                const double dy = projected[j].y - observed[j].y;
                err += dx * dx + dy * dy;
            }
            totalError += err;
            totalPoints += static_cast<int>(observed.size());
        }
        const double meanReprojError = (totalPoints > 0)
            ? std::sqrt(totalError / totalPoints) : rmsError;

        // 6. 写入 outputFile（cv::FileStorage 写 yml）
        bool fileSaved = false;
        if (!m_outputFile.isEmpty()) {
            try {
                cv::FileStorage fs(m_outputFile.toStdString(), cv::FileStorage::WRITE);
                if (fs.isOpened()) {
                    fs << "image_width"  << imageSize.width
                       << "image_height" << imageSize.height
                       << "board_width"  << m_boardW
                       << "board_height" << m_boardH
                       << "square_size"  << m_squareSize
                       << "cameraMatrix" << cameraMatrix
                       << "distCoeffs"   << distCoeffs
                       << "reproj_error" << meanReprojError
                       << "image_count"  << validCount;
                    fs.release();
                    fileSaved = true;
                    Logger::info(QString("CameraCalibTool: 标定结果已写入 %1").arg(m_outputFile));
                } else {
                    Logger::warn(QString("CameraCalibTool: 无法写入 %1，仅保留内存结果").arg(m_outputFile));
                }
            } catch (const cv::Exception& e) {
                Logger::error(QString("CameraCalibTool: 写标定文件异常: %1")
                                  .arg(QString::fromStdString(e.what())));
            }
        }

        // 7. 输出结果（ports + data 双通道）
        result.ok = true;
        result.ports["reprojError"] = QVariant(meanReprojError);
        result.ports["fx"] = QVariant(fx);
        result.ports["fy"] = QVariant(fy);
        result.ports["cx"] = QVariant(cx);
        result.ports["cy"] = QVariant(cy);
        result.ports["calibFile"] = QVariant(fileSaved ? m_outputFile : QString());

        result.data["fx"] = fx;
        result.data["fy"] = fy;
        result.data["cx"] = cx;
        result.data["cy"] = cy;
        result.data["reprojError"] = meanReprojError;
        result.data["rmsError"]    = rmsError;
        result.data["imageCount"]  = validCount;
        result.data["calibrated"]  = true;
        result.data["calibFile"]   = fileSaved ? m_outputFile : QString();

        // 相机矩阵 3x3（行优先）
        QJsonArray matArr;
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c) {
                matArr.append(cameraMatrix.at<double>(r, c));
            }
        }
        result.data["cameraMatrix"] = matArr;
        // 畸变系数
        QJsonArray distArr;
        for (size_t i = 0; i < distCoeffs.total(); ++i) {
            distArr.append(distCoeffs.at<double>(static_cast<int>(i)));
        }
        result.data["distCoeffs"] = distArr;

        // 8. overlay：在 input 上绘制检测到的角点 + 状态文字
        //    input 为空时回退到首张标定图（firstOverlay 已含角点）
        const QString statusTxt = QString("Calib OK: %1 imgs, err=%2 px")
            .arg(validCount).arg(meanReprojError, 0, 'f', 3);

        if (!input.empty()) {
            // input → BGR 副本
            cv::Mat overlay;
            if (input.channels() == 3) {
                overlay = input.clone();
            } else if (input.channels() == 1) {
                cv::cvtColor(input, overlay, cv::COLOR_GRAY2BGR);
            } else if (input.channels() == 4) {
                cv::cvtColor(input, overlay, cv::COLOR_BGRA2BGR);
            } else {
                overlay = input.clone();
            }
            // 在 input 上绘制首张成功图检测到的角点
            if (!firstCorners.empty()) {
                cv::drawChessboardCorners(overlay, boardSize, firstCorners, true);
            }
            cv::putText(overlay, statusTxt.toStdString(), cv::Point(10, 30),
                        cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 255, 0), 2);
            result.overlayImage = overlay;
        } else if (!firstOverlay.empty()) {
            cv::putText(firstOverlay, statusTxt.toStdString(), cv::Point(10, 30),
                        cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 255, 0), 2);
            result.overlayImage = firstOverlay;
        }

        Logger::info(QString("CameraCalibTool: 标定完成, %1 张图, fx=%2, fy=%3, cx=%4, cy=%5, err=%6 px")
                         .arg(validCount)
                         .arg(fx, 0, 'f', 2).arg(fy, 0, 'f', 2)
                         .arg(cx, 0, 'f', 2).arg(cy, 0, 'f', 2)
                         .arg(meanReprojError, 0, 'f', 4));
        return true;

    } catch (const cv::Exception& e) {
        Logger::error(QString("CameraCalibTool: OpenCV 异常: %1").arg(QString::fromStdString(e.what())));
        result.ok = false;
        result.data["error"] = QString::fromStdString(e.what());
        cv::Mat black(480, 640, CV_8UC3, cv::Scalar(0, 0, 0));
        cv::putText(black, "calibration exception", cv::Point(20, 240),
                    cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 0, 255), 2);
        result.overlayImage = black;
        return false;
    } catch (const std::exception& e) {
        Logger::error(QString("CameraCalibTool: 标准异常: %1").arg(QString::fromStdString(e.what())));
        result.ok = false;
        result.data["error"] = QString::fromStdString(e.what());
        cv::Mat black(480, 640, CV_8UC3, cv::Scalar(0, 0, 0));
        cv::putText(black, "calibration exception", cv::Point(20, 240),
                    cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 0, 255), 2);
        result.overlayImage = black;
        return false;
    }
}

// =====================================================
// 端口声明（P1-3 typed ports）
// =====================================================
QList<PortDescriptor> CameraCalibTool::outputPorts() const {
    return {
        PortDescriptor{ "calibFile",   "标定文件",     PortType::String, PortDirection::Out, "标定结果文件路径（.yml）" },
        PortDescriptor{ "reprojError", "重投影误差",   PortType::Number, PortDirection::Out, "重投影误差（像素）" },
        PortDescriptor{ "fx",          "fx",           PortType::Number, PortDirection::Out, "焦距 x（像素）" },
        PortDescriptor{ "fy",          "fy",           PortType::Number, PortDirection::Out, "焦距 y（像素）" },
    };
}

QList<PortDescriptor> CameraCalibTool::inputPorts() const {
    return {
        PortDescriptor{ "image", "图像", PortType::Image, PortDirection::In, "输入图像（用于 overlay 可视化）" },
    };
}

// =====================================================
// 序列化 / 反序列化
// =====================================================
QJsonObject CameraCalibTool::serialize() const {
    QJsonObject obj = VisionTool::serialize();
    obj["boardW"]     = m_boardW;
    obj["boardH"]     = m_boardH;
    obj["squareSize"] = m_squareSize;
    obj["imagePaths"] = m_imagePaths;
    obj["outputFile"] = m_outputFile;
    obj["maxImages"]  = m_maxImages;
    return obj;
}

bool CameraCalibTool::deserialize(const QJsonObject& data) {
    if (!VisionTool::deserialize(data)) return false;

    if (data.contains("boardW"))     m_boardW     = std::max(2, data["boardW"].toInt());
    if (data.contains("boardH"))     m_boardH     = std::max(2, data["boardH"].toInt());
    if (data.contains("squareSize")) {
        const double v = data["squareSize"].toDouble();
        m_squareSize = (v > 0.0) ? v : 25.0;
    }
    if (data.contains("imagePaths")) m_imagePaths = data["imagePaths"].toString();
    if (data.contains("outputFile")) m_outputFile = data["outputFile"].toString();
    if (data.contains("maxImages"))  m_maxImages  = std::max(1, data["maxImages"].toInt());
    return true;
}
