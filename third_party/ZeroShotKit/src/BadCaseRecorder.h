#ifndef BADCASERECORDER_H
#define BADCASERECORDER_H

// ============================================================================
// Bad Case 记录器
// 记录推理结果与用户修正后的结果，用于后续模型优化与回归测试
// ============================================================================

#include <QObject>
#include <QString>
#include <QList>
#include <QJsonObject>
#include <QJsonArray>
#include "ZeroShotTypes.h"

namespace zsu {

class BadCaseRecorder : public QObject {
    Q_OBJECT

public:
    explicit BadCaseRecorder(QObject* parent = nullptr);

    // 记录一条 bad case
    void record(const BadCaseRecord& entry);

    // 获取所有记录
    const QList<BadCaseRecord>& records() const { return m_records; }

    // 清空所有记录
    void clear();

    // 导出为 JSON 文件
    bool exportToJson(const QString& filePath) const;

    // 从 JSON 文件导入
    bool importFromJson(const QString& filePath);

    // 获取记录数
    int count() const { return m_records.size(); }

signals:
    void recordAdded(const BadCaseRecord& entry);
    void recordsCleared();

private:
    QList<BadCaseRecord> m_records;
};

} // namespace zsu

#endif // BADCASERECORDER_H
