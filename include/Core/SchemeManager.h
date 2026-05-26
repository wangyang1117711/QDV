#ifndef SCHEMEMANAGER_H
#define SCHEMEMANAGER_H

#include <QObject>
#include <QStringList>
#include <QMap>
#include <QMutex>
#include "Scheme.h"

class SchemeManager : public QObject {
    Q_OBJECT
    
public:
    static SchemeManager* instance();
    
    bool loadScheme(const QString& filePath);
    bool saveScheme(Scheme* scheme, const QString& filePath);
    bool saveCurrentScheme();
    
    QStringList listSchemes(const QString& directory);
    bool deleteScheme(const QString& filePath);
    
    Scheme* currentScheme() const { return m_currentScheme; }
    void setCurrentScheme(Scheme* scheme);
    
    bool importScheme(const QString& filePath);
    bool exportScheme(Scheme* scheme, const QString& filePath);
    
signals:
    void schemeLoaded(Scheme* scheme);
    void schemeSaved(Scheme* scheme);
    void schemeChanged(Scheme* scheme);
    
private:
    SchemeManager(QObject* parent = nullptr);
    ~SchemeManager();
    
    Scheme* m_currentScheme = nullptr;
    QMap<QString, Scheme*> m_schemeCache;
    
    static SchemeManager* s_instance;
    static QMutex s_mutex;
};

#endif // SCHEMEMANAGER_H