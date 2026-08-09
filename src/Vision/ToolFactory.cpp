#include "ToolFactory.h"
#include "TemplateMatchTool.h"
#include "EdgeDetectTool.h"
#include "BlobDetectTool.h"
#include "ColorDetectTool.h"
#include "ThresholdTool.h"
#include "ImagePreprocessTool.h"
#include "ContourAnalyzeTool.h"
#include "BranchControlTool.h"
#include "GeometryMeasureTool.h"
#include "LineCircleDetectTool.h"
#include "ImageArithmeticTool.h"
#include "ImageTransformTool.h"
#include "ImageMergeTool.h"
#include "AiClassifyTool.h"
#include "DetectObjectsDlTool.h"
#include "SegmentDlTool.h"
#include "YoloDetectTool.h"
#include "ZeroShotDetectTool.h"   // spec v2 阶段三 Task 8：零样本检测算子
#include "ReadImageTool.h"
#include "OpenFramegrabberTool.h"
#include "NormalizationTool.h"
#include "GrabImageTool.h"
#include "OperatorSDK/IInferenceEngine.h"
// 新增：滤波类
#include "GaussFilterTool.h"
#include "MeanImageTool.h"
#include "MedianImageTool.h"
#include "EmphasizeTool.h"
#include "ScaleImageTool.h"
#include "FftGenericTool.h"
// 新增：形态学类
#include "ErosionTool.h"
#include "DilationTool.h"
#include "OpeningTool.h"
#include "ClosingTool.h"
#include "TopHatTool.h"
#include "BottomHatTool.h"
// 新增：几何变换+分割类
#include "AffineTransImageTool.h"
#include "PolarTransImageTool.h"
#include "DynThresholdTool.h"
#include "WatershedTool.h"
#include "RegionGrowingTool.h"
// 新增：Blob+特征提取类
#include "ConnectionTool.h"
#include "SelectShapeTool.h"
#include "PointsHarrisTool.h"
#include "EdgesSubPixTool.h"
// 新增：匹配+测量类
#include "FindNccModelTool.h"
#include "FindShapeModelTool.h"
#include "DistancePpTool.h"
#include "AngleLlTool.h"
// 新增：分析与识别类（2026-07-09）
#include "HistogramTool.h"
#include "RgbExtractTool.h"
#include "ChannelSplitTool.h"
#include "AreaCenterTool.h"
#include "QrCodeDetectTool.h"
#include "Barcode1dTool.h"
#include "OcrTool.h"
#include "HandEyeCalibTool.h"
#include "RobotPoseTool.h"       // v5.3：机器人位姿算子
// P0/P1 升级新增算子（2026-07-15）
#include "PositionCorrectTool.h"   // P0-2 位置修正
#include "UnitConvertTool.h"       // P0-3 单位换算
#include "CaliperTool.h"           // P0-4 卡尺测量
#include "CameraCalibTool.h"       // P0-5 相机标定
#include "ContourMatchTool.h"      // P0-6b 轮廓匹配
#include "ColorMatchTool.h"        // P0-6c 颜色比对
#include "DLOCRTool.h"             // P0-6d 端到端 OCR
#include "LightControlTool.h"      // P0-6e 光源控制
#include "LoopTool.h"              // P1-4a 循环遍历
#include "ScriptTool.h"            // P1-4b Python 脚本
#include "VariableTool.h"          // P1-4c 变量运算
#include "SurfaceDefectTool.h"     // P1-5a 表面缺陷检测
#include "ContourCompareTool.h"    // P1-5b 轮廓比对
#include "FlowJoinTool.h"          // v2.7.0 流程合并

using namespace QDV;

ToolFactory* ToolFactory::s_instance = nullptr;

