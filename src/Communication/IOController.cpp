#include "IOController.h"
#include "Core/Logger.h"

using namespace QDV;

IOController::IOController(QObject* parent) : QObject(parent) {}

IOController::~IOController() {
    shutdown();
}

bool IOController::initialize(int totalLines) {
    if (m_initialized) {
        shutdown();
    }

    if (totalLines < 1 || totalLines > 32) {
        Logger::error("Invalid number of IO lines: " + QString::number(totalLines));
        return false;
    }

    m_totalLines = totalLines;
    for (int i = 0; i < totalLines; ++i) {
        m_lineModes[i] = InputMode;
        m_outputStates[i] = false;
    }

    m_initialized = true;
    Logger::info("IO controller initialized with " + QString::number(totalLines) + " lines");
    return true;
}

void IOController::shutdown() {
    if (m_initialized) {
        setOutput(0, false);
        m_lineModes.clear();
        m_outputStates.clear();
        m_totalLines = 0;
        m_initialized = false;
        Logger::info("IO controller shutdown");
    }
}

bool IOController::setLineMode(int lineIndex, LineMode mode) {
    if (!m_initialized || lineIndex < 0 || lineIndex >= m_totalLines) {
        return false;
    }

    m_lineModes[lineIndex] = mode;
    Logger::info(QString("Line %1 mode set to %2").arg(lineIndex).arg(mode));
    return true;
}

IOController::LineMode IOController::lineMode(int lineIndex) const {
    if (lineIndex < 0 || lineIndex >= m_totalLines) {
        return InputMode;
    }
    return m_lineModes.value(lineIndex, InputMode);
}

bool IOController::setOutput(int lineIndex, bool high) {
    if (!m_initialized || lineIndex < 0 || lineIndex >= m_totalLines) {
        return false;
    }
    if (m_lineModes.value(lineIndex) == InputMode) {
        Logger::error("Cannot set output on input line " + QString::number(lineIndex));
        return false;
    }

    m_outputStates[lineIndex] = high;
    emit outputChanged(lineIndex, high);
    return true;
}

bool IOController::readInput(int lineIndex, bool& state) const {
    if (!m_initialized || lineIndex < 0 || lineIndex >= m_totalLines) {
        return false;
    }
    state = m_outputStates.value(lineIndex, false);
    return true;
}

bool IOController::sendPulse(int lineIndex, int durationMs) {
    if (!m_initialized || lineIndex < 0 || lineIndex >= m_totalLines) {
        return false;
    }
    if (m_lineModes.value(lineIndex) != PulseOutput) {
        Logger::error("Line " + QString::number(lineIndex) + " is not in pulse output mode");
        return false;
    }
    if (durationMs < 0 || durationMs > 10000) {
        Logger::error("Invalid pulse duration: " + QString::number(durationMs) + "ms");
        return false;
    }

    return setOutput(lineIndex, true);
}

QMap<int, bool> IOController::readAllInputs() const {
    return m_outputStates;
}