#ifndef IOCONTROLLER_H
#define IOCONTROLLER_H

#include <QObject>
#include <QMap>

class IOController : public QObject {
    Q_OBJECT

public:
    explicit IOController(QObject* parent = nullptr);
    ~IOController() override;

    enum LineMode {
        InputMode = 0,
        OutputMode = 1,
        PulseOutput = 2
    };

    bool initialize(int totalLines = 8);
    void shutdown();

    bool setLineMode(int lineIndex, LineMode mode);
    LineMode lineMode(int lineIndex) const;

    bool setOutput(int lineIndex, bool high);
    bool readInput(int lineIndex, bool& state) const;
    bool sendPulse(int lineIndex, int durationMs);

    QMap<int, bool> readAllInputs() const;
    QMap<int, LineMode> allLineModes() const { return m_lineModes; }
    int totalLines() const { return m_totalLines; }

signals:
    void inputChanged(int lineIndex, bool state);
    void outputChanged(int lineIndex, bool state);
    void triggerDetected(int lineIndex);

private:
    int m_totalLines = 0;
    bool m_initialized = false;
    QMap<int, LineMode> m_lineModes;
    QMap<int, bool> m_outputStates;
};

#endif // IOCONTROLLER_H