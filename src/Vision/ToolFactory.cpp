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
#include "ReadImageTool.h"

using namespace QDV;

ToolFactory* ToolFactory::s_instance = nullptr;

ToolFactory::ToolFactory() {
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
    registerTool("ReadImage", []() { return new ReadImageTool(); });
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
        return it.value()();
    }
    return nullptr;
}

QStringList ToolFactory::getAvailableToolTypes() const {
    return m_creators.keys();
}