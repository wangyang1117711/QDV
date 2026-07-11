#include "RegionGrowingTool.h"
#include <opencv2/imgproc.hpp>
#include <queue>
#include <cstdlib>
#include <algorithm>

using namespace QDV;

RegionGrowingTool::RegionGrowingTool() {
    m_name = "区域生长";
}

bool RegionGrowingTool::configure(const QJsonObject& params) {
    if (params.contains("seedX")) m_seedX = params["seedX"].toInt();
    if (params.contains("seedY")) m_seedY = params["seedY"].toInt();
    if (params.contains("threshold")) m_threshold = params["threshold"].toDouble();
    if (params.contains("connectivity")) {
        // 仅接受合法连通性，非法值保持默认
        int c = params["connectivity"].toInt();
        if (c == 4 || c == 8) {
            m_connectivity = c;
        }
    }

    // 钳制灰度差阈值到有效范围
    m_threshold = std::clamp(m_threshold, 0.0, 255.0);

    return true;
}

bool RegionGrowingTool::execute(const cv::Mat& input, ToolResult& result) {
    if (input.empty()) {
        result.ok = false;
        return false;
    }

    // 种子点边界校验
    if (m_seedX < 0 || m_seedX >= input.cols ||
        m_seedY < 0 || m_seedY >= input.rows) {
        result.ok = false;
        result.data["error"] = QStringLiteral("种子点超出图像范围");
        return false;
    }

    // 转灰度用于灰度差判断
    cv::Mat gray;
    if (input.channels() == 3) {
        cv::cvtColor(input, gray, cv::COLOR_BGR2GRAY);
    } else {
        gray = input.clone();
    }

    // 以种子点灰度值作为生长基准
    uchar seedValue = gray.at<uchar>(m_seedY, m_seedX);
    double thresh = m_threshold;

    // 生长掩码：255 表示已归入生长区域
    cv::Mat mask = cv::Mat::zeros(gray.size(), CV_8U);

    // 4/8 邻域偏移
    static const int dx4[] = {1, -1, 0, 0};
    static const int dy4[] = {0, 0, 1, -1};
    static const int dx8[] = {1, -1, 0, 0, 1, 1, -1, -1};
    static const int dy8[] = {0, 0, 1, -1, 1, -1, 1, -1};
    const int* dx = (m_connectivity == 4) ? dx4 : dx8;
    const int* dy = (m_connectivity == 4) ? dy4 : dy8;
    int nCount = (m_connectivity == 4) ? 4 : 8;

    // BFS 区域生长：相邻像素与种子点灰度差小于阈值则归入同一区域
    std::queue<cv::Point> q;
    q.push(cv::Point(m_seedX, m_seedY));
    mask.at<uchar>(m_seedY, m_seedX) = 255;

    int grownPixels = 0;
    while (!q.empty()) {
        cv::Point p = q.front();
        q.pop();
        ++grownPixels;

        for (int i = 0; i < nCount; ++i) {
            int nx = p.x + dx[i];
            int ny = p.y + dy[i];
            if (nx < 0 || nx >= gray.cols || ny < 0 || ny >= gray.rows) continue;
            if (mask.at<uchar>(ny, nx) != 0) continue;

            uchar nv = gray.at<uchar>(ny, nx);
            if (std::abs(static_cast<int>(nv) - static_cast<int>(seedValue)) < thresh) {
                mask.at<uchar>(ny, nx) = 255;
                q.push(cv::Point(nx, ny));
            }
        }
    }

    // overlayImage 必须为 BGR 格式：在原图上以绿色半透明标注生长区域
    if (input.channels() == 1) {
        cv::cvtColor(input, result.overlayImage, cv::COLOR_GRAY2BGR);
    } else {
        result.overlayImage = input.clone();
    }
    cv::Mat coloredMask;
    cv::cvtColor(mask, coloredMask, cv::COLOR_GRAY2BGR);
    coloredMask.setTo(cv::Scalar(0, 255, 0), mask == 255);
    cv::addWeighted(result.overlayImage, 0.6, coloredMask, 0.4, 0, result.overlayImage);

    result.ok = true;
    result.score = static_cast<double>(grownPixels) / (gray.rows * gray.cols);
    result.data["seedX"] = m_seedX;
    result.data["seedY"] = m_seedY;
    result.data["threshold"] = m_threshold;
    result.data["connectivity"] = m_connectivity;
    result.data["grownPixels"] = grownPixels;

    return true;
}

QJsonObject RegionGrowingTool::serialize() const {
    QJsonObject obj;
    obj["id"] = m_id;
    obj["type"] = type();
    obj["name"] = m_name;
    obj["seedX"] = m_seedX;
    obj["seedY"] = m_seedY;
    obj["threshold"] = m_threshold;
    obj["connectivity"] = m_connectivity;
    return obj;
}

bool RegionGrowingTool::deserialize(const QJsonObject& data) {
    if (data.contains("id")) m_id = data["id"].toString();
    if (data.contains("name")) m_name = data["name"].toString();
    if (data.contains("seedX")) m_seedX = data["seedX"].toInt();
    if (data.contains("seedY")) m_seedY = data["seedY"].toInt();
    if (data.contains("threshold")) m_threshold = data["threshold"].toDouble();
    if (data.contains("connectivity")) {
        int c = data["connectivity"].toInt();
        if (c == 4 || c == 8) {
            m_connectivity = c;
        }
    }

    // 反序列化后同样钳制，保证状态一致
    m_threshold = std::clamp(m_threshold, 0.0, 255.0);

    return true;
}
