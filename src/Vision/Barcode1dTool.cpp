#include "Barcode1dTool.h"
#include "Core/Logger.h"
#include <opencv2/imgproc.hpp>
#include <QString>
#include <QStringList>
#include <vector>
#include <string>

using namespace QDV;

Barcode1dTool::Barcode1dTool() {
    m_name = "一维码识别";
}

bool Barcode1dTool::configure(const QJsonObject& params) {
    if (params.contains("format")) {
        m_format = params["format"].toString();
    }
    m_params = params;
    return true;
}

namespace {
// 从二值图中部若干行扫描，统计连续黑/白段宽度序列
// 返回条空宽度交替序列，第一个元素为黑条宽度
std::vector<int> scanBarWidthes(const cv::Mat& binary) {
    std::vector<int> widths;
    if (binary.empty()) return widths;
    const int mid = binary.rows / 2;
    const int lo = std::max(0, mid - 5);
    const int hi = std::min(binary.rows - 1, mid + 5);

    // 累加多行投影到一行（取每列像素均值）
    cv::Mat rowProf(1, binary.cols, CV_32SC1, cv::Scalar(0));
    for (int y = lo; y <= hi; ++y) {
        const uchar* p = binary.ptr<uchar>(y);
        for (int x = 0; x < binary.cols; ++x) {
            // 黑(0)记1，白(255)记0
            rowProf.at<int>(0, x) += (p[x] == 0) ? 1 : 0;
        }
    }
    // 阈值：超过半数行黑即视为黑
    const int rowCnt = hi - lo + 1;
    const int halfTh = rowCnt / 2;

    // 统计连续段
    bool prevBlack = (rowProf.at<int>(0, 0) > halfTh);
    int runLen = 1;
    for (int x = 1; x < binary.cols; ++x) {
        const bool black = (rowProf.at<int>(0, x) > halfTh);
        if (black == prevBlack) {
            ++runLen;
        } else {
            widths.push_back(runLen);
            prevBlack = black;
            runLen = 1;
        }
    }
    widths.push_back(runLen);
    return widths;
}

// EAN-13 编码表
// L 码（奇校验，左侧6位左侧组，编码模式由首位决定）
// G 码（偶校验）
// R 码（右侧6位，奇校验，与 L 互补）
// 每个数字用 7 模块表示，格式 SBS（space-bar-space-bar）
// 数组索引 0..9 对应数字 0..9
const char* kLCode[10] = {
    "0001101", "0011001", "0010011", "0111101", "0100011",
    "0110001", "0101111", "0111011", "0110111", "0001011"
};
const char* kGCode[10] = {
    "0100111", "0110011", "0011011", "0100001", "0011101",
    "0111001", "0000101", "0010001", "0001001", "0010111"
};
const char* kRCode[10] = {
    "1110010", "1100110", "1101100", "1000010", "1011100",
    "1001110", "1010000", "1000100", "1001000", "1110100"
};
// 起始/终止 = "101"，中分隔 = "01010"

// 首位决定左侧 6 位使用 L/G 编码模式
// 0=LLLLLL, 1=LLGLGG, 2=LLGGLG, 3=LLGGGL, 4=LGLLGG,
// 5=LGGLLL, 6=LGGGLL, 7=LGLGLG, 8=LGLGGL, 9=LGGLGL
const char* kFirstDigitPattern[10] = {
    "LLLLLL", "LLGLGG", "LLGGLG", "LLGGGL", "LGLLGG",
    "LGGLLL", "LGGGLL", "LGLGLG", "LGLGGL", "LGGLGL"
};

// 把 7 模块 bit 串匹配到 L/G/R 码表，返回匹配的数字或 -1
int matchDigitCode(const std::string& s, const char* table[10]) {
    for (int d = 0; d < 10; ++d) {
        if (s == table[d]) return d;
    }
    return -1;
}

// 简化 EAN-13 解码：输入条空宽度序列（从首个黑条开始），
// 把宽度序列归一化到 95 模块，按结构起始3+左6*7+中5+右6*7+终止3 解码
QString decodeEan13(const std::vector<int>& widths) {
    // 总模块数 95，至少需要 59 段（3+42+5+42+3 = 95 个模块 = 4 个段一组，
    // 实际段数为 3+6*4+5+6*4+3 = 59 段（每段代表一个模块宽度的连续 run）
    // 起始3模块=2段(BS), 左数据6*7=6*4=24段, 中分隔5模块=4段, 右数据24段, 终止3模块=2段 = 56段
    if (widths.size() < 56) return QString();

    // 计算总宽度（前 59 个段的累加作为参考模块宽度估算）
    // 实际应取整张条码 95 模块的总宽度，这里取前 59 段
    long totalWidth = 0;
    const int segCount = std::min(static_cast<int>(widths.size()), 59);
    for (int i = 0; i < segCount; ++i) totalWidth += widths[i];
    if (totalWidth <= 0) return QString();
    const double unitW = static_cast<double>(totalWidth) / 95.0;
    if (unitW < 0.5) return QString();

    // 把每段宽度转换为模块数（四舍五入，至少为1）
    std::vector<int> modules(widths.size());
    for (size_t i = 0; i < widths.size(); ++i) {
        int m = static_cast<int>(std::round(widths[i] / unitW));
        if (m < 1) m = 1;
        modules[i] = m;
    }

    // 验证起始符：3 模块，模式 "101"（黑1 白1 黑1）→ 段 [1,1,1]
    // 取前 3 段应为 [1,1,1]
    int idx = 0;
    if (modules[0] != 1 || modules[1] != 1 || modules[2] != 1) return QString();
    idx = 3;

    // 左侧 6 位数据，每位 7 模块 4 段
    std::string leftBits;
    int leftDigits[6] = {0, 0, 0, 0, 0, 0};
    for (int d = 0; d < 6; ++d) {
        if (idx + 4 > static_cast<int>(modules.size())) return QString();
        // 7 模块 = 4 段，按段模块数展开为 0/1 串（黑=1，白=0）
        std::string s;
        const bool startBlack = ((idx % 2) == 0); // idx=3 奇数，起始为白？这里以黑开始
        // 第一个数据位从黑开始（起始符后接黑）
        // 起始符 3 段：黑白黑，共 3 段（idx=0,1,2），下一段 idx=3 为白
        // 但 EAN-13 左侧数据从黑开始，所以起始符后第一段应为黑
        // 起始符 "101" = 黑1 白1 黑1 → 3 段，但实际宽度序列首段为黑，
        // 起始符占 3 段（1,1,1），下一段 idx=3 为白
        // 这里需要重新对齐：实际扫描时第一段为黑，所以 idx=0 黑,1 白,2 黑 = 起始
        // idx=3 开始为白，但 EAN-13 左数据位以黑开始
        // 处理：跳过起始的奇偶对齐，直接按 4 段 1 组读取模块模式
        bool curBlack = true; // 左数据每 7 模块以黑开始
        for (int seg = 0; seg < 4; ++seg) {
            const int m = modules[idx + seg];
            for (int b = 0; b < m; ++b) {
                s.push_back(curBlack ? '1' : '0');
            }
            curBlack = !curBlack;
        }
        // 截取前 7 位
        if (s.size() < 7) return QString();
        s = s.substr(0, 7);
        // 先尝试 L 码
        int digit = matchDigitCode(s, kLCode);
        char codeType = 'L';
        if (digit < 0) {
            digit = matchDigitCode(s, kGCode);
            codeType = 'G';
        }
        if (digit < 0) return QString();
        leftDigits[d] = digit;
        leftBits.push_back(codeType);
        idx += 4;
    }

    // 由左侧 L/G 模式推断首位数字
    int firstDigit = -1;
    for (int d = 0; d < 10; ++d) {
        if (leftBits == kFirstDigitPattern[d]) {
            firstDigit = d;
            break;
        }
    }
    if (firstDigit < 0) return QString();

    // 中分隔 "01010" = 5 模块，4 段（白黑黑白）从 idx 开始
    // 中分隔前一段为白，所以 idx 处应为白
    if (idx + 4 > static_cast<int>(modules.size())) return QString();
    // 简化校验：期望 modules[idx..idx+3] = [1,1,1,1]
    if (modules[idx] != 1 || modules[idx + 1] != 1 ||
        modules[idx + 2] != 1 || modules[idx + 3] != 1) return QString();
    idx += 4;

    // 右侧 6 位数据，R 码
    int rightDigits[6] = {0, 0, 0, 0, 0, 0};
    for (int d = 0; d < 6; ++d) {
        if (idx + 4 > static_cast<int>(modules.size())) return QString();
        std::string s;
        bool curBlack = true;
        for (int seg = 0; seg < 4; ++seg) {
            const int m = modules[idx + seg];
            for (int b = 0; b < m; ++b) {
                s.push_back(curBlack ? '1' : '0');
            }
            curBlack = !curBlack;
        }
        if (s.size() < 7) return QString();
        s = s.substr(0, 7);
        const int digit = matchDigitCode(s, kRCode);
        if (digit < 0) return QString();
        rightDigits[d] = digit;
        idx += 4;
    }

    // 终止符 3 模块 "101"
    if (idx + 2 > static_cast<int>(modules.size())) return QString();
    if (modules[idx] != 1 || modules[idx + 1] != 1) return QString();

    // 校验位计算（EAN-13 第 13 位为校验位）
    int digits[13];
    digits[0] = firstDigit;
    for (int i = 0; i < 6; ++i) digits[1 + i] = leftDigits[i];
    for (int i = 0; i < 6; ++i) digits[7 + i] = rightDigits[i];
    int sum = 0;
    for (int i = 0; i < 12; ++i) {
        sum += (i % 2 == 0) ? digits[i] : digits[i] * 3;
    }
    const int check = (10 - (sum % 10)) % 10;
    if (check != digits[12]) return QString();

    std::string s;
    for (int i = 0; i < 13; ++i) s.push_back('0' + digits[i]);
    return QString::fromStdString(s);
}
} // namespace

