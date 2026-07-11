#include "HandEyeCalibTool.h"
#include "RobotPoseTool.h"      // v5.3：访问全局 PoseBuffer
#include "Core/Logger.h"
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/calib3d.hpp>
#include <QFile>
#include <QTextStream>
#include <QStringList>
#include <QJsonArray>
#include <QRegularExpression>
#include <QDateTime>
#include <vector>
#include <string>

using namespace QDV;

// =====================================================
// 构造
// =====================================================
HandEyeCalibTool::HandEyeCalibTool() {
    m_name = "手眼标定";
}

// =====================================================
// 参数配置
// =====================================================
bool HandEyeCalibTool::configure(const QJsonObject& params) {
    if (params.contains("mode")) {
        const QString v = params["mode"].toString();
        if (v == "eye_in_hand" || v == "eye_to_hand") {
            m_mode = v;
        } else {
            Logger::warn(QString("HandEyeCalibTool: mode '%1' 非法，回退为 'eye_in_hand'").arg(v));
            m_mode = "eye_in_hand";
        }
    }
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
    if (params.contains("poseFilePath")) {
        m_poseFilePath = params["poseFilePath"].toString();
    }
    // v5.3：实时配对模式参数
    if (params.contains("minPairs")) {
        m_minPairs = std::max(3, params["minPairs"].toInt());
    }
    if (params.contains("pairTimeoutMs")) {
        m_pairTimeoutMs = std::max(100, params["pairTimeoutMs"].toInt());
    }
    if (params.contains("maxCacheSize")) {
        m_maxCacheSize = std::max(10, params["maxCacheSize"].toInt());
    }
    return true;
}

// =====================================================
// 重置配对缓存
// =====================================================
void HandEyeCalibTool::resetPairs() {
    m_pairs.clear();
    m_pendingFrames.clear();
    Logger::info("HandEyeCalibTool: 配对缓存已重置");
}

int HandEyeCalibTool::pairedCount() const {
    return m_pairs.size();
}

// =====================================================
// 棋盘格角点检测
// =====================================================
bool HandEyeCalibTool::detectCorners(const cv::Mat& img, std::vector<cv::Point2f>& corners) const {
    if (img.empty()) return false;

    cv::Mat gray;
    if (img.channels() == 3) {
        cv::cvtColor(img, gray, cv::COLOR_BGR2GRAY);
    } else {
        gray = img.clone();
    }

    const cv::Size boardSize(m_boardW, m_boardH);
    const bool found = cv::findChessboardCorners(gray, boardSize, corners,
        cv::CALIB_CB_ADAPTIVE_THRESH + cv::CALIB_CB_NORMALIZE_IMAGE);

    if (!found) return false;

    // 亚像素精细化
    cv::TermCriteria criteria(cv::TermCriteria::EPS + cv::TermCriteria::MAX_ITER, 30, 0.001);
    cv::cornerSubPix(gray, corners, cv::Size(11, 11), cv::Size(-1, -1), criteria);
    return true;
}

// =====================================================
// execute：分发到文件模式或实时配对模式
// =====================================================
bool HandEyeCalibTool::execute(const cv::Mat& input, ToolResult& result) {
    // 文件模式：配置了 imagePaths 时走批量标定
    if (!m_imagePaths.isEmpty()) {
        return executeFileCalib(result);
    }

    // 实时配对模式：从上游帧 + PoseBuffer 配对
    return executeRealtimeCalib(input, result);
}

