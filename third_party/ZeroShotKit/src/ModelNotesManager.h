#ifndef MODELNOTESMANAGER_H
#define MODELNOTESMANAGER_H

// ============================================================================
// 模型注意事项管理器
// 从 JSON 文件加载各模型的注意事项，支持默认文件 + 用户覆盖
// ============================================================================

#include <QObject>
#include <QString>
#include <QMap>
#include <QJsonObject>
#include "ZeroShotTypes.h"

namespace zsu {

class ModelNotesManager : public QObject {
    Q_OBJECT

public:
    explicit ModelNotesManager(QObject* parent = nullptr);

    // 加载默认注意事项（从 resources/models/model_notes.json）
    bool loadDefaultNotes(const QString& defaultPath);

    // 加载用户覆盖的注意事项（从用户配置目录）
    bool loadUserOverride(const QString& userPath);

    // 保存用户修改到用户配置目录
    bool saveUserOverride(const QString& userPath) const;

    // 获取指定模型类型的注意事项
    ModelNote getNote(ZeroShotModelType type) const;

    // 设置/更新指定模型类型的注意事项（会保存到用户覆盖文件）
    void setNote(ZeroShotModelType type, const ModelNote& note);

    // 获取所有模型注意事项
    QList<ModelNote> allNotes() const;

    // 模型类型枚举转字符串 key
    static QString modelTypeKey(ZeroShotModelType type);

    // 字符串 key 转模型类型枚举
    static ZeroShotModelType keyToModelType(const QString& key);

private:
    // modelTypeKey → ModelNote
    QMap<QString, ModelNote> m_notes;

    // 从 JSON 对象解析 ModelNote
    ModelNote parseNote(const QString& key, const QJsonObject& obj) const;

    // ModelNote 转 JSON 对象
    QJsonObject noteToJson(const ModelNote& note) const;
};

} // namespace zsu

#endif // MODELNOTESMANAGER_H
