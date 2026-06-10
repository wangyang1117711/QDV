#ifndef PLUGIN_VERIFIER_H
#define PLUGIN_VERIFIER_H

#include <QObject>
#include <QString>
#include <QList>

struct PluginVerifyResult {
    QString checkName;
    bool passed = false;
    QString message;
};

class PluginVerifier : public QObject {
    Q_OBJECT

public:
    explicit PluginVerifier(QObject* parent = nullptr);

    QList<PluginVerifyResult> verifyAll();
    PluginVerifyResult verifyTestPluginCreation();
    PluginVerifyResult verifyPluginLifecycle();
    PluginVerifyResult verifyPluginExecution();
    PluginVerifyResult verifyPluginManagerEnumeration();

    int passedCount() const;
    int totalCount() const;

private:
    QList<PluginVerifyResult> m_results;
};

#endif