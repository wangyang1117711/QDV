#pragma once

#include "Monitoring/ProcessSnapshot.h"
#include <QString>
#include <QVariantMap>
#include <QMetaType>

namespace QDV {

// 异常记录
struct Anomaly {
    QString anomalyId;
    qint64 timestamp = 0;
    QString severity;    // warning / error / critical
    QString category;    // cpu / memory / disk / network / error_rate / progress
    QString message;
    ProcessSnapshot snapshot;
    QVariantMap context;
};

} // namespace QDV

Q_DECLARE_METATYPE(QDV::Anomaly)
