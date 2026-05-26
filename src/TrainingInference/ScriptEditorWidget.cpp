#include "TrainingInference/ScriptEditorWidget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QLabel>
#include <QFont>
#include <QPainter>



PythonHighlighter::PythonHighlighter(QTextDocument* parent) : QSyntaxHighlighter(parent) {
    QTextCharFormat keywordFormat;
    keywordFormat.setForeground(QColor(197, 134, 192));
    keywordFormat.setFontWeight(QFont::Bold);

    QStringList keywords = {
        "def", "class", "return", "if", "elif", "else", "for", "while",
        "import", "from", "try", "except", "finally", "raise", "with",
        "as", "in", "is", "not", "and", "or", "True", "False", "None",
        "print", "lambda", "yield", "pass", "break", "continue", "global",
        "nonlocal", "assert", "del"
    };

    for (const QString& kw : keywords) {
        HighlightRule rule;
        rule.pattern = QRegularExpression("\\b" + kw + "\\b");
        rule.format = keywordFormat;
        m_rules.append(rule);
    }

    QTextCharFormat stringFormat;
    stringFormat.setForeground(QColor(206, 145, 120));
    {
        HighlightRule rule;
        rule.pattern = QRegularExpression("\"[^\"]*\"");
        rule.format = stringFormat;
        m_rules.append(rule);
    }
    {
        HighlightRule rule;
        rule.pattern = QRegularExpression("'[^']*'");
        rule.format = stringFormat;
        m_rules.append(rule);
    }

    QTextCharFormat commentFormat;
    commentFormat.setForeground(QColor(106, 153, 85));
    {
        HighlightRule rule;
        rule.pattern = QRegularExpression("#[^\n]*");
        rule.format = commentFormat;
        m_rules.append(rule);
    }

    QTextCharFormat numberFormat;
    numberFormat.setForeground(QColor(181, 206, 168));
    {
        HighlightRule rule;
        rule.pattern = QRegularExpression("\\b[0-9]+(\\.[0-9]+)?\\b");
        rule.format = numberFormat;
        m_rules.append(rule);
    }

    QTextCharFormat decoratorFormat;
    decoratorFormat.setForeground(QColor(86, 156, 214));
    {
        HighlightRule rule;
        rule.pattern = QRegularExpression("@\\w+");
        rule.format = decoratorFormat;
        m_rules.append(rule);
    }
}

void PythonHighlighter::highlightBlock(const QString& text) {
    for (const HighlightRule& rule : m_rules) {
        QRegularExpressionMatchIterator it = rule.pattern.globalMatch(text);
        while (it.hasNext()) {
            QRegularExpressionMatch match = it.next();
            setFormat(match.capturedStart(), match.capturedLength(), rule.format);
        }
    }
}

ScriptEditorWidget::ScriptEditorWidget(QWidget* parent) : QWidget(parent), m_process(nullptr) {
    setupUI();
}

