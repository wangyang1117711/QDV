#ifndef SCRIPT_EDITOR_WIDGET_H
#define SCRIPT_EDITOR_WIDGET_H

#include <QWidget>
#include <QPlainTextEdit>
#include <QTextEdit>
#include <QPushButton>
#include <QLabel>
#include <QProcess>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QRegularExpression>



class PythonHighlighter : public QSyntaxHighlighter {
    Q_OBJECT

public:
    explicit PythonHighlighter(QTextDocument* parent = nullptr);

protected:
    void highlightBlock(const QString& text) override;

private:
    struct HighlightRule {
        QRegularExpression pattern;
        QTextCharFormat format;
    };
    QList<HighlightRule> m_rules;
};

class ScriptEditorWidget : public QWidget {
    Q_OBJECT

public:
    explicit ScriptEditorWidget(QWidget* parent = nullptr);

    void setScript(const QString& script);
    QString script() const;
    void setBreakpoints(const QList<int>& lines);
    QList<int> breakpoints() const;

signals:
    void scriptExecuted(const QString& output);
    void executionError(const QString& error);

private slots:
    void onRunScript();
    void onStopScript();
    void onClearOutput();
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);
    void onProcessOutput();
    void onProcessError();
    void onMarginClicked(int line);

private:
    void setupUI();

    QPlainTextEdit* m_editor;
    QTextEdit* m_outputArea;
    QPushButton* m_runBtn;
    QPushButton* m_stopBtn;
    QPushButton* m_clearBtn;
    QLabel* m_statusLabel;
    QProcess* m_process;
    PythonHighlighter* m_highlighter;
    QList<int> m_breakpoints;
};



#endif