ToolFactory::ToolFactory() {
    // 原有算子（15个）
    registerTool("TemplateMatch", []() { return new TemplateMatchTool(); });
    registerTool("EdgeDetect", []() { return new EdgeDetectTool(); });
    registerTool("BlobDetect", []() { return new BlobDetectTool(); });
    registerTool("ColorDetect", []() { return new ColorDetectTool(); });
    registerTool("Threshold", []() { return new ThresholdTool(); });
    registerTool("ImagePreprocess", []() { return new ImagePreprocessTool(); });
    registerTool("ContourAnalyze", []() { return new ContourAnalyzeTool(); });
    registerTool("BranchControl", []() { return new BranchControlTool(); });
    registerTool("GeometryMeasure", []() { return new GeometryMeasureTool(); });
    registerTool("LineCircleDetect", []() { return new LineCircleDetectTool(); });
    registerTool("ImageArithmetic", []() { return new ImageArithmeticTool(); });
    registerTool("ImageTransform", []() { return new ImageTransformTool(); });
    registerTool("ImageMerge", []() { return new ImageMergeTool(); });
    registerTool("AiClassify", []() { return new AiClassifyTool(); });
    registerTool("DetectObjectsDl", []() { return new DetectObjectsDlTool(); });
    registerTool("SegmentDl", []() { return new SegmentDlTool(); });
    registerTool("YoloDetect", []() { return new YoloDetectTool(); });
    registerTool("Normalization", []() { return new NormalizationTool(); });
    registerTool("ReadImage", []() { return new ReadImageTool(); });

    // 图像采集类（2个）
    registerTool("OpenFramegrabber", []() { return new OpenFramegrabberTool(); });
    registerTool("GrabImage",        []() { return new GrabImageTool(); });

    // 新增：滤波类（6个）
    registerTool("GaussFilter", []() { return new GaussFilterTool(); });
    registerTool("MeanImage", []() { return new MeanImageTool(); });
    registerTool("MedianImage", []() { return new MedianImageTool(); });
    registerTool("Emphasize", []() { return new EmphasizeTool(); });
    registerTool("ScaleImage", []() { return new ScaleImageTool(); });
    registerTool("FftGeneric", []() { return new FftGenericTool(); });

    // 新增：形态学类（6个）
    registerTool("Erosion", []() { return new ErosionTool(); });
    registerTool("Dilation", []() { return new DilationTool(); });
    registerTool("Opening", []() { return new OpeningTool(); });
    registerTool("Closing", []() { return new ClosingTool(); });
    registerTool("TopHat", []() { return new TopHatTool(); });
    registerTool("BottomHat", []() { return new BottomHatTool(); });

    // 新增：几何变换+分割类（5个）
    registerTool("AffineTransImage", []() { return new AffineTransImageTool(); });
    registerTool("PolarTransImage", []() { return new PolarTransImageTool(); });
    registerTool("DynThreshold", []() { return new DynThresholdTool(); });
    registerTool("Watershed", []() { return new WatershedTool(); });
    registerTool("RegionGrowing", []() { return new RegionGrowingTool(); });

    // 新增：Blob+特征提取类（4个）
    registerTool("Connection", []() { return new ConnectionTool(); });
    registerTool("SelectShape", []() { return new SelectShapeTool(); });
    registerTool("PointsHarris", []() { return new PointsHarrisTool(); });
    registerTool("EdgesSubPix", []() { return new EdgesSubPixTool(); });

    // 新增：匹配+测量类（4个）
    registerTool("FindNccModel", []() { return new FindNccModelTool(); });
    registerTool("FindShapeModel", []() { return new FindShapeModelTool(); });
    registerTool("DistancePp", []() { return new DistancePpTool(); });
    registerTool("AngleLl", []() { return new AngleLlTool(); });

    // 新增：分析与识别类（7个，2026-07-09）
    registerTool("Histogram",     []() { return new HistogramTool(); });
    registerTool("RgbExtract",    []() { return new RgbExtractTool(); });
    registerTool("ChannelSplit",  []() { return new ChannelSplitTool(); });
    registerTool("AreaCenter",    []() { return new AreaCenterTool(); });
    registerTool("QrCodeDetect",  []() { return new QrCodeDetectTool(); });
    registerTool("Barcode1d",     []() { return new Barcode1dTool(); });
    registerTool("Ocr",           []() { return new OcrTool(); });
    registerTool("HandEyeCalib",  []() { return new HandEyeCalibTool(); });
    registerTool("RobotPose",      []() { return new RobotPoseTool(); });  // v5.3

    // P0/P1 升级新增算子（2026-07-15）
    // P0 坐标与标定类
    registerTool("PositionCorrect", []() { return new PositionCorrectTool(); });  // P0-2
    registerTool("UnitConvert",     []() { return new UnitConvertTool(); });      // P0-3
    registerTool("Caliper",         []() { return new CaliperTool(); });          // P0-4
    registerTool("CameraCalib",     []() { return new CameraCalibTool(); });      // P0-5
    // P0 高级匹配与检测类
    registerTool("ContourMatch",    []() { return new ContourMatchTool(); });     // P0-6b
    registerTool("ColorMatch",      []() { return new ColorMatchTool(); });       // P0-6c
    registerTool("DLOCR",           []() { return new DLOCRTool(); });            // P0-6d
    registerTool("LightControl",    []() { return new LightControlTool(); });     // P0-6e
    // P1 流程控制类
    registerTool("Loop",            []() { return new LoopTool(); });             // P1-4a
    registerTool("Script",          []() { return new ScriptTool(); });           // P1-4b
    registerTool("Variable",        []() { return new VariableTool(); });         // P1-4c
    // P1 缺陷检测类
    registerTool("SurfaceDefect",   []() { return new SurfaceDefectTool(); });    // P1-5a
    registerTool("ContourCompare",  []() { return new ContourCompareTool(); });   // P1-5b
    // v2.7.0 流程控制类
    registerTool("FlowJoin",        []() { return new FlowJoinTool(); });        // 流程合并
    // spec v2 阶段三 Task 8：零样本检测算子（封装 zsu::Kit 进方案链）
    registerTool("ZeroShotDetect",  []() { return new ZeroShotDetectTool(); });
}

