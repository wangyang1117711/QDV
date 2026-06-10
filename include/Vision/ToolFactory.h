#ifndef TOOLFACTORY_H
#define TOOLFACTORY_H

#include <QString>
#include <QStringList>
#include <QMap>
#include <functional>
#include "VisionTool.h"

class ToolFactory {
public:
    static ToolFactory* instance();
    
    void registerTool(const QString& type, std::function<QDV::VisionTool*()> creator);
    QDV::VisionTool* createTool(const QString& type);
    QStringList getAvailableToolTypes() const;
    
private:
    ToolFactory();
    
    QMap<QString, std::function<QDV::VisionTool*()>> m_creators;
    static ToolFactory* s_instance;
};

#define REGISTER_TOOL(type, className) \
    static bool _registered_##className = []() { \
        ToolFactory::instance()->registerTool(type, []() -> QDV::VisionTool* { return new className(); }); \
        return true; \
    }()

#endif // TOOLFACTORY_H