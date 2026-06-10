#include "SerialCommunicator.h"
#include "Core/Logger.h"

using namespace QDV;

SerialCommunicator::SerialCommunicator(QObject* parent) : QObject(parent) {}

SerialCommunicator::~SerialCommunicator() {
    close();
}

bool SerialCommunicator::open(const QString& portName, BaudRate baudRate,
                              DataBits dataBits, StopBits stopBits, Parity parity) {
    if (m_isOpen) {
        close();
    }

    m_portName = portName;
    m_baudRate = baudRate;
    m_dataBits = dataBits;
    m_stopBits = stopBits;
    m_parity = parity;
    m_isOpen = true;

    emit connectionStatusChanged(true);
    Logger::info(QString("Serial port opened: %1 @ %2 baud").arg(portName).arg(static_cast<int>(baudRate)));
    return true;
}

void SerialCommunicator::close() {
    if (m_isOpen) {
        m_isOpen = false;
        emit connectionStatusChanged(false);
        Logger::info("Serial port closed: " + m_portName);
    }
}

bool SerialCommunicator::configure(const QString& portName,
                                   int baudRate, int dataBits,
                                   int stopBits, int parity) {
    if (dataBits < 5 || dataBits > 8) {
        Logger::error("Invalid data bits configuration");
        return false;
    }
    if (stopBits != 1 && stopBits != 2) {
        Logger::error("Invalid stop bits configuration");
        return false;
    }
    if (parity < 0 || parity > 3) {
        Logger::error("Invalid parity configuration");
        return false;
    }

    return open(portName, static_cast<BaudRate>(baudRate),
                static_cast<DataBits>(dataBits),
                static_cast<StopBits>(stopBits),
                static_cast<Parity>(parity));
}

bool SerialCommunicator::send(const QByteArray& data) {
    if (!m_isOpen) {
        Logger::error("Cannot send data: port not open");
        return false;
    }

    m_txBuffer.append(data);
    return true;
}

bool SerialCommunicator::send(const QString& text) {
    return send(text.toUtf8());
}