// =====================================================
// 实时配对标定
// =====================================================
bool HandEyeCalibTool::executeRealtimeCalib(const cv::Mat& input, ToolResult& result) {
    if (input.empty()) {
        result.ok = false;
        result.data["error"] = "realtime mode requires upstream image input";
        cv::Mat black(240, 480, CV_8UC3, cv::Scalar(0, 0, 0));
        cv::putText(black, "No upstream image", cv::Point(10, 120),
                    cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(0, 0, 255), 2);
        result.overlayImage = black;
        return false;
    }

    // 1. 清理超时未配对的位姿（防止旧位姿堆积）
    RobotPoseTool::purgeExpiredPoses(m_pairTimeoutMs);

    // 2. 清理超时的待配对图像帧
    {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        while (!m_pendingFrames.isEmpty()) {
            const PendingFrame& front = m_pendingFrames.head();
            if ((now - front.timestampMs) > m_pairTimeoutMs) {
                Logger::warn(QString("HandEyeCalibTool: 丢弃超时图像帧 frameId=%1").arg(front.frameId));
                m_pendingFrames.dequeue();
            } else {
                break;
            }
        }
    }

    // 3. 检测当前帧的角点
    std::vector<cv::Point2f> corners;
    const bool cornersFound = detectCorners(input, corners);

    // 4. 当前帧存入待配对队列
    PendingFrame frame;
    frame.image = input.clone();
    frame.corners = corners;
    frame.cornersFound = cornersFound;
    frame.frameId = RobotPoseTool::nextFrameId();  // 分配帧号
    frame.timestampMs = QDateTime::currentMSecsSinceEpoch();
    m_pendingFrames.enqueue(frame);

    // 缓存上限保护
    while (m_pendingFrames.size() > m_maxCacheSize) {
        m_pendingFrames.dequeue();
        Logger::warn("HandEyeCalibTool: 待配对帧缓存满，丢弃最旧帧");
    }

    // 5. 尝试配对：从 PoseBuffer 取位姿，与待配对帧 FIFO 配对
    while (!m_pendingFrames.isEmpty() && RobotPoseTool::poseBufferSize() > 0) {
        const PendingFrame& pf = m_pendingFrames.head();
        if (!pf.cornersFound) {
            // 角点未检测到，跳过此帧（丢弃）
            Logger::warn(QString("HandEyeCalibTool: 帧 %1 角点未检测到，丢弃").arg(pf.frameId));
            m_pendingFrames.dequeue();
            continue;
        }

        // 取最旧的位姿（FIFO 配对）
        RobotPoseData pose = RobotPoseTool::popOldestPose();
        if (!pose.valid) break;

        // 位姿 → 旋转矩阵 + 平移向量
        cv::Mat rvec = (cv::Mat_<double>(3, 1) << pose.rx, pose.ry, pose.rz);
        cv::Mat R;
        cv::Rodrigues(rvec, R);
        cv::Mat t = (cv::Mat_<double>(3, 1) << pose.x, pose.y, pose.z);

        CalibPair pair;
        pair.imageCorners = pf.corners;
        pair.R_gripper2base = R.clone();
        pair.t_gripper2base = t.clone();
        pair.frameId = pf.frameId;
        pair.timestampMs = pf.timestampMs;
        m_pairs.append(pair);

        m_pendingFrames.dequeue();  // 配对成功，移出队列

        Logger::info(QString("HandEyeCalibTool: 配对成功 frameId=%1, 已配对 %2/%3")
                         .arg(pair.frameId).arg(m_pairs.size()).arg(m_minPairs));
    }

    // 6. 生成 overlay（显示当前帧 + 配对进度）
    cv::Mat overlay = input.clone();
    if (cornersFound) {
        cv::drawChessboardCorners(overlay, cv::Size(m_boardW, m_boardH), corners, true);
    }
    const QString statusTxt = QString("Paired: %1/%2 | Buffer: %3 | Pending: %4")
        .arg(m_pairs.size()).arg(m_minPairs)
        .arg(RobotPoseTool::poseBufferSize())
        .arg(m_pendingFrames.size());
    cv::putText(overlay, statusTxt.toStdString(), cv::Point(10, 25),
                cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(0, 255, 255), 2);

    // 7. 配对数不足时返回"采集中"状态
    if (m_pairs.size() < m_minPairs) {
        result.ok = true;  // 采集阶段视为成功（非错误）
        result.data["status"] = "collecting";
        result.data["pairedCount"] = m_pairs.size();
        result.data["minPairs"] = m_minPairs;
        result.data["bufferSize"] = RobotPoseTool::poseBufferSize();
        result.data["pendingFrames"] = m_pendingFrames.size();
        result.overlayImage = overlay;
        return true;
    }

    // 8. 配对数达到 minPairs，触发标定
    Logger::info(QString("HandEyeCalibTool: 达到 %1 组配对，开始标定").arg(m_pairs.size()));
    const bool calibOk = runCalibration(m_pairs, input.size(), result);

    // 标定完成后重置配对缓存（下一轮重新采集）
    if (calibOk) {
        Logger::info("HandEyeCalibTool: 标定完成，重置配对缓存");
        resetPairs();
    }

    // 标定结果的 overlay 覆盖采集状态
    if (result.overlayImage.empty()) {
        result.overlayImage = overlay;
    }
    return calibOk;
}