void ScriptEditorWidget::setupUI() {
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);

    QHBoxLayout* toolbar = new QHBoxLayout();
    toolbar->setSpacing(6);

    QLabel* titleLabel = new QLabel("Python 脚本编辑器");
    titleLabel->setStyleSheet("font-size: 14px; font-weight: bold; color: #e0e0e0; background: transparent;");
    toolbar->addWidget(titleLabel);
    toolbar->addStretch();

    m_runBtn = new QPushButton("运行");
    m_runBtn->setStyleSheet(R"(
        QPushButton {
            background-color: #4CAF50;
            color: white;
            border: none;
            border-radius: 3px;
            padding: 5px 14px;
            font-size: 12px;
            font-weight: bold;
        }
        QPushButton:hover { background-color: #66BB6A; }
        QPushButton:disabled { background-color: #555; }
    )");
    connect(m_runBtn, &QPushButton::clicked, this, &ScriptEditorWidget::onRunScript);
    toolbar->addWidget(m_runBtn);

    m_stopBtn = new QPushButton("停止");
    m_stopBtn->setEnabled(false);
    m_stopBtn->setStyleSheet(R"(
        QPushButton {
            background-color: #c62828;
            color: white;
            border: none;
            border-radius: 3px;
            padding: 5px 14px;
            font-size: 12px;
            font-weight: bold;
        }
        QPushButton:hover { background-color: #e53935; }
        QPushButton:disabled { background-color: #555; }
    )");
    connect(m_stopBtn, &QPushButton::clicked, this, &ScriptEditorWidget::onStopScript);
    toolbar->addWidget(m_stopBtn);

    m_clearBtn = new QPushButton("清空");
    m_clearBtn->setStyleSheet(R"(
        QPushButton {
            background-color: #3d3d3d;
            color: #e0e0e0;
            border: 1px solid #555;
            border-radius: 3px;
            padding: 5px 14px;
            font-size: 12px;
        }
        QPushButton:hover { background-color: #555; }
    )");
    connect(m_clearBtn, &QPushButton::clicked, this, &ScriptEditorWidget::onClearOutput);
    toolbar->addWidget(m_clearBtn);

    layout->addLayout(toolbar);

    QSplitter* splitter = new QSplitter(Qt::Vertical);

    m_editor = new QPlainTextEdit();
    m_editor->setStyleSheet(R"(
        QPlainTextEdit {
            background-color: #1a1a2e;
            border: 1px solid #444;
            border-radius: 4px;
            color: #e0e0e0;
            font-family: 'Consolas', 'Courier New', monospace;
            font-size: 13px;
            selection-background-color: #660874;
            padding: 8px;
        }
    )");
    m_editor->setTabStopDistance(32);
    m_editor->setLineWrapMode(QPlainTextEdit::NoWrap);

    QFont monoFont("Consolas", 12);
    m_editor->setFont(monoFont);

    m_highlighter = new PythonHighlighter(m_editor->document());

    m_editor->setPlainText(R"(import cv2
import numpy as np

def preprocess_image(image_path):
    img = cv2.imread(image_path)
    if img is None:
        return None
    img = cv2.cvtColor(img, cv2.COLOR_BGR2RGB)
    img = cv2.resize(img, (224, 224))
    img = img.astype(np.float32) / 255.0
    return img

def main():
    print("图像预处理脚本已加载")
    print("等待推理任务...")

if __name__ == "__main__":
    main()
)");

    splitter->addWidget(m_editor);

    m_outputArea = new QTextEdit();
    m_outputArea->setReadOnly(true);
    m_outputArea->setStyleSheet(R"(
        QTextEdit {
            background-color: #1a1a1a;
            border: 1px solid #444;
            border-radius: 4px;
            color: #aaa;
            font-family: 'Consolas', 'Courier New', monospace;
            font-size: 12px;
            padding: 8px;
        }
    )");
    splitter->addWidget(m_outputArea);

    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 1);

    layout->addWidget(splitter, 1);

    m_statusLabel = new QLabel("就绪");
    m_statusLabel->setStyleSheet("color: #aaa; font-size: 11px; padding: 2px;");
    layout->addWidget(m_statusLabel);
}

void ScriptEditorWidget::setScript(const QString& script) {
    m_editor->setPlainText(script);
}

QString ScriptEditorWidget::script() const {
    return m_editor->toPlainText();
}

void ScriptEditorWidget::setBreakpoints(const QList<int>& lines) {
    m_breakpoints = lines;
}

QList<int> ScriptEditorWidget::breakpoints() const {
    return m_breakpoints;
}

void ScriptEditorWidget::onRunScript() {
    QString code = m_editor->toPlainText().trimmed();
    if (code.isEmpty()) return;

    m_outputArea->clear();
    m_statusLabel->setText("运行中...");
    m_statusLabel->setStyleSheet("color: #42A5F5; font-size: 11px; padding: 2px;");
    m_runBtn->setEnabled(false);
    m_stopBtn->setEnabled(true);

    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::SeparateChannels);
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &ScriptEditorWidget::onProcessFinished);
    connect(m_process, &QProcess::readyReadStandardOutput, this, &ScriptEditorWidget::onProcessOutput);
    connect(m_process, &QProcess::readyReadStandardError, this, &ScriptEditorWidget::onProcessError);

    m_process->start("python", QStringList() << "-c" << code);
}

void ScriptEditorWidget::onStopScript() {
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_process->kill();
    }
}

void ScriptEditorWidget::onClearOutput() {
    m_outputArea->clear();
}

void ScriptEditorWidget::onProcessFinished(int exitCode, QProcess::ExitStatus status) {
    m_runBtn->setEnabled(true);
    m_stopBtn->setEnabled(false);
    m_process->deleteLater();
    m_process = nullptr;

    if (status == QProcess::NormalExit && exitCode == 0) {
        m_statusLabel->setText("执行成功");
        m_statusLabel->setStyleSheet("color: #4CAF50; font-size: 11px; padding: 2px;");
        emit scriptExecuted(m_outputArea->toPlainText());
    } else {
        m_statusLabel->setText(QString("退出码: %1").arg(exitCode));
        m_statusLabel->setStyleSheet("color: #F44336; font-size: 11px; padding: 2px;");
    }
}

void ScriptEditorWidget::onProcessOutput() {
    if (m_process) {
        QString output = QString::fromUtf8(m_process->readAllStandardOutput());
        m_outputArea->append(output);
    }
}

void ScriptEditorWidget::onProcessError() {
    if (m_process) {
        QString error = QString::fromUtf8(m_process->readAllStandardError());
        m_outputArea->append("<span style='color:#F44336;'>" + error + "</span>");
        emit executionError(error);
    }
}

void ScriptEditorWidget::onMarginClicked(int line) {
    if (m_breakpoints.contains(line)) {
        m_breakpoints.removeOne(line);
    } else {
        m_breakpoints.append(line);
    }
    std::sort(m_breakpoints.begin(), m_breakpoints.end());
}

