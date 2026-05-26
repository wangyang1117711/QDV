#ifndef COMM_VIEW_H
#define COMM_VIEW_H

#include <QWidget>

class QTcpSocket;
class QTextEdit;
class QLineEdit;
class QAction;

class CommView : public QWidget {
    Q_OBJECT

public:
    explicit CommView(QWidget* parent = nullptr);
    ~CommView();

private slots:
    void onTcpConnected();
    void onTcpReadyRead();
    void onTcpDisconnected();
    void onTcpError();

private:
    void setupUI();
    void tcpConnect(const QString& host, quint16 port);
    void tcpDisconnect();
    void tcpSend(const QString& data);

    QTcpSocket* m_tcpSocket;
    bool m_isSerialMode;
};

#endif // COMM_VIEW_H