// =====================================================
// 文件模式标定（原有逻辑重构）
// =====================================================
namespace {
// 解析位姿文件：每行 6 个数 x y z rx ry rz（弧度欧拉角）
bool loadPoses(const QString& path,
               std::vector<cv::Mat>& R_vec,
               std::vector<cv::Mat>& t_vec) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
    QTextStream in(&f);
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith('#')) continue;
        const QStringList parts = line.split(QRegularExpression("[\\s,;]+"), Qt::SkipEmptyParts);
        if (parts.size() < 6) continue;

        bool ok = false;
        double vals[6];
        for (int i = 0; i < 6; ++i) {
            vals[i] = parts[i].toDouble(&ok);
            if (!ok) break;
        }
        if (!ok) continue;

        cv::Mat rvec = (cv::Mat_<double>(3, 1) << vals[3], vals[4], vals[5]);
        cv::Mat R;
        cv::Rodrigues(rvec, R);
        cv::Mat t = (cv::Mat_<double>(3, 1) << vals[0], vals[1], vals[2]);
        R_vec.push_back(R);
        t_vec.push_back(t);
    }
    return !R_vec.empty();
}

// 兼容中文路径的图像加载
cv::Mat loadCalibImage(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return cv::Mat();
    const QByteArray data = f.readAll();
    return cv::imdecode(std::vector<uchar>(data.begin(), data.end()), cv::IMREAD_COLOR);
}
} // namespace

