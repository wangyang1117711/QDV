#ifndef COMMUNICATION_VERIFIER_H
#define COMMUNICATION_VERIFIER_H

#include <QObject>
#include <QString>
#include <QList>

struct CommVerifyResult {
    QString componentName;
    bool passed = false;
    QString message;
};

class CommunicationVerifier : public QObject {
    Q_OBJECT

public:
    explicit CommunicationVerifier(QObject* parent = nullptr);

    QList<CommVerifyResult> verifyAll();
    CommVerifyResult verifyTCPLoopback();
    CommVerifyResult verifySerialEnumeration();
    CommVerifyResult verifyIOController();

    int passedCount() const;
    int totalCount() const;

private:
    QList<CommVerifyResult> m_results;
};

#endif