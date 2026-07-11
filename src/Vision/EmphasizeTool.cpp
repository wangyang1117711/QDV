#include "EmphasizeTool.h"
#include "Core/Logger.h"
#include <opencv2/imgproc.hpp>
#include <algorithm>

using namespace QDV;

EmphasizeTool::EmphasizeTool() {
    m_name = "锐化增强";
}

bool EmphasizeTool::configure(const QJsonObject& params) {
    if (params.contains("kernelSize")) {
        // 防御性校验：kernelSize 必须 >= 1 且为奇数，否则 cv::GaussianBlur 会断言失败
        int ks = params["kernelSize"].toInt();
        if (ks < 1) {
            ks = 1;
            Logger::warn("EmphasizeTool: kernelSize < 1，已钳制为 1");
        } else if (ks > 31) {
            ks = 31;
            Logger::warn("EmphasizeTool: kernelSize > 31，已钳制为 31");
        } else if (ks % 2 == 0) {
            // 偶数 → 强制 +1 变奇数
            ks += 1;
            Logger::warn(QString("EmphasizeTool: kernelSize 为偶数 %1，已调整为奇数 %2")
                         .arg(ks - 1).arg(ks));
        }
        m_kernelSize = ks;
    }
    if (params.contains("amount")) {
        // 锐化强度范围 0~20，超出则钳制
        double a = params["amount"].toDouble();
        if (a < 0.0) {
            a = 0.0;
            Logger::warn("EmphasizeTool: amount < 0，已钳制为 0");
        } else if (a > 20.0) {
            a = 20.0;
            Logger::warn("EmphasizeTool: amount > 20，已钳制为 20");
        }
        m_amount = a;
    }
    return true;
}

bool EmphasizeTool::execute(const cv::Mat& input, ToolResult& result) {
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

    // 运行时双保险：确保 kernelSize 合法
    int ks = m_kernelSize;
    if (ks < 1) ks = 1;
    if (ks > 31) ks = 31;
    if (ks % 2 == 0) ks += 1;

    // 1) 先做高斯模糊
    cv::Mat blurred;
    cv::GaussianBlur(gray, blurred, cv::Size(ks, ks), 0, 0);

    // 2) 反锐化掩模：result = input * (1 + amount/100) - blurred * (amount/100)
    //    使用 CV_64F 中间结果防止截断，最终再转回 CV_8U
    cv::Mat grayF, blurredF, sharpenedF;
    gray.convertTo(grayF, CV_64F);
    blurred.convertTo(blurredF, CV_64F);

    double k = m_amount / 100.0;
    sharpenedF = grayF * (1.0 + k) - blurredF * k;

    cv::Mat sharpened;
    sharpenedF.convertTo(sharpened, CV_8U);

    // overlayImage 必须为 BGR：左侧原图(转BGR) + 右侧锐化结果(转BGR) 并排
    cv::Mat grayBgr, sharpenedBgr;
    cv::cvtColor(gray, grayBgr, cv::COLOR_GRAY2BGR);
    cv::cvtColor(sharpened, sharpenedBgr, cv::COLOR_GRAY2BGR);

    cv::Mat canvas(grayBgr.rows, grayBgr.cols + sharpenedBgr.cols, grayBgr.type());
    cv::Mat left = canvas(cv::Rect(0, 0, grayBgr.cols, grayBgr.rows));
    cv::Mat right = canvas(cv::Rect(grayBgr.cols, 0, sharpenedBgr.cols, sharpenedBgr.rows));
    grayBgr.copyTo(left);
    sharpenedBgr.copyTo(right);
    result.overlayImage = canvas;

    result.ok = true;
    result.score = 1.0;
    result.data["kernelSize"] = ks;
    result.data["amount"] = m_amount;

    return true;
}

QJsonObject EmphasizeTool::serialize() const {
    QJsonObject obj;
    obj["id"] = m_id;
    obj["type"] = type();
    obj["name"] = m_name;
    obj["kernelSize"] = m_kernelSize;
    obj["amount"] = m_amount;
    return obj;
}

bool EmphasizeTool::deserialize(const QJsonObject& data) {
    // 使用 contains 检查，避免缺失字段被覆盖为默认值
    if (data.contains("id")) m_id = data["id"].toString();
    if (data.contains("name")) m_name = data["name"].toString();
    if (data.contains("kernelSize")) {
        QJsonObject params;
        params["kernelSize"] = data["kernelSize"].toInt();
        // amount 同步传入 configure，避免单独维护
        if (data.contains("amount")) params["amount"] = data["amount"].toDouble();
        configure(params);
    } else if (data.contains("amount")) {
        QJsonObject params;
        params["amount"] = data["amount"].toDouble();
        configure(params);
    }
    return true;
}
