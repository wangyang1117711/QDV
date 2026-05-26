#ifndef SERIALCOMMUNICATOR_H
#define SERIALCOMMUNICATOR_H

#include <QObject>
#include <QString>
#include <QByteArray>

class SerialCommunicator : public QObject {
    Q_OBJECT

public:
    explicit SerialCommunicator(QObject* parent = nullptr);
    ~SerialCommunicator() override;

    enum BaudRate {
        Baud9600 = 9600,
        Baud19200 = 19200,
        Baud38400 = 38400,
        Baud57600 = 57600,
        Baud115200 = 115200
    };

    enum DataBits {
        Data5 = 5,
        Data6 = 6,
        Data7 = 7,
        Data8 = 8
    };

    enum StopBits {
        StopOne = 1,
        StopTwo = 2
    };

    enum Parity {
        NoParity = 0,
        EvenParity = 2,
        OddParity = 3
    };

    bool open(const QString& portName,
              BaudRate baudRate = Baud115200,
              DataBits dataBits = Data8,
              StopBits stopBits = StopOne,
              Parity parity = NoParity);
    void close();
    bool isOpen() const { return m_isOpen; }

    bool configure(const QString& portName,
                   int baudRate, int dataBits,
                   int stopBits, int parity);
    bool send(const QByteArray& data);
    bool send(const QString& text);

signals:
    void dataReceived(const QByteArray& data);
    void errorOccurred(const QString& error);
    void connectionStatusChanged(bool connected);

private:
    QString m_portName;
    BaudRate m_baudRate = Baud115200;
    DataBits m_dataBits = Data8;
    StopBits m_stopBits = StopOne;
    Parity m_parity = NoParity;
    bool m_isOpen = false;
    QByteArray m_rxBuffer;
    QByteArray m_txBuffer;
};

#endif // SERIALCOMMUNICATOR_H