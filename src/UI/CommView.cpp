#include "CommView.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QToolBar>
#include <QAction>
#include <QTextEdit>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QGroupBox>
#include <QSpinBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QTcpSocket>
#include <QDateTime>

CommView::CommView(QWidget* parent) : QWidget(parent), m_tcpSocket(nullptr), m_isSerialMode(false) {
    setupUI();
}

CommView::~CommView() {
    tcpDisconnect();
}

void CommView::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    QToolBar* toolbar = new QToolBar();
    toolbar->setStyleSheet(R"(
        QToolBar {
            background-color: #2d2d2d;
            border-bottom: 1px solid #444;
            padding: 3px 12px;
            spacing: 6px;
        }
        QToolBar QToolButton {
            background: transparent;
            border: 1px solid transparent;
            border-radius: 4px;
            padding: 4px 14px;
            color: #e0e0e0;
            font-size: 12px;
        }
        QToolBar QToolButton:hover {
            background-color: #555;
            border-color: #555;
        }
        QToolBar QToolButton:disabled {
            color: #666;
        }
    )");

    QAction* connectAction = toolbar->addAction("连接");
    QAction* disconnectAction = toolbar->addAction("断开");
    disconnectAction->setEnabled(false);
    QAction* clearAction = toolbar->addAction("清空日志");

    mainLayout->addWidget(toolbar);

    QWidget* contentWidget = new QWidget();
    QVBoxLayout* contentLayout = new QVBoxLayout(contentWidget);
    contentLayout->setContentsMargins(20, 12, 20, 12);
    contentLayout->setSpacing(10);

    // v5.0：网格布局，全屏下更充分利用空间
    QGroupBox* configGroup = new QGroupBox("连接配置");
    QGridLayout* configLayout = new QGridLayout(configGroup);
    configLayout->setSpacing(10);
    configLayout->setContentsMargins(12, 16, 12, 12);

    QLabel* addrLabel = new QLabel("地址:");
    addrLabel->setStyleSheet("font-weight: bold;");
    QLineEdit* addrEdit = new QLineEdit("127.0.0.1");
    addrEdit->setFixedWidth(200);

    QLabel* portLabel = new QLabel("端口:");
    portLabel->setStyleSheet("font-weight: bold;");
    QSpinBox* portSpin = new QSpinBox();
    portSpin->setRange(1, 65535);
    portSpin->setValue(8000);
    portSpin->setFixedWidth(120);

    QLabel* baudLabel = new QLabel("波特率:");
    baudLabel->setStyleSheet("font-weight: bold;");
    QComboBox* baudCombo = new QComboBox();
    baudCombo->addItems({"9600", "19200", "38400", "57600", "115200"});
    baudCombo->setFixedWidth(120);
    baudCombo->setVisible(false);

    configLayout->addWidget(addrLabel, 0, 0);
    configLayout->addWidget(addrEdit, 0, 1);
    configLayout->addWidget(portLabel, 0, 2);
    configLayout->addWidget(portSpin, 0, 3);
    configLayout->addWidget(baudLabel, 0, 4);
    configLayout->addWidget(baudCombo, 0, 5);
    configLayout->setColumnStretch(6, 1);

    contentLayout->addWidget(configGroup);

    QTextEdit* logEdit = new QTextEdit();
    logEdit->setReadOnly(true);
    logEdit->setPlaceholderText("通信日志...");
    contentLayout->addWidget(logEdit);

    QWidget* sendWidget = new QWidget();
    QHBoxLayout* sendLayout = new QHBoxLayout(sendWidget);
    sendLayout->setContentsMargins(0, 0, 0, 0);
    sendLayout->setSpacing(8);

    QLineEdit* sendEdit = new QLineEdit();
    sendEdit->setPlaceholderText("输入要发送的数据...");

    QPushButton* sendBtn = new QPushButton("发送");
    sendBtn->setFixedWidth(80);

    sendLayout->addWidget(sendEdit);
    sendLayout->addWidget(sendBtn);
    contentLayout->addWidget(sendWidget);

    contentLayout->addStretch();
    mainLayout->addWidget(contentWidget);

    connect(connectAction, &QAction::triggered, [this, connectAction, disconnectAction, addrEdit, portSpin, logEdit]() {
        QString host = addrEdit->text().trimmed();
        quint16 port = static_cast<quint16>(portSpin->value());
        tcpConnect(host, port);
        connectAction->setEnabled(false);
        disconnectAction->setEnabled(true);
        logEdit->append(QString("[%1] 已连接到 %2:%3")
            .arg(QDateTime::currentDateTime().toString("HH:mm:ss"))
            .arg(host)
            .arg(port));
    });

    connect(disconnectAction, &QAction::triggered, [this, connectAction, disconnectAction, logEdit]() {
        tcpDisconnect();
        connectAction->setEnabled(true);
        disconnectAction->setEnabled(false);
        logEdit->append(QString("[%1] 已断开连接")
            .arg(QDateTime::currentDateTime().toString("HH:mm:ss")));
    });

    connect(clearAction, &QAction::triggered, [logEdit]() {
        logEdit->clear();
    });

    connect(sendBtn, &QPushButton::clicked, [this, sendEdit, logEdit]() {
        QString data = sendEdit->text().trimmed();
        if (data.isEmpty()) return;
        tcpSend(data);
        logEdit->append(QString("[%1] 发送: %2")
            .arg(QDateTime::currentDateTime().toString("HH:mm:ss"))
            .arg(data));
        sendEdit->clear();
    });

    connect(sendEdit, &QLineEdit::returnPressed, sendBtn, &QPushButton::click);
}

void CommView::tcpConnect(const QString& host, quint16 port) {
    if (!m_tcpSocket) {
        m_tcpSocket = new QTcpSocket(this);
        connect(m_tcpSocket, &QTcpSocket::connected, this, &CommView::onTcpConnected);
        connect(m_tcpSocket, &QTcpSocket::readyRead, this, &CommView::onTcpReadyRead);
        connect(m_tcpSocket, &QTcpSocket::disconnected, this, &CommView::onTcpDisconnected);
        connect(m_tcpSocket, &QTcpSocket::errorOccurred, this, &CommView::onTcpError);
    }
    m_tcpSocket->connectToHost(host, port);
}

void CommView::tcpDisconnect() {
    if (m_tcpSocket && m_tcpSocket->state() != QAbstractSocket::UnconnectedState) {
        m_tcpSocket->disconnectFromHost();
    }
}

void CommView::tcpSend(const QString& data) {
    if (m_tcpSocket && m_tcpSocket->state() == QAbstractSocket::ConnectedState) {
        m_tcpSocket->write(data.toUtf8());
    }
}

void CommView::onTcpConnected() {
}

void CommView::onTcpReadyRead() {
}

void CommView::onTcpDisconnected() {
}

void CommView::onTcpError() {
}