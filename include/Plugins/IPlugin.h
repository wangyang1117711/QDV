#ifndef IPLUGIN_H
#define IPLUGIN_H

// P1-C9 + P1-C12 修复（CodeWiki 已知限制 + 架构评估 D7）：
// ---------------------------------------------------------------------
// 之前 IPlugin 接口与 TestPlugin 实现签名严重不一致：
//   * IPlugin::initialize(QWidget*) 返回 void，TestPlugin::initialize() 返回 bool
//   * IPlugin 缺少 enable/disable/isEnabled/description 等方法，
//     但 TestPlugin 声明为 override（实际并非 override，编译告警）
//   * IPlugin 未继承 QObject，导致插件无法直接使用 signals/slots
//     TestPlugin 不得不多继承 QObject + IPlugin，引入脆弱性
//
// 本次修复：
//   1) IPlugin 继承 QObject，插件可直接使用 signals/slots（D7）
//   2) 接口方法对齐 TestPlugin 实现：initialize/enable/disable/cleanup/description
//   3) initialize(QWidget*) 改为 initialize() 返回 bool，与实际语义一致
//      （原 QWidget* parent 参数从未被任何插件使用）
//   4) 保留 onSchemeLoaded/onFrameAcquired/onToolResult 钩子
//
// 兼容性：TestPlugin 与 PluginManager 同步更新；外部插件需重新编译。
// ---------------------------------------------------------------------

#include <QObject>
#include <opencv2/core/mat.hpp>
#include <QString>

class QWidget;
class Scheme;
class ToolResult;

class IPlugin : public QObject {
    Q_OBJECT

public:
    virtual ~IPlugin() = default;

    virtual QString name() const = 0;
    virtual QString version() const = 0;
    virtual QString description() const { return QString(); }

    /// 初始化插件内部资源。返回 true 表示成功。
    virtual bool initialize() = 0;
    /// 启用插件（开始响应钩子）。
    virtual bool enable() = 0;
    /// 禁用插件（停止响应钩子，但保留资源）。
    virtual bool disable() = 0;
    /// 释放插件所有资源。
    virtual void cleanup() = 0;

    virtual bool isEnabled() const = 0;

    /// 钩子：方案加载完成
    virtual void onSchemeLoaded(Scheme* scheme) { Q_UNUSED(scheme) }
    /// 钩子：新帧采集
    virtual void onFrameAcquired(const cv::Mat& frame) { Q_UNUSED(frame) }
    /// 钩子：工具执行结果产生
    virtual void onToolResult(const ToolResult& result) { Q_UNUSED(result) }
};

#define IPlugin_IID "com.qdetectvision.plugin.IPlugin"
Q_DECLARE_INTERFACE(IPlugin, IPlugin_IID)

#endif // IPLUGIN_H
