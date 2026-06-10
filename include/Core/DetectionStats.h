#ifndef DETECTION_STATS_H
#define DETECTION_STATS_H

#include <QString>

struct DetectionStats {
    int totalDetected = 0;
    int passed = 0;
    int failed = 0;
    int alerts = 0;
    double passRate = 0.0;
    double throughputPerMin = 0.0;
    QString batchProgress;
    QString status;
};

#endif