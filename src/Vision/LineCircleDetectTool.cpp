#include "LineCircleDetectTool.h"

bool LineCircleDetectTool::configure(const QJsonObject& params) {
    if (!params.contains("detectType")) {
        return false;
    }
    m_detectType = params["detectType"].toString();
    QStringList validTypes = {"line", "circle", "lineP"};
    if (!validTypes.contains(m_detectType)) {
        return false;
    }

    if (params.contains("threshold")) m_threshold = params["threshold"].toInt();
    if (params.contains("rho")) m_rho = params["rho"].toDouble();
    if (params.contains("theta")) m_theta = params["theta"].toDouble();
    if (params.contains("minLineLength")) m_minLineLength = params["minLineLength"].toDouble();
    if (params.contains("maxLineGap")) m_maxLineGap = params["maxLineGap"].toDouble();
    if (params.contains("minRadius")) m_minRadius = params["minRadius"].toDouble();
    if (params.contains("maxRadius")) m_maxRadius = params["maxRadius"].toDouble();
    if (params.contains("dp")) m_dp = params["dp"].toDouble();
    if (params.contains("minDist")) m_minDist = params["minDist"].toDouble();
    if (params.contains("param1")) m_param1 = params["param1"].toDouble();
    if (params.contains("param2")) m_param2 = params["param2"].toDouble();

    if (m_threshold <= 0 || m_minLineLength < 0 || m_maxLineGap < 0 ||
        m_minRadius <= 0 || m_maxRadius <= m_minRadius) {
        return false;
    }

    return true;
}

bool LineCircleDetectTool::execute(const cv::Mat& input, ToolResult& result) {
    result.overlayImage = input.clone();
    result.elapsedMs = 0;

    cv::Mat gray;
    if (input.channels() == 3) {
        cv::cvtColor(input, gray, cv::COLOR_BGR2GRAY);
    } else {
        gray = input.clone();
    }

    cv::Mat edges;
    cv::Canny(gray, edges, 50, 150);

    QJsonArray features;

    if (m_detectType == "line") {
        std::vector<cv::Vec2f> lines;
        cv::HoughLines(edges, lines, m_rho, m_theta, m_threshold);

        for (auto& line : lines) {
            QJsonObject lineObj;
            lineObj["rho"] = line[0];
            lineObj["theta"] = line[1];
            features.append(lineObj);
            float rho = line[0], theta = line[1];
            cv::Point pt1, pt2;
            double a = cos(theta), b = sin(theta);
            double x0 = a * rho, y0 = b * rho;
            pt1.x = cvRound(x0 + 1000 * (-b));
            pt1.y = cvRound(y0 + 1000 * a);
            pt2.x = cvRound(x0 - 1000 * (-b));
            pt2.y = cvRound(y0 - 1000 * a);
            cv::line(result.overlayImage, pt1, pt2, cv::Scalar(0, 255, 0), 2);
        }
    } else if (m_detectType == "lineP") {
        std::vector<cv::Vec4i> linesP;
        cv::HoughLinesP(edges, linesP, m_rho, m_theta, m_threshold,
                        m_minLineLength, m_maxLineGap);
        for (auto& line : linesP) {
            QJsonObject lineObj;
            lineObj["x1"] = line[0];
            lineObj["y1"] = line[1];
            lineObj["x2"] = line[2];
            lineObj["y2"] = line[3];
            features.append(lineObj);
            cv::line(result.overlayImage, cv::Point(line[0], line[1]),
                     cv::Point(line[2], line[3]), cv::Scalar(0, 255, 0), 2);
        }
    } else if (m_detectType == "circle") {
        std::vector<cv::Vec3f> circles;
        cv::HoughCircles(gray, circles, cv::HOUGH_GRADIENT, m_dp, m_minDist,
                         m_param1, m_param2, static_cast<int>(m_minRadius),
                         static_cast<int>(m_maxRadius));
        for (auto& circle : circles) {
            QJsonObject circleObj;
            circleObj["x"] = circle[0];
            circleObj["y"] = circle[1];
            circleObj["radius"] = circle[2];
            features.append(circleObj);
            cv::Point center(cvRound(circle[0]), cvRound(circle[1]));
            int radius = cvRound(circle[2]);
            cv::circle(result.overlayImage, center, radius, cv::Scalar(0, 255, 0), 2);
            cv::circle(result.overlayImage, center, 2, cv::Scalar(0, 0, 255), 3);
        }
    }

    result.data["features"] = features;
    result.data["count"] = features.size();
    result.data["detectType"] = m_detectType.c_str();
    result.score = static_cast<double>(features.size());
    result.ok = (features.size() > 0);

    return true;
}

QJsonObject LineCircleDetectTool::serialize() const {
    QJsonObject obj;
    obj["id"] = m_id;
    obj["type"] = type();
    obj["detectType"] = m_detectType;
    obj["rho"] = m_rho;
    obj["theta"] = m_theta;
    obj["threshold"] = m_threshold;
    obj["minLineLength"] = m_minLineLength;
    obj["maxLineGap"] = m_maxLineGap;
    obj["minRadius"] = m_minRadius;
    obj["maxRadius"] = m_maxRadius;
    return obj;
}

void LineCircleDetectTool::deserialize(const QJsonObject& data) {
    m_id = data["id"].toString();
    m_detectType = data["detectType"].toString("line");
    m_rho = data["rho"].toDouble(1.0);
    m_theta = data["theta"].toDouble(CV_PI / 180.0);
    m_threshold = data["threshold"].toInt(100);
    m_minLineLength = data["minLineLength"].toDouble(50.0);
    m_maxLineGap = data["maxLineGap"].toDouble(10.0);
    m_minRadius = data["minRadius"].toDouble(20.0);
    m_maxRadius = data["maxRadius"].toDouble(200.0);
}