bool Barcode1dTool::execute(const cv::Mat& input, ToolResult& result) {
    if (input.empty()) {
        result.ok = false;
        return false;
    }

    // overlay 必须为 BGR
    cv::Mat overlay;
    if (input.channels() == 1) {
        cv::cvtColor(input, overlay, cv::COLOR_GRAY2BGR);
    } else {
        overlay = input.clone();
    }

    // 转灰度
    cv::Mat gray;
    if (input.channels() == 3) {
        cv::cvtColor(input, gray, cv::COLOR_BGR2GRAY);
    } else {
        gray = input.clone();
    }

    // 中部行自适应二值化
    cv::Mat binary;
    const int blockSize = 31;
    const double C = 10.0;
    try {
        cv::adaptiveThreshold(gray, binary, 255,
                              cv::ADAPTIVE_THRESH_GAUSSIAN_C,
                              cv::THRESH_BINARY, blockSize, C);
    } catch (const cv::Exception& e) {
        Logger::warn(QString("Barcode1dTool: adaptiveThreshold 异常: %1")
            .arg(QString::fromStdString(e.what())));
        result.overlayImage = overlay;
        result.ok = true;
        result.data["text"] = "";
        result.data["format"] = "EAN13";
        result.data["detected"] = false;
        return true;
    }

    // 扫描条空宽度
    const std::vector<int> widths = scanBarWidthes(binary);
    if (widths.size() < 56) {
        // 段数不足，直接返回未识别
        cv::putText(overlay, "no barcode", cv::Point(10, 30),
                    cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 0, 255), 2);
        result.overlayImage = overlay;
        result.ok = true;
        result.data["text"] = "";
        result.data["format"] = "EAN13";
        result.data["detected"] = false;
        return true;
    }

    // 在条码区域画绿色矩形框（用整个图的中部行高度作为近似 bbox）
    const int midY = gray.rows / 2;
    cv::Rect bbox(0, std::max(0, midY - 20), gray.cols, std::min(40, gray.rows));
    cv::rectangle(overlay, bbox, cv::Scalar(0, 255, 0), 2);

    // 尝试 EAN-13 解码
    const QString decoded = decodeEan13(widths);
    const bool ok = !decoded.isEmpty();

    if (ok) {
        cv::putText(overlay, decoded.toStdString(), cv::Point(10, 30),
                    cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(0, 255, 0), 2);
    } else {
        cv::putText(overlay, "unrecognized", cv::Point(10, 30),
                    cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 200, 255), 2);
    }

    result.overlayImage = overlay;
    result.ok = true;  // 算子执行成功，无论是否识别到
    result.data["text"] = decoded;
    result.data["format"] = "EAN13";
    result.data["detected"] = ok;
    return true;
}

QJsonObject Barcode1dTool::serialize() const {
    QJsonObject obj;
    obj["id"] = m_id;
    obj["type"] = type();
    obj["name"] = m_name;
    obj["format"] = m_format;
    return obj;
}

bool Barcode1dTool::deserialize(const QJsonObject& data) {
    if (data.contains("id")) m_id = data["id"].toString();
    if (data.contains("name")) m_name = data["name"].toString();
    if (data.contains("format")) m_format = data["format"].toString();
    return true;
}
