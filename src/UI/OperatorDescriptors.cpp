#include "UI/OperatorDescriptors.h"
#include "Core/Logger.h"
// P1-A11 修复：引入 ToolFactory 做交叉校验，检测 operators.json 中的幽灵算子
#include "Vision/ToolFactory.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QCoreApplication>
#include <QMap>
#include <algorithm>

namespace QDV {
namespace UI {

// ========================================================================
// ParamSpec 序列化（QVariantMap 形式便于 QML 端 JS 访问）
// ========================================================================

QVariantMap ParamSpec::toMap() const {
    QVariantMap m;
    m["name"]         = name;
    m["cnName"]       = cnName;
    m["type"]         = static_cast<int>(type);   // QML 端用 int 触发 Loader 切换
    m["defaultValue"] = defaultValue;
    m["minValue"]     = minValue;
    m["maxValue"]     = maxValue;
    m["step"]         = step;
    m["options"]      = options;
    m["optionKeys"]   = optionKeys;
    m["help"]         = help;
    m["unit"]         = unit;
    return m;
}

ParamSpec ParamSpec::fromMap(const QVariantMap& m) {
    ParamSpec p;
    p.name         = m.value("name").toString();
    p.cnName       = m.value("cnName").toString();
    p.type         = static_cast<ParamType>(m.value("type").toInt());
    p.defaultValue = m.value("defaultValue");
    p.minValue     = m.value("minValue");
    p.maxValue     = m.value("maxValue");
    p.step         = m.value("step");
    p.options      = m.value("options").toStringList();
    p.optionKeys   = m.value("optionKeys").toStringList();
    p.help         = m.value("help").toString();
    p.unit         = m.value("unit").toString();
    return p;
}

// ========================================================================
// OperatorMeta 序列化
// ========================================================================

QVariantMap OperatorMeta::toMap() const {
    QVariantMap m;
    m["type"]        = type;
    m["cnName"]      = cnName;
    m["category"]    = category;
    m["subGroup"]    = subGroup;     // v2.2.0：子分组
    m["iconPath"]    = iconPath;
    m["description"] = description;
    QVariantList paramsList;
    for (const ParamSpec& p : params) {
        paramsList.append(p.toMap());
    }
    m["params"] = paramsList;
    // v3.0.0：输出参数列表
    QVariantList outputsList;
    for (const QVariantMap& o : outputs) {
        outputsList.append(o);
    }
    m["outputs"] = outputsList;
    return m;
}

OperatorMeta OperatorMeta::fromMap(const QVariantMap& m) {
    OperatorMeta om;
    om.type        = m.value("type").toString();
    om.cnName      = m.value("cnName").toString();
    om.category    = m.value("category").toString();
    om.subGroup    = m.value("subGroup").toString();  // v2.2.0：子分组
    om.iconPath    = m.value("iconPath").toString();
    om.description = m.value("description").toString();
    const QVariantList paramsList = m.value("params").toList();
    for (const QVariant& v : paramsList) {
        om.params.append(ParamSpec::fromMap(v.toMap()));
    }
    // v3.0.0：输出参数列表
    const QVariantList outputsList = m.value("outputs").toList();
    for (const QVariant& v : outputsList) {
        if (v.typeId() == QMetaType::QVariantMap) {
            om.outputs.append(v.toMap());
        }
    }
    return om;
}

// ========================================================================
// 单例注册表
// ========================================================================

QList<OperatorMeta> OperatorDescriptors::s_registry;
bool                OperatorDescriptors::s_initialized = false;
QList<OperatorMeta> OperatorDescriptors::s_externalRegistry;  // Phase 2: 外部动态算子

QList<OperatorMeta> OperatorDescriptors::buildRegistry() {
    QList<OperatorMeta> reg;

    // ---------- 输入 ----------
    {
        OperatorMeta om;
        om.type        = "ReadImage";
        om.cnName      = "读图";
        om.category    = "输入";
        om.iconPath    = "qrc:/icons/readimage.svg";
        om.description = "从本地路径加载图像到方案（支持 png/jpg/bmp/tiff 等）";
        om.params.append({"filePath", "文件路径", ParamType::String,
                          QStringLiteral(""),
                          QVariant(), QVariant(), QVariant(),
                          {}, {},
                          QStringLiteral("支持 png/jpg/jpeg/bmp/tiff/tif/webp 等格式，最大 100MB"),
                          QStringLiteral("")});
        om.params.append({"colorMode", "颜色模式", ParamType::Enum,
                          QStringLiteral("color"),
                          QVariant(), QVariant(), QVariant(),
                          {QStringLiteral("原样"), QStringLiteral("灰度"), QStringLiteral("彩色")},
                          {"unchanged", "grayscale", "color"},
                          QStringLiteral("IMREAD_UNCHANGED/GRAYSCALE/COLOR"),
                          QStringLiteral("")});
        reg.append(om);
    }

    // ---------- 预处理 ----------
    {
        OperatorMeta om;
        om.type        = "ImagePreprocess";
        om.cnName      = "图像预处理";
        om.category    = "预处理";
        om.iconPath    = "qrc:/icons/preprocess.svg";
        om.description = "去噪 + 形态学操作（开/闭/腐蚀/膨胀）";
        om.params.append({"denoise", "双边滤波去噪", ParamType::Bool,
                          false,
                          QVariant(), QVariant(), QVariant(),
                          {}, {},
                          QStringLiteral("使用 cv::bilateralFilter，强度 d=9 / sigma=75"),
                          QStringLiteral("")});
        {
            ParamSpec p;
            p.name     = "morphology";
            p.cnName   = "形态学操作";
            p.type     = ParamType::Enum;
            p.defaultValue = QStringLiteral("none");
            p.options  = {QStringLiteral("无"), QStringLiteral("开运算"),
                          QStringLiteral("闭运算"), QStringLiteral("腐蚀"),
                          QStringLiteral("膨胀")};
            p.optionKeys = {"none", "open", "close", "erode", "dilate"};
            p.help     = QStringLiteral("开：去小白噪；闭：填小黑洞；腐蚀/膨胀：形态学基础");
            om.params.append(p);
        }
        om.params.append({"kernelSize", "核大小", ParamType::Int,
                          3,
                          1, 31, 2,
                          {}, {},
                          QStringLiteral("形态学核尺寸（奇数，推荐 3-7）"),
                          QStringLiteral("px")});
        reg.append(om);
    }
    {
        OperatorMeta om;
        om.type        = "Threshold";
        om.cnName      = "阈值分割";
        om.category    = "预处理";
        om.iconPath    = "qrc:/icons/threshold.svg";
        om.description = "灰度阈值化（BINARY / BINARY_INV / TRUNC / TOZERO / TOZERO_INV）";
        om.params.append({"threshold", "阈值", ParamType::Float,
                          128.0,
                          0.0, 255.0, 1.0,
                          {}, {},
                          QStringLiteral("分割阈值（0-255）"),
                          QStringLiteral("")});
        om.params.append({"maxValue", "最大值", ParamType::Float,
                          255.0,
                          0.0, 255.0, 1.0,
                          {}, {},
                          QStringLiteral("二值化后的最大像素值"),
                          QStringLiteral("")});
        {
            ParamSpec p;
            p.name     = "method";
            p.cnName   = "方法";
            p.type     = ParamType::Enum;
            p.defaultValue = QStringLiteral("BINARY");
            p.options  = {QStringLiteral("二值"), QStringLiteral("反向二值"),
                          QStringLiteral("截断"), QStringLiteral("归零"),
                          QStringLiteral("反向归零")};
            p.optionKeys = {"BINARY", "BINARY_INV", "TRUNC", "TOZERO", "TOZERO_INV"};
            p.help     = QStringLiteral("OpenCV 阈值化方法");
            om.params.append(p);
        }
        reg.append(om);
    }
    {
        OperatorMeta om;
        om.type        = "EdgeDetect";
        om.cnName      = "边缘检测";
        om.category    = "预处理";
        om.iconPath    = "qrc:/icons/edge.svg";
        om.description = "Canny 边缘检测（双阈值 + Sobel 核大小）";
        om.params.append({"lowThreshold", "低阈值", ParamType::Int,
                          50,
                          0, 500, 10,
                          {}, {},
                          QStringLiteral("Canny 低阈值"),
                          QStringLiteral("")});
        om.params.append({"highThreshold", "高阈值", ParamType::Int,
                          150,
                          0, 500, 10,
                          {}, {},
                          QStringLiteral("Canny 高阈值（建议为低阈值的 2-3 倍）"),
                          QStringLiteral("")});
        om.params.append({"apertureSize", "Sobel 核", ParamType::Int,
                          3,
                          3, 7, 2,
                          {}, {},
                          QStringLiteral("Sobel 算子核大小（3/5/7）"),
                          QStringLiteral("")});
        reg.append(om);
    }
    {
        OperatorMeta om;
        om.type        = "ColorDetect";
        om.cnName      = "颜色检测";
        om.category    = "预处理";
        om.iconPath    = "qrc:/icons/color.svg";
        om.description = "HSV 颜色空间阈值分割（适合检测特定颜色目标）";
        om.params.append({"hMin", "色相最小", ParamType::Int,
                          0, 0, 179, 1, {}, {},
                          QStringLiteral("HSV 颜色空间色相下界"), ""});
        om.params.append({"hMax", "色相最大", ParamType::Int,
                          180, 0, 180, 1, {}, {},
                          QStringLiteral("HSV 颜色空间色相上界（OpenCV hue 范围 0-179，180 表示不过滤）"), ""});
        om.params.append({"sMin", "饱和度最小", ParamType::Int,
                          0, 0, 255, 1, {}, {},
                          QStringLiteral("饱和度下界"), ""});
        om.params.append({"sMax", "饱和度最大", ParamType::Int,
                          255, 0, 255, 1, {}, {},
                          QStringLiteral("饱和度上界"), ""});
        om.params.append({"vMin", "明度最小", ParamType::Int,
                          0, 0, 255, 1, {}, {},
                          QStringLiteral("明度下界"), ""});
        om.params.append({"vMax", "明度最大", ParamType::Int,
                          255, 0, 255, 1, {}, {},
                          QStringLiteral("明度上界"), ""});
        reg.append(om);
    }

    // ---------- 检测 ----------
    {
        OperatorMeta om;
        om.type        = "BlobDetect";
        om.cnName      = "斑块检测";
        om.category    = "检测";
        om.iconPath    = "qrc:/icons/blob.svg";
        om.description = "SimpleBlobDetector 斑块检测（按面积/圆度过滤）";
        om.params.append({"minArea", "最小面积", ParamType::Float,
                          10.0, 0.0, 1000000.0, 10.0, {}, {},
                          QStringLiteral("斑块最小面积（像素²）"),
                          QStringLiteral("px²")});
        om.params.append({"maxArea", "最大面积", ParamType::Float,
                          50000.0, 0.0, 1000000.0, 100.0, {}, {},
                          QStringLiteral("斑块最大面积（像素²）"),
                          QStringLiteral("px²")});
        om.params.append({"minCircularity", "最小圆度", ParamType::Float,
                          0.0, 0.0, 1.0, 0.05, {}, {},
                          QStringLiteral("圆度下限（1.0=完美圆）"), ""});
        om.params.append({"maxCircularity", "最大圆度", ParamType::Float,
                          1.0, 0.0, 1.0, 0.05, {}, {},
                          QStringLiteral("圆度上限"), ""});
        reg.append(om);
    }
    {
        OperatorMeta om;
        om.type        = "ContourAnalyze";
        om.cnName      = "轮廓分析";
        om.category    = "检测";
        om.iconPath    = "qrc:/icons/contour.svg";
        om.description = "查找并按面积过滤轮廓（OTSU 自适应阈值二值化）";
        om.params.append({"minArea", "最小面积", ParamType::Float,
                          100.0, 0.0, 1000000.0, 50.0, {}, {},
                          QStringLiteral("轮廓最小面积"), QStringLiteral("px²")});
        om.params.append({"maxArea", "最大面积", ParamType::Float,
                          100000.0, 0.0, 1000000.0, 100.0, {}, {},
                          QStringLiteral("轮廓最大面积"), QStringLiteral("px²")});
        om.params.append({"filterByArea", "启用面积过滤", ParamType::Bool,
                          true, QVariant(), QVariant(), QVariant(), {}, {},
                          QStringLiteral("勾选后只保留 minArea~maxArea 之间的轮廓"), ""});
        reg.append(om);
    }
    {
        OperatorMeta om;
        om.type        = "TemplateMatch";
        om.cnName      = "模板匹配";
        om.category    = "检测";
        om.iconPath    = "qrc:/icons/template.svg";
        om.description = "模板匹配定位（6 种 OpenCV 方法）";
        om.params.append({"template", "模板图像路径", ParamType::String,
                          QStringLiteral(""),
                          QVariant(), QVariant(), QVariant(),
                          {}, {},
                          QStringLiteral("10x10 到 5000x5000 之间的灰度图，最大 50MB"), ""});
        om.params.append({"threshold", "匹配阈值", ParamType::Float,
                          0.8, 0.0, 1.0, 0.05, {}, {},
                          QStringLiteral("分数 ≥ 阈值 视为命中"), ""});
        {
            ParamSpec p;
            p.name     = "method";
            p.cnName   = "匹配方法";
            p.type     = ParamType::Enum;
            p.defaultValue = QStringLiteral("CCOEFF_NORMED");
            p.options  = {QStringLiteral("SQDIFF"), QStringLiteral("SQDIFF_NORMED"),
                          QStringLiteral("CCORR"), QStringLiteral("CCORR_NORMED"),
                          QStringLiteral("CCOEFF"), QStringLiteral("CCOEFF_NORMED")};
            p.optionKeys = {"SQDIFF", "SQDIFF_NORMED", "CCORR", "CCORR_NORMED",
                            "CCOEFF", "CCOEFF_NORMED"};
            p.help     = QStringLiteral("推荐 CCOEFF_NORMED（归一化相关系数）");
            om.params.append(p);
        }
        reg.append(om);
    }
    {
        OperatorMeta om;
        om.type        = "LineCircleDetect";
        om.cnName      = "线圆检测";
        om.category    = "检测";
        om.iconPath    = "qrc:/icons/linecircle.svg";
        om.description = "Hough 变换检测直线 / 直线段 / 圆";
        {
            ParamSpec p;
            p.name     = "detectType";
            p.cnName   = "检测类型";
            p.type     = ParamType::Enum;
            p.defaultValue = QStringLiteral("lineP");
            p.options  = {QStringLiteral("标准直线"), QStringLiteral("概率直线"),
                          QStringLiteral("圆")};
            p.optionKeys = {"line", "lineP", "circle"};
            p.help     = QStringLiteral("line=HoughLines，lineP=HoughLinesP，circle=HoughCircles");
            om.params.append(p);
        }
        om.params.append({"rho", "rho（像素）", ParamType::Float,
                          1.0, 0.1, 10.0, 0.1, {}, {},
                          QStringLiteral("距离分辨率（像素）"), QStringLiteral("px")});
        om.params.append({"theta", "theta（弧度）", ParamType::Float,
                          3.14159265 / 180.0, 0.001, 3.14159, 0.001, {}, {},
                          QStringLiteral("角度分辨率（弧度）"), QStringLiteral("rad")});
        om.params.append({"threshold", "累加器阈值", ParamType::Int,
                          100, 1, 1000, 10, {}, {},
                          QStringLiteral("最小投票数"), ""});
        om.params.append({"minLineLength", "最小线长", ParamType::Float,
                          50.0, 0.0, 10000.0, 10.0, {}, {},
                          QStringLiteral("lineP 专用"), QStringLiteral("px")});
        om.params.append({"maxLineGap", "最大线段间隙", ParamType::Float,
                          10.0, 0.0, 1000.0, 1.0, {}, {},
                          QStringLiteral("lineP 专用"), QStringLiteral("px")});
        om.params.append({"minRadius", "最小半径", ParamType::Float,
                          20.0, 1.0, 10000.0, 1.0, {}, {},
                          QStringLiteral("circle 专用"), QStringLiteral("px")});
        om.params.append({"maxRadius", "最大半径", ParamType::Float,
                          200.0, 1.0, 10000.0, 1.0, {}, {},
                          QStringLiteral("circle 专用"), QStringLiteral("px")});
        om.params.append({"dp", "累加器分辨率比", ParamType::Float,
                          1.0, 0.1, 10.0, 0.1, {}, {},
                          QStringLiteral("circle 专用（HoughCircles 的 dp）"), ""});
        om.params.append({"minDist", "圆心最小距离", ParamType::Float,
                          50.0, 1.0, 10000.0, 1.0, {}, {},
                          QStringLiteral("circle 专用"), QStringLiteral("px")});
        om.params.append({"param1", "Canny 高阈值", ParamType::Float,
                          150.0, 1.0, 1000.0, 1.0, {}, {},
                          QStringLiteral("circle 专用"), ""});
        om.params.append({"param2", "圆心累加阈值", ParamType::Float,
                          30.0, 1.0, 1000.0, 1.0, {}, {},
                          QStringLiteral("circle 专用"), ""});
        reg.append(om);
    }

    // ---------- 几何 ----------
    {
        OperatorMeta om;
        om.type        = "GeometryMeasure";
        om.cnName      = "几何测量";
        om.category    = "几何";
        om.iconPath    = "qrc:/icons/measure.svg";
        om.description = "几何量测量（距离/面积/周长/角度/圆半径）";
        {
            ParamSpec p;
            p.name     = "measureType";
            p.cnName   = "测量类型";
            p.type     = ParamType::Enum;
            p.defaultValue = QStringLiteral("distance");
            p.options  = {QStringLiteral("距离"), QStringLiteral("面积"),
                          QStringLiteral("周长"), QStringLiteral("角度"),
                          QStringLiteral("圆半径")};
            p.optionKeys = {"distance", "area", "perimeter", "angle", "circleRadius"};
            p.help     = QStringLiteral("基于最大轮廓的物理量测量");
            om.params.append(p);
        }
        om.params.append({"minThreshold", "最小阈值", ParamType::Float,
                          0.0, 0.0, 1e6, 1.0, {}, {},
                          QStringLiteral("小于此值视为 NG"), ""});
        om.params.append({"maxThreshold", "最大阈值", ParamType::Float,
                          1000.0, 0.0, 1e6, 1.0, {}, {},
                          QStringLiteral("大于此值视为 NG"), ""});
        om.params.append({"pixelScale", "像素→物理比例", ParamType::Float,
                          1.0, 1e-6, 1e6, 0.001, {}, {},
                          QStringLiteral("每像素对应的物理量（如 0.05 mm/px）"),
                          QStringLiteral("mm/px")});
        reg.append(om);
    }

    // ---------- 变换 ----------
    {
        OperatorMeta om;
        om.type        = "ImageArithmetic";
        om.cnName      = "图像算术";
        om.category    = "变换";
        om.iconPath    = "qrc:/icons/arithmetic.svg";
        om.description = "图像加减乘除 / 位运算 / 取反";
        {
            ParamSpec p;
            p.name     = "operation";
            p.cnName   = "运算";
            p.type     = ParamType::Enum;
            p.defaultValue = QStringLiteral("add");
            p.options  = {QStringLiteral("加"), QStringLiteral("减"),
                          QStringLiteral("乘"), QStringLiteral("除"),
                          QStringLiteral("与"), QStringLiteral("或"),
                          QStringLiteral("异或"), QStringLiteral("取反")};
            p.optionKeys = {"add", "subtract", "multiply", "divide",
                            "and", "or", "xor", "not"};
            p.help     = QStringLiteral("add/subtract/multiply/divide/and/or/xor/not");
            om.params.append(p);
        }
        om.params.append({"scalar", "标量", ParamType::Float,
                          0.0, 0.0, 1e6, 1.0, {}, {},
                          QStringLiteral("与标量运算时使用"), ""});
        om.params.append({"useScalar", "使用标量", ParamType::Bool,
                          false, QVariant(), QVariant(), QVariant(), {}, {},
                          QStringLiteral("勾选：与标量运算；取消：与上一张缓存图运算"), ""});
        reg.append(om);
    }
    {
        OperatorMeta om;
        om.type        = "ImageTransform";
        om.cnName      = "图像变换";
        om.category    = "变换";
        om.iconPath    = "qrc:/icons/transform.svg";
        om.description = "resize / rotate / flip / affine / perspective";
        {
            ParamSpec p;
            p.name     = "transformType";
            p.cnName   = "变换类型";
            p.type     = ParamType::Enum;
            p.defaultValue = QStringLiteral("resize");
            p.options  = {QStringLiteral("缩放"), QStringLiteral("旋转"),
                          QStringLiteral("翻转"), QStringLiteral("仿射"),
                          QStringLiteral("透视")};
            p.optionKeys = {"resize", "rotate", "flip", "affine", "perspective"};
            p.help     = QStringLiteral("5 种基本变换类型");
            om.params.append(p);
        }
        om.params.append({"angle", "旋转角度", ParamType::Float,
                          0.0, -360.0, 360.0, 1.0, {}, {},
                          QStringLiteral("rotate 专用"), QStringLiteral("°")});
        om.params.append({"scaleX", "X 缩放", ParamType::Float,
                          1.0, 0.01, 10.0, 0.1, {}, {},
                          QStringLiteral("affine/perspective 专用"), ""});
        om.params.append({"scaleY", "Y 缩放", ParamType::Float,
                          1.0, 0.01, 10.0, 0.1, {}, {},
                          QStringLiteral("affine/perspective 专用"), ""});
        om.params.append({"flipCode", "翻转码", ParamType::Int,
                          0, -1, 1, 1, {}, {},
                          QStringLiteral("flip 专用：0=垂直，1=水平，-1=双向"), ""});
        om.params.append({"targetWidth", "目标宽度", ParamType::Int,
                          640, 1, 8192, 1, {}, {},
                          QStringLiteral("resize 专用"), QStringLiteral("px")});
        om.params.append({"targetHeight", "目标高度", ParamType::Int,
                          480, 1, 8192, 1, {}, {},
                          QStringLiteral("resize 专用"), QStringLiteral("px")});
        reg.append(om);
    }
    {
        OperatorMeta om;
        om.type        = "ImageMerge";
        om.cnName      = "图像合并";
        om.category    = "变换";
        om.iconPath    = "qrc:/icons/merge.svg";
        om.description = "水平/垂直拼接 / 覆盖 / Alpha 混合（与上一帧缓存图合并）";
        {
            ParamSpec p;
            p.name     = "mergeType";
            p.cnName   = "合并类型";
            p.type     = ParamType::Enum;
            p.defaultValue = QStringLiteral("horizontal");
            p.options  = {QStringLiteral("水平拼接"), QStringLiteral("垂直拼接"),
                          QStringLiteral("覆盖"), QStringLiteral("Alpha 混合")};
            p.optionKeys = {"horizontal", "vertical", "overlay", "alphaBlend"};
            p.help     = QStringLiteral("horizontal/vertical/overlay/alphaBlend");
            om.params.append(p);
        }
        om.params.append({"alpha", "Alpha", ParamType::Float,
                          0.5, 0.0, 1.0, 0.05, {}, {},
                          QStringLiteral("alphaBlend 专用（前景权重）"), ""});
        reg.append(om);
    }

    // ---------- 分支 ----------
    {
        OperatorMeta om;
        om.type        = "BranchControl";
        om.cnName      = "分支控制";
        om.category    = "分支";
        om.iconPath    = "qrc:/icons/branch.svg";
        om.description = "条件分支控制（根据上游工具结果决定流向）";
        {
            ParamSpec p;
            p.name     = "conditionOp";
            p.cnName   = "条件运算符";
            p.type     = ParamType::Enum;
            p.defaultValue = QStringLiteral(">");
            p.options  = {QStringLiteral("大于"), QStringLiteral("小于"),
                          QStringLiteral("等于"), QStringLiteral("不等于"),
                          QStringLiteral("大于等于"), QStringLiteral("小于等于")};
            p.optionKeys = {">", "<", "==", "!=", ">=", "<="};
            p.help     = QStringLiteral("将上游 score 与 conditionValue 比较");
            om.params.append(p);
        }
        om.params.append({"conditionValue", "条件值", ParamType::Float,
                          0.5, -1e9, 1e9, 0.01, {}, {},
                          QStringLiteral("比较的阈值"), ""});
        om.params.append({"trueBranch", "真分支工具 ID 列表", ParamType::Vector,
                          QStringLiteral(""),
                          QVariant(), QVariant(), QVariant(), {}, {},
                          QStringLiteral("逗号或换行分隔的工具 ID"), ""});
        om.params.append({"falseBranch", "假分支工具 ID 列表", ParamType::Vector,
                          QStringLiteral(""),
                          QVariant(), QVariant(), QVariant(), {}, {},
                          QStringLiteral("逗号或换行分隔的工具 ID"), ""});
        reg.append(om);
    }

    // ---------- AI ----------
    {
        OperatorMeta om;
        om.type        = "AiClassify";
        om.cnName      = "AI 分类";
        om.category    = "AI";
        om.iconPath    = "qrc:/icons/ai.svg";
        om.description = "使用训练好的模型对图像进行分类（PyTorch/ONNX/TensorRT）";
        om.params.append({"modelPath", "模型路径", ParamType::String,
                          QStringLiteral(""),
                          QVariant(), QVariant(), QVariant(), {}, {},
                          QStringLiteral("推荐 .pt / .onnx 格式"), ""});
        om.params.append({"confidenceThreshold", "置信度阈值", ParamType::Float,
                          0.5, 0.0, 1.0, 0.05, {}, {},
                          QStringLiteral("分数 ≥ 阈值 视为 OK"), ""});
        om.params.append({"topK", "Top-K", ParamType::Int,
                          3, 1, 100, 1, {}, {},
                          QStringLiteral("返回前 K 个分类"), ""});
        om.params.append({"inputWidth", "输入宽", ParamType::Int,
                          224, 32, 4096, 32, {}, {},
                          QStringLiteral("模型输入尺寸"), QStringLiteral("px")});
        om.params.append({"inputHeight", "输入高", ParamType::Int,
                          224, 32, 4096, 32, {}, {},
                          QStringLiteral("模型输入尺寸"), QStringLiteral("px")});
        om.params.append({"categoryLabels", "类别标签", ParamType::Vector,
                          QStringLiteral(""),
                          QVariant(), QVariant(), QVariant(), {}, {},
                          QStringLiteral("每行一个类别名（顺序对应模型输出索引）"), ""});
        reg.append(om);
    }

    // 按 category 排序，同 category 内按 cnName 排序
    std::sort(reg.begin(), reg.end(),
              [](const OperatorMeta& a, const OperatorMeta& b) {
                  if (a.category != b.category) return a.category < b.category;
                  return a.cnName < b.cnName;
              });

    return reg;
}

QList<OperatorMeta> OperatorDescriptors::all() {
    if (!s_initialized) {
        // v2.1.0 M5：优先从 config/operators.json 加载（用户可热改默认参数）；
        // 加载失败（如文件不存在/JSON 损坏）回退到内置 buildRegistry()。
        const QString path = defaultConfigPath();
        QString err;
        QList<OperatorMeta> reg;
        if (loadFromJson(path, &err)) {
            reg = s_registry;
            // 同时走 Qt 日志（IDE 控制台）和 QDV Logger（logs/yyyymmdd.log）
            qInfo("[OperatorDescriptors] loaded %d operators from %s",
                  int(reg.size()), qUtf8Printable(path));
            QDV::Logger::info(QString("[OperatorDescriptors] loaded %1 operators from %2")
                                  .arg(reg.size()).arg(path));
            // v2.1.0 M5：把每个算子加载到的 defaultValue 写到日志，
            // 方便端到端验证 JSON 是否真的被加载（vs buildRegistry 内置值）
            for (const OperatorMeta& om : reg) {
                QStringList kv;
                for (const ParamSpec& ps : om.params) {
                    kv << QString("%1=%2").arg(ps.name).arg(ps.defaultValue.toString());
                }
                QDV::Logger::info(QString("[OperatorDescriptors]   %1: %2")
                                      .arg(om.type, -16)
                                      .arg(kv.join(", ")));
            }
        } else {
            qWarning("[OperatorDescriptors] config load failed (%s), fallback to buildRegistry()",
                     qUtf8Printable(err));
            QDV::Logger::warn(QString("[OperatorDescriptors] config load failed (%1), fallback to buildRegistry()")
                                  .arg(err));
            reg = buildRegistry();
        }
        s_registry = reg;
        s_initialized = true;
    }
    // Phase 2: 合并外部动态算子（如 Histogram.dll 加载的算子元数据）
    // v5.3.7 修复：按 type 跨表去重，保留 s_registry（JSON 权威源）版本
    if (s_externalRegistry.isEmpty()) {
        return s_registry;
    }
    QList<OperatorMeta> merged = s_registry;
    QSet<QString> seenTypes;
    for (const OperatorMeta& om : s_registry) {
        seenTypes.insert(om.type);
    }
    for (const OperatorMeta& om : s_externalRegistry) {
        if (!seenTypes.contains(om.type)) {
            merged.append(om);
            seenTypes.insert(om.type);
        }
    }
    return merged;
}

QStringList OperatorDescriptors::categories() {
    QStringList cats;
    const QList<OperatorMeta>& reg = all();
    for (const OperatorMeta& om : reg) {
        if (!cats.contains(om.category)) {
            cats.append(om.category);
        }
    }
    return cats;
}

OperatorMeta OperatorDescriptors::get(const QString& type) {
    const QList<OperatorMeta>& reg = all();
    for (const OperatorMeta& om : reg) {
        if (om.type == type) {
            return om;
        }
    }
    return OperatorMeta{};  // 空对象
}

QList<OperatorMeta> OperatorDescriptors::byCategory(const QString& category) {
    QList<OperatorMeta> result;
    const QList<OperatorMeta>& reg = all();
    for (const OperatorMeta& om : reg) {
        if (om.category == category) {
            result.append(om);
        }
    }
    return result;
}

QList<OperatorMeta> OperatorDescriptors::search(const QString& keyword) {
    QList<OperatorMeta> result;
    const QString lowerKw = keyword.toLower();
    const QList<OperatorMeta>& reg = all();
    for (const OperatorMeta& om : reg) {
        // 匹配 cnName / type / description / category
        if (om.cnName.toLower().contains(lowerKw) ||
            om.type.toLower().contains(lowerKw) ||
            om.description.toLower().contains(lowerKw) ||
            om.category.toLower().contains(lowerKw) ||
            om.subGroup.toLower().contains(lowerKw)) {
            result.append(om);
        }
    }
    // 按匹配度排序（cnName 命中优先）
    std::sort(result.begin(), result.end(),
              [&lowerKw](const OperatorMeta& a, const OperatorMeta& b) {
        bool aCn = a.cnName.toLower().contains(lowerKw);
        bool bCn = b.cnName.toLower().contains(lowerKw);
        if (aCn != bCn) return aCn > bCn;
        return a.cnName < b.cnName;
    });
    return result;
}

QStringList OperatorDescriptors::subGroups(const QString& category) {
    QStringList result;
    const QList<OperatorMeta>& reg = all();
    for (const OperatorMeta& om : reg) {
        if (om.category == category && !om.subGroup.isEmpty() &&
            !result.contains(om.subGroup)) {
            result.append(om.subGroup);
        }
    }
    result.sort();
    return result;
}

bool OperatorDescriptors::has(const QString& type) {
    const QList<OperatorMeta>& reg = all();
    for (const OperatorMeta& om : reg) {
        if (om.type == type) {
            return true;
        }
    }
    return false;
}

QStringList OperatorDescriptors::allTypes() {
    QStringList types;
    const QList<OperatorMeta>& reg = all();
    for (const OperatorMeta& om : reg) {
        types.append(om.type);
    }
    return types;
}

// ========================================================================
// v2.1.0 M5：算子元数据外部化（config/operators.json）
// 加载策略：缺文件/解析失败 → 回退 buildRegistry()（见 all()）
// JSON 格式：{"version":1,"operators":[{type,cnName,category,iconPath,
//   description,params:[{name,cnName,type(int),defaultValue,minValue,
//   maxValue,step,options:[],optionKeys:[],help,unit}]}, ...]}
// ========================================================================

QString OperatorDescriptors::defaultConfigPath() {
    // 部署路径：<exe-dir>/config/operators.json
    // 开发期（CWD=build）也可命中，因 QCoreApplication::applicationDirPath()
    // 始终返回 exe 所在目录，windwos 下从 build 启动时 = build/bin
    const QString dir = QCoreApplication::applicationDirPath() + "/config";
    QDir().mkpath(dir);
    return dir + "/operators.json";
}

bool OperatorDescriptors::loadFromJson(const QString& path, QString* outError) {
    auto fail = [outError](const QString& m) -> bool {
        if (outError) *outError = m;
        return false;
    };

    QFile f(path);
    if (!f.exists()) {
        return fail(QStringLiteral("file not found: %1").arg(path));
    }
    if (!f.open(QIODevice::ReadOnly)) {
        return fail(QStringLiteral("open failed: %1").arg(f.errorString()));
    }
    const QByteArray data = f.readAll();
    f.close();

    QJsonParseError perr;
    QJsonDocument doc = QJsonDocument::fromJson(data, &perr);
    if (perr.error != QJsonParseError::NoError) {
        return fail(QStringLiteral("JSON parse error at offset %1: %2")
                        .arg(perr.offset).arg(perr.errorString()));
    }
    if (!doc.isObject()) {
        return fail(QStringLiteral("root is not JSON object"));
    }
    const QJsonObject root = doc.object();
    const QJsonArray ops = root.value("operators").toArray();
    if (ops.isEmpty()) {
        return fail(QStringLiteral("operators array is empty"));
    }

    QList<OperatorMeta> reg;
    reg.reserve(ops.size());
    QStringList parseErrors;
    for (const QJsonValue& v : ops) {
        if (!v.isObject()) continue;
        const OperatorMeta om = OperatorMeta::fromMap(v.toObject().toVariantMap());
        // P1-B2 修复：入库校验 — type 非空、Enum 类型 options/optionKeys 长度一致、Int/Float minValue<=maxValue
        if (om.type.isEmpty()) {
            parseErrors.append(QStringLiteral("跳过：type 为空的条目"));
            continue;
        }
        bool metaValid = true;
        for (const ParamSpec& p : om.params) {
            if (p.name.isEmpty()) {
                parseErrors.append(QStringLiteral("%1: 参数 name 为空").arg(om.type));
                metaValid = false; break;
            }
            if (p.type == ParamType::Enum) {
                if (p.options.size() != p.optionKeys.size() || p.options.isEmpty()) {
                    parseErrors.append(QStringLiteral("%1.%2: Enum options/optionKeys 长度不一致或为空")
                                        .arg(om.type).arg(p.name));
                    metaValid = false; break;
                }
            }
            if (p.type == ParamType::Int || p.type == ParamType::Float) {
                bool minOk, maxOk;
                double mn = p.minValue.toDouble(&minOk);
                double mx = p.maxValue.toDouble(&maxOk);
                if (minOk && maxOk && mn > mx) {
                    parseErrors.append(QStringLiteral("%1.%2: minValue(%3) > maxValue(%4)")
                                        .arg(om.type).arg(p.name).arg(mn).arg(mx));
                    metaValid = false; break;
                }
            }
        }
        if (!metaValid) continue;
        reg.append(om);
    }
    if (reg.isEmpty()) {
        return fail(QStringLiteral("no valid operator entry parsed; errors: %1")
                        .arg(parseErrors.join("; ")));
    }

    std::sort(reg.begin(), reg.end(),
              [](const OperatorMeta& a, const OperatorMeta& b) {
                  if (a.category != b.category) return a.category < b.category;
                  return a.cnName < b.cnName;
              });

    // P1-B1 修复：剔除幽灵算子（用户选择"只剔除不实现"策略）
    // 之前 P1-A11 仅打 warning 不剔除，用户拖拽幽灵算子时报错体验断裂
    // 现在直接从 registry 中移除未在 ToolFactory 注册的算子，UI 不会再展示
    QStringList registered = ToolFactory::instance()->getAvailableToolTypes();
    QSet<QString> registeredSet(registered.begin(), registered.end());
    QStringList ghosts;
    QList<OperatorMeta> filtered;
    filtered.reserve(reg.size());
    for (const OperatorMeta& om : reg) {
        if (registeredSet.contains(om.type)) {
            filtered.append(om);
        } else {
            ghosts.append(om.type);
        }
    }
    if (!ghosts.isEmpty()) {
        Logger::warn(QStringLiteral("[OperatorDescriptors] 已剔除 %1 个未在 ToolFactory 注册的幽灵算子: %2")
                         .arg(ghosts.size()).arg(ghosts.join(", ")));
    }
    if (!parseErrors.isEmpty()) {
        Logger::warn(QStringLiteral("[OperatorDescriptors] 入库校验跳过 %1 个非法条目: %2")
                         .arg(parseErrors.size()).arg(parseErrors.join("; ")));
    }

    s_registry = filtered;
    s_initialized = true;

    return true;
}

bool OperatorDescriptors::exportToJson(const QString& path) {
    // 用 buildRegistry() 的"权威"内置数据（与代码版本强一致），
    // 改 config/operators.json 不影响 export 出的内容
    const QList<OperatorMeta> reg = buildRegistry();

    QJsonObject root;
    root["version"] = 1;
    root["comment"] = QStringLiteral(
        "Q-DetectVision 算子元数据配置 (v2.1.0 M5)\n"
        "修改后需重启应用生效；删除/损坏后回退到内置 buildRegistry()。\n"
        "字段含义参考 ParamSpec / OperatorMeta（include/UI/OperatorDescriptors.h）");
    root["regeneratedAt"] = QDateTime::currentDateTime().toString(Qt::ISODate);

    QJsonArray ops;
    for (const OperatorMeta& om : reg) {
        ops.append(QJsonObject::fromVariantMap(om.toMap()));
    }
    root["operators"] = ops;

    QFileInfo fi(path);
    QDir().mkpath(fi.absolutePath());

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    f.close();
    return true;
}

void OperatorDescriptors::reset() {
    s_registry.clear();
    s_initialized = false;
    s_externalRegistry.clear();  // Phase 2: 同步清空外部算子注册表
}

// Phase 2: 注册外部动态算子元数据（由 OperatorPluginLoader 调用）
// 不重复添加同 type；成功返回 true
bool OperatorDescriptors::registerExternalOperator(const OperatorMeta& meta) {
    if (meta.type.isEmpty()) return false;
    // 不重复添加同 type
    for (const auto& existing : s_externalRegistry) {
        if (existing.type == meta.type) return false;
    }
    s_externalRegistry.append(meta);
    return true;
}

} // namespace UI
} // namespace QDV
