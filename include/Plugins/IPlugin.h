#ifndef IPLUGIN_H
#define IPLUGIN_H

#include <opencv2/core/mat.hpp>
#include <QString>

class QWidget;

class Scheme;
class ToolResult;

class IPlugin {
public:
    virtual ~IPlugin() = default;
    
    virtual QString name() const = 0;
    virtual QString version() const = 0;
    
    virtual void initialize(QWidget* parent) = 0;
    virtual void cleanup() = 0;
    
    virtual void onSchemeLoaded(Scheme* scheme) = 0;
    virtual void onFrameAcquired(const cv::Mat& frame) = 0;
    virtual void onToolResult(const ToolResult& result) = 0;
};

#define IPlugin_IID "com.qdetectvision.plugin.IPlugin"
Q_DECLARE_INTERFACE(IPlugin, IPlugin_IID)

#endif // IPLUGIN_H