ToolFactory* ToolFactory::instance() {
    if (!s_instance) {
        s_instance = new ToolFactory();
    }
    return s_instance;
}

void ToolFactory::registerTool(const QString& type, std::function<QDV::VisionTool*()> creator) {
    m_creators[type] = creator;
}

QDV::VisionTool* ToolFactory::createTool(const QString& type) {
    auto it = m_creators.find(type);
    if (it != m_creators.end()) {
        QDV::VisionTool* tool = it.value()();
        // v5.3：AI 算子创建后自动注入推理引擎
        if (tool && m_inferenceEngine) {
            if (type == "AiClassify") {
                AiClassifyTool* aiTool = dynamic_cast<AiClassifyTool*>(tool);
                if (aiTool) aiTool->setInferenceEngine(m_inferenceEngine);
            } else if (type == "DetectObjectsDl") {
                DetectObjectsDlTool* detTool = dynamic_cast<DetectObjectsDlTool*>(tool);
                if (detTool) detTool->setInferenceEngine(m_inferenceEngine);
            } else if (type == "SegmentDl") {
                SegmentDlTool* segTool = dynamic_cast<SegmentDlTool*>(tool);
                if (segTool) segTool->setInferenceEngine(m_inferenceEngine);
            } else if (type == "YoloDetect") {
                YoloDetectTool* yoloTool = dynamic_cast<YoloDetectTool*>(tool);
                if (yoloTool) yoloTool->setInferenceEngine(m_inferenceEngine);
            }
        }
        return tool;
    }
    return nullptr;
}

void ToolFactory::setInferenceEngine(QDV::IInferenceEngine* engine) {
    m_inferenceEngine = engine;
}

QStringList ToolFactory::getAvailableToolTypes() const {
    return m_creators.keys();
}