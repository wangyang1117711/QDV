#include "SchemeManager.h"
#include "Logger.h"
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>

using namespace QDV;

SchemeManager* SchemeManager::s_instance = nullptr;
QMutex SchemeManager::s_mutex;

SchemeManager::SchemeManager(QObject* parent) : QObject(parent) {
}

SchemeManager::~SchemeManager() {
    QMutexLocker locker(&s_mutex);
    for (Scheme* scheme : m_schemeCache.values()) {
        delete scheme;
    }
    delete m_currentScheme;
}

SchemeManager* SchemeManager::instance() {
    if (!s_instance) {
        QMutexLocker locker(&s_mutex);
        if (!s_instance) {
            s_instance = new SchemeManager();
        }
    }
    return s_instance;
}

bool SchemeManager::loadScheme(const QString& filePath) {
    QMutexLocker locker(&s_mutex);
    
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        Logger::error("Failed to open scheme file: " + filePath);
        return false;
    }
    
    QByteArray data = file.readAll();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) {
        Logger::error("Invalid scheme file format: " + filePath);
        return false;
    }
    
    Scheme* scheme = new Scheme();
    if (!scheme->deserialize(doc.object())) {
        delete scheme;
        return false;
    }
    
    scheme->setFilePath(filePath);
    
    delete m_currentScheme;
    m_currentScheme = scheme;
    m_schemeCache[filePath] = scheme;
    
    emit schemeLoaded(m_currentScheme);
    Logger::info("Scheme loaded: " + filePath);
    return true;
}

bool SchemeManager::saveScheme(Scheme* scheme, const QString& filePath) {
    QMutexLocker locker(&s_mutex);
    
    QJsonObject obj = scheme->serialize();
    QJsonDocument doc(obj);
    
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        Logger::error("Failed to open file for writing: " + filePath);
        return false;
    }
    
    qint64 bytesWritten = file.write(doc.toJson(QJsonDocument::Indented));
    if (bytesWritten != doc.toJson().size()) {
        Logger::error("Failed to write all data to: " + filePath);
        return false;
    }
    
    file.close();
    scheme->setFilePath(filePath);
    
    emit schemeSaved(scheme);
    Logger::info("Scheme saved: " + filePath);
    return true;
}

bool SchemeManager::saveCurrentScheme() {
    QMutexLocker locker(&s_mutex);
    
    if (!m_currentScheme) {
        return false;
    }
    return saveScheme(m_currentScheme, m_currentScheme->filePath());
}

QStringList SchemeManager::listSchemes(const QString& directory) {
    QMutexLocker locker(&s_mutex);
    
    QDir dir(directory);
    if (!dir.exists()) {
        return QStringList();
    }
    
    QStringList filters;
    filters << "*.svscheme";
    return dir.entryList(filters, QDir::Files);
}

bool SchemeManager::deleteScheme(const QString& filePath) {
    QMutexLocker locker(&s_mutex);
    
    QFile file(filePath);
    bool result = file.remove();
    if (result) {
        m_schemeCache.remove(filePath);
        Logger::info("Scheme deleted: " + filePath);
    }
    return result;
}

void SchemeManager::setCurrentScheme(Scheme* scheme) {
    QMutexLocker locker(&s_mutex);
    
    if (m_currentScheme) {
        delete m_currentScheme;
    }
    m_currentScheme = scheme;
    emit schemeChanged(m_currentScheme);
}

bool SchemeManager::importScheme(const QString& filePath) {
    QFileInfo info(filePath);
    QString ext = info.suffix().toLower();
    
    if (ext == "svscheme") {
        return loadScheme(filePath);
    }
    
    return false;
}

bool SchemeManager::exportScheme(Scheme* scheme, const QString& filePath) {
    return saveScheme(scheme, filePath);
}