bool HandEyeCalibTool::executeFileCalib(ToolResult& result) {
    try {
        // 解析图像路径
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

        // 解析位姿文件
        std::vector<cv::Mat> R_vec, t_vec;
        if (m_poseFilePath.isEmpty() || !loadPoses(m_poseFilePath, R_vec, t_vec)) {
            result.ok = false;
            result.data["error"] = "pose file invalid";
            cv::Mat black(480, 640, CV_8UC3, cv::Scalar(0, 0, 0));
            cv::putText(black, "pose file invalid", cv::Point(20, 240),
                        cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(0, 0, 255), 2);
            result.overlayImage = black;
            return false;
        }

        // 数量校验
        if (static_cast<int>(imgList.size()) != static_cast<int>(R_vec.size())) {
            result.ok = false;
            result.data["error"] = "images/poses count mismatch";
            cv::Mat black(480, 640, CV_8UC3, cv::Scalar(0, 0, 0));
            cv::putText(black, "images/poses count mismatch", cv::Point(20, 240),
                        cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 0, 255), 2);
            result.overlayImage = black;
            return false;
        }

        // 逐图检测角点，构造 CalibPair
        QList<CalibPair> pairs;
        cv::Size imageSize;
        cv::Mat firstImageBGR;

        for (int idx = 0; idx < imgList.size(); ++idx) {
            cv::Mat img = loadCalibImage(imgList[idx]);
            if (img.empty()) {
                Logger::warn(QString("HandEyeCalibTool: 无法加载图像 %1").arg(imgList[idx]));
                continue;
            }
            if (imageSize.area() == 0) imageSize = img.size();

            std::vector<cv::Point2f> corners;
            if (!detectCorners(img, corners)) {
                Logger::warn(QString("HandEyeCalibTool: 角点未找到 %1").arg(imgList[idx]));
                continue;
            }

            CalibPair pair;
            pair.imageCorners = corners;
            pair.R_gripper2base = R_vec[idx].clone();
            pair.t_gripper2base = t_vec[idx].clone();
            pair.frameId = idx;
            pair.timestampMs = 0;
            pairs.append(pair);

            if (firstImageBGR.empty()) {
                firstImageBGR = img.clone();
                cv::drawChessboardCorners(firstImageBGR, cv::Size(m_boardW, m_boardH), corners, true);
            }
        }

        if (pairs.size() < 3) {
            result.ok = false;
            result.data["error"] = "insufficient valid images";
            cv::Mat black(480, 640, CV_8UC3, cv::Scalar(0, 0, 0));
            cv::putText(black, "insufficient valid images", cv::Point(20, 240),
                        cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 0, 255), 2);
            result.overlayImage = black;
            return false;
        }

        // 执行标定
        const bool ok = runCalibration(pairs, imageSize, result);

        // 文件模式下用第一张图 + 角点作为 overlay
        if (!firstImageBGR.empty() && result.overlayImage.empty()) {
            result.overlayImage = firstImageBGR;
        }
        return ok;

    } catch (const cv::Exception& e) {
        Logger::error(QString("HandEyeCalibTool: 标定异常: %1").arg(QString::fromStdString(e.what())));
        result.ok = false;
        result.data["error"] = QString::fromStdString(e.what());
        cv::Mat black(480, 640, CV_8UC3, cv::Scalar(0, 0, 0));
        cv::putText(black, "calibration exception", cv::Point(20, 240),
                    cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 0, 255), 2);
        result.overlayImage = black;
        return false;
    } catch (const std::exception& e) {
        Logger::error(QString("HandEyeCalibTool: 标准异常: %1").arg(QString::fromStdString(e.what())));
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
// 标定计算（共有逻辑）
// =====================================================
bool HandEyeCalibTool::runCalibration(const QList<CalibPair>& pairs, cv::Size imageSize,
                                       ToolResult& result) const {
    if (pairs.size() < 3) {
        result.ok = false;
        result.data["error"] = "insufficient pairs for calibration";
        return false;
    }

    // 构造棋盘格物理坐标
    std::vector<cv::Point3f> objectCorners;
    for (int i = 0; i < m_boardH; ++i) {
        for (int j = 0; j < m_boardW; ++j) {
            objectCorners.emplace_back(
                static_cast<float>(j * m_squareSize),
                static_cast<float>(i * m_squareSize), 0.0f);
        }
    }

    // 聚合 imagePoints / objectPoints / gripper2base
    std::vector<std::vector<cv::Point2f>> imagePoints;
    std::vector<std::vector<cv::Point3f>> objectPoints;
    std::vector<cv::Mat> R_g2b, t_g2b;

    for (const CalibPair& p : pairs) {
        imagePoints.push_back(p.imageCorners);
        objectPoints.push_back(objectCorners);
        R_g2b.push_back(p.R_gripper2base);
        t_g2b.push_back(p.t_gripper2base);
    }

    // 1. 标定相机内参
    cv::Mat cameraMatrix = cv::Mat::eye(3, 3, CV_64F);
    cv::Mat distCoeffs = cv::Mat::zeros(1, 5, CV_64F);
    std::vector<cv::Mat> rvecsCam, tvecsCam;
    cv::calibrateCamera(objectPoints, imagePoints, imageSize,
                        cameraMatrix, distCoeffs, rvecsCam, tvecsCam);

    // 2. 对每组 imagePoints 用 solvePnP 求 target2cam
    std::vector<cv::Mat> R_target2cam, t_target2cam;
    for (size_t i = 0; i < imagePoints.size(); ++i) {
        cv::Mat rvec, tvec;
        if (!cv::solvePnP(objectPoints[i], imagePoints[i],
                          cameraMatrix, distCoeffs, rvec, tvec, false,
                          cv::SOLVEPNP_ITERATIVE)) {
            Logger::warn(QString("HandEyeCalibTool: solvePnP 失败 index=%1").arg(i));
            continue;
        }
        cv::Mat R;
        cv::Rodrigues(rvec, R);
        R_target2cam.push_back(R);
        t_target2cam.push_back(tvec);
    }

    // 配对数校验
    const size_t n = std::min({R_target2cam.size(), R_g2b.size(), t_target2cam.size(), t_g2b.size()});
    if (n < 3) {
        result.ok = false;
        result.data["error"] = "solvePnP insufficient";
        cv::Mat black(480, 640, CV_8UC3, cv::Scalar(0, 0, 0));
        cv::putText(black, "solvePnP insufficient", cv::Point(20, 240),
                    cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 0, 255), 2);
        result.overlayImage = black;
        return false;
    }
    R_target2cam.resize(n);
    t_target2cam.resize(n);
    R_g2b.resize(n);
    t_g2b.resize(n);

    // 3. eye_to_hand 模式：用 base2gripper = inv(gripper2base)
    std::vector<cv::Mat> R_input, t_input;
    if (m_mode == "eye_to_hand") {
        for (size_t i = 0; i < n; ++i) {
            cv::Mat R_b2g = R_g2b[i].inv();
            cv::Mat t_b2g = -R_b2g * t_g2b[i];
            R_input.push_back(R_b2g);
            t_input.push_back(t_b2g);
        }
    } else {
        R_input = R_g2b;
        t_input = t_g2b;
    }

    // 4. 手眼标定
    cv::Mat R_cam2gripper, t_cam2gripper;
    cv::calibrateHandEye(R_input, t_input,
                         R_target2cam, t_target2cam,
                         R_cam2gripper, t_cam2gripper,
                         cv::CALIB_HAND_EYE_PARK);

    // 5. 构造 4x4 齐次矩阵
    cv::Mat H = cv::Mat::eye(4, 4, CV_64F);
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            H.at<double>(r, c) = R_cam2gripper.at<double>(r, c);
        }
        H.at<double>(r, 3) = t_cam2gripper.at<double>(r, 0);
    }

    // 6. 输出结果
    QJsonArray matArr;
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            matArr.append(H.at<double>(r, c));
        }
    }

    result.ok = true;
    result.data["matrix"] = matArr;
    result.data["mode"] = m_mode;
    result.data["numPairs"] = static_cast<qint64>(pairs.size());
    result.data["status"] = "calibrated";
    result.data["cameraMatrix"] = QJsonArray{
        cameraMatrix.at<double>(0, 0), cameraMatrix.at<double>(0, 1), cameraMatrix.at<double>(0, 2),
        cameraMatrix.at<double>(1, 0), cameraMatrix.at<double>(1, 1), cameraMatrix.at<double>(1, 2),
        cameraMatrix.at<double>(2, 0), cameraMatrix.at<double>(2, 1), cameraMatrix.at<double>(2, 2)
    };

    // overlay 提示
    cv::Mat overlay(480, 640, CV_8UC3, cv::Scalar(20, 20, 30));
    const QString txt = QString("HandEye Calib: %1 pairs, mode=%2").arg(pairs.size()).arg(m_mode);
    cv::putText(overlay, txt.toStdString(), cv::Point(10, 30),
                cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 255, 0), 2);
    result.overlayImage = overlay;

    Logger::info(QString("HandEyeCalibTool: 标定完成, %1 组配对, 模式=%2").arg(pairs.size()).arg(m_mode));
    return true;
}

