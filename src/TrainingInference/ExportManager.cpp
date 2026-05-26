#include "TrainingInference/ExportManager.h"
#include <QFile>
#include <QTextStream>
#include <QJsonDocument>
#include <QJsonArray>



ExportManager* ExportManager::s_instance = nullptr;

ExportManager* ExportManager::instance() {
    if (!s_instance) {
        s_instance = new ExportManager();
    }
    return s_instance;
}

ExportManager::ExportManager(QObject* parent) : QObject(parent) {
}

bool ExportManager::exportToCSV(const QString& filePath, const QList<InferenceResult>& results) {
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        emit exportFailed("无法打开文件: " + filePath);
        return false;
    }

    QTextStream stream(&file);
    stream << "ImagePath,Category,Confidence\n";
    for (const auto& r : results) {
        stream << "\"" << r.imagePath << "\",\""
               << r.category << "\","
               << r.confidence << "\n";
    }
    file.close();
    emit exportCompleted(filePath, results.size());
    return true;
}

bool ExportManager::exportToJSON(const QString& filePath, const QList<InferenceResult>& results) {
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        emit exportFailed("无法打开文件: " + filePath);
        return false;
    }

    QJsonArray arr;
    for (const auto& r : results) {
        QJsonObject obj;
        obj["imagePath"] = r.imagePath;
        obj["category"] = r.category;
        obj["confidence"] = r.confidence;
        obj["rawOutput"] = r.rawOutput;
        arr.append(obj);
    }

    QJsonDocument doc(arr);
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();
    emit exportCompleted(filePath, results.size());
    return true;
}

bool ExportManager::exportToTXT(const QString& filePath, const QList<InferenceResult>& results) {
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        emit exportFailed("无法打开文件: " + filePath);
        return false;
    }

    QTextStream stream(&file);
    for (const auto& r : results) {
        stream << "Image: " << r.imagePath << "\n"
               << "  Category: " << r.category << "\n"
               << "  Confidence: " << (r.confidence * 100.0) << "%\n\n";
    }
    file.close();
    emit exportCompleted(filePath, results.size());
    return true;
}

