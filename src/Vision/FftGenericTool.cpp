#include "FftGenericTool.h"
#include "Core/Logger.h"
#include <opencv2/imgproc.hpp>
#include <opencv2/core.hpp>
#include <algorithm>

using namespace QDV;

FftGenericTool::FftGenericTool() {
    m_name = "FFT变换";
}

bool FftGenericTool::configure(const QJsonObject& params) {
    if (params.contains("mode")) {
        QString m = params["mode"].toString().toLower();
        // 仅允许 forward / inverse，非法值回退为 forward
        if (m == "forward" || m == "inverse") {
            m_mode = m;
        } else {
            m_mode = "forward";
            Logger::warn(QString("FftGenericTool: 非法 mode='%1'，已回退为 forward").arg(params["mode"].toString()));
        }
    }
    if (params.contains("logScale")) {
        m_logScale = params["logScale"].toBool();
    }
    return true;
}

bool FftGenericTool::execute(const cv::Mat& input, ToolResult& result) {
    if (input.empty()) {
        result.ok = false;
        return false;
    }

    // 转灰度
    cv::Mat gray;
    if (input.channels() == 3) {
        cv::cvtColor(input, gray, cv::COLOR_BGR2GRAY);
    } else {
        gray = input.clone();
    }

    // 扩展到最优 DFT 尺寸，提升计算效率
    cv::Mat padded;
    int m = cv::getOptimalDFTSize(gray.rows);
    int n = cv::getOptimalDFTSize(gray.cols);
    cv::copyMakeBorder(gray, padded, 0, m - gray.rows, 0, n - gray.cols,
                       cv::BORDER_CONSTANT, cv::Scalar::all(0));

    // 转 float 并构建双通道 planes[0]=实部, planes[1]=虚部
    cv::Mat planes[] = { cv::Mat_<float>(padded), cv::Mat::zeros(padded.size(), CV_32F) };
    cv::Mat complexI;
    cv::merge(planes, 2, complexI);

    cv::Mat display; // 用于可视化的 8U 图像

    if (m_mode == "forward") {
        // 正变换
        cv::dft(complexI, complexI);

        // 计算幅度谱: magnitude = sqrt(re^2 + im^2)
        cv::split(complexI, planes);
        cv::Mat mag;
        cv::magnitude(planes[0], planes[1], mag);

        // 转对数尺度，便于人眼观察
        if (m_logScale) {
            mag += cv::Scalar::all(1.0);
            cv::log(mag, mag);
        }

        // 归一化到 0~255
        cv::normalize(mag, mag, 0, 255, cv::NORM_MINMAX);
        mag.convertTo(display, CV_8U);

        // 把低频搬到中心（fftshift）
        int cx = display.cols / 2;
        int cy = display.rows / 2;
        cv::Mat q0(display, cv::Rect(0, 0, cx, cy));
        cv::Mat q1(display, cv::Rect(cx, 0, cx, cy));
        cv::Mat q2(display, cv::Rect(0, cy, cx, cy));
        cv::Mat q3(display, cv::Rect(cx, cy, cx, cy));
        cv::Mat tmp;
        q0.copyTo(tmp); q3.copyTo(q0); tmp.copyTo(q3);
        q1.copyTo(tmp); q2.copyTo(q1); tmp.copyTo(q2);
    } else {
        // 逆变换：将输入图像视为频域系数（实部=输入灰度，虚部=0），
        // 直接调用 idft 重建空域图像（DFT_SCALE 做归一化，DFT_REAL_OUTPUT 取实部）。
        // 注意：原实现对输入先 dft 再 idft，等同恒等变换，无实际意义，此处修复为真正的逆变换。
        cv::idft(complexI, complexI, cv::DFT_SCALE | cv::DFT_REAL_OUTPUT);

        // 取实部并归一化为 8U 用于显示
        cv::Mat realPart;
        complexI.convertTo(realPart, CV_32F);
        cv::normalize(realPart, realPart, 0, 255, cv::NORM_MINMAX);
        realPart.convertTo(display, CV_8U);
    }

    // overlayImage 必须为 BGR：左侧原图(转BGR) + 右侧 FFT 结果(转BGR) 并排
    cv::Mat grayBgr, dispBgr;
    cv::cvtColor(gray, grayBgr, cv::COLOR_GRAY2BGR);
    cv::cvtColor(display, dispBgr, cv::COLOR_GRAY2BGR);

    // 尺寸可能不同（DFT 会 padding），统一按各自尺寸并排
    int rows = std::max(grayBgr.rows, dispBgr.rows);
    int cols = grayBgr.cols + dispBgr.cols;
    cv::Mat canvas = cv::Mat::zeros(rows, cols, grayBgr.type());
    cv::Mat left = canvas(cv::Rect(0, 0, grayBgr.cols, grayBgr.rows));
    cv::Mat right = canvas(cv::Rect(grayBgr.cols, 0, dispBgr.cols, dispBgr.rows));
    grayBgr.copyTo(left);
    dispBgr.copyTo(right);
    result.overlayImage = canvas;

    result.ok = true;
    result.score = 1.0;
    result.data["mode"] = m_mode;
    result.data["logScale"] = m_logScale;

    return true;
}

QJsonObject FftGenericTool::serialize() const {
    QJsonObject obj;
    obj["id"] = m_id;
    obj["type"] = type();
    obj["name"] = m_name;
    obj["mode"] = m_mode;
    obj["logScale"] = m_logScale;
    return obj;
}

bool FftGenericTool::deserialize(const QJsonObject& data) {
    // 使用 contains 检查，避免缺失字段被覆盖为默认值
    if (data.contains("id")) m_id = data["id"].toString();
    if (data.contains("name")) m_name = data["name"].toString();
    QJsonObject params;
    if (data.contains("mode"))     params["mode"]     = data["mode"].toString();
    if (data.contains("logScale")) params["logScale"] = data["logScale"].toBool();
    if (!params.isEmpty()) configure(params);
    return true;
}