// =====================================================
// 序列化 / 反序列化
// =====================================================
QJsonObject HandEyeCalibTool::serialize() const {
    QJsonObject obj;
    obj["id"] = m_id;
    obj["type"] = type();
    obj["name"] = m_name;
    obj["mode"] = m_mode;
    obj["boardW"] = m_boardW;
    obj["boardH"] = m_boardH;
    obj["squareSize"] = m_squareSize;
    obj["imagePaths"] = m_imagePaths;
    obj["poseFilePath"] = m_poseFilePath;
    obj["minPairs"] = m_minPairs;
    obj["pairTimeoutMs"] = m_pairTimeoutMs;
    obj["maxCacheSize"] = m_maxCacheSize;
    return obj;
}

bool HandEyeCalibTool::deserialize(const QJsonObject& data) {
    if (data.contains("id")) m_id = data["id"].toString();
    if (data.contains("name")) m_name = data["name"].toString();
    if (data.contains("mode")) {
        const QString v = data["mode"].toString();
        if (v == "eye_in_hand" || v == "eye_to_hand") m_mode = v;
    }
    if (data.contains("boardW")) m_boardW = std::max(2, data["boardW"].toInt());
    if (data.contains("boardH")) m_boardH = std::max(2, data["boardH"].toInt());
    if (data.contains("squareSize")) {
        const double v = data["squareSize"].toDouble();
        m_squareSize = (v > 0.0) ? v : 25.0;
    }
    if (data.contains("imagePaths")) m_imagePaths = data["imagePaths"].toString();
    if (data.contains("poseFilePath")) m_poseFilePath = data["poseFilePath"].toString();
    if (data.contains("minPairs")) m_minPairs = std::max(3, data["minPairs"].toInt());
    if (data.contains("pairTimeoutMs")) m_pairTimeoutMs = std::max(100, data["pairTimeoutMs"].toInt());
    if (data.contains("maxCacheSize")) m_maxCacheSize = std::max(10, data["maxCacheSize"].toInt());
    return true;
}
