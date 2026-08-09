#include "ModelNotesDialog.h"
#include "ZeroShotKit/ModelNotesManager.h"

#include <QVBoxLayout>
#include <QGroupBox>
#include <QScrollArea>
#include <QPushButton>

ModelNotesDialog::ModelNotesDialog(zsu::ModelNotesManager* manager,
                                     zsu::ZeroShotModelType modelType,
                                     QWidget* parent)
    : QDialog(parent), m_manager(manager), m_modelType(modelType)
{
    setupUI();
    loadNotes();
}

void ModelNotesDialog::setupUI() {
    setWindowTitle(tr("模型注意事项"));
    setMinimumSize(500, 400);
    setModal(true);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setSpacing(8);

    // 标题
    m_titleLabel = new QLabel(this);
    m_titleLabel->setStyleSheet("font-size: 16px; font-weight: bold; color: #4ec9b0;");
    layout->addWidget(m_titleLabel);

    // 简述
    m_descLabel = new QLabel(this);
    m_descLabel->setWordWrap(true);
    m_descLabel->setStyleSheet("color: #dcdcaa; font-size: 12px;");
    layout->addWidget(m_descLabel);

    // 内容区（只读）
    m_contentEdit = new QTextEdit(this);
    m_contentEdit->setReadOnly(true);
    m_contentEdit->setStyleSheet(
        "QTextEdit { background-color: #1e1e1e; color: #cccccc; "
        "border: 1px solid #3a3a3e; font-size: 12px; }");
    layout->addWidget(m_contentEdit, 1);

    // 关闭按钮
    QPushButton* closeBtn = new QPushButton(tr("关闭"), this);
    closeBtn->setMinimumHeight(32);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    layout->addWidget(closeBtn);
}

void ModelNotesDialog::loadNotes() {
    if (!m_manager) return;

    zsu::ModelNote note = m_manager->getNote(m_modelType);

    m_titleLabel->setText(note.displayName);
    m_descLabel->setText(note.description);

    QString html;
    html += "<html><body style='color: #cccccc;'>";

    // 注意事项
    if (!note.notes.isEmpty()) {
        html += "<h3 style='color: #4ec9b0;'>注意事项</h3><ul>";
        for (const auto& s : note.notes) {
            html += QString("<li>%1</li>").arg(s.toHtmlEscaped());
        }
        html += "</ul>";
    }

    // 输入格式
    if (!note.inputFormat.isEmpty()) {
        html += "<h3 style='color: #4ec9b0;'>输入格式</h3><ul>";
        for (const auto& s : note.inputFormat) {
            html += QString("<li>%1</li>").arg(s.toHtmlEscaped());
        }
        html += "</ul>";
    }

    // 限制条件
    if (!note.limitations.isEmpty()) {
        html += "<h3 style='color: #f44747;'>限制条件</h3><ul>";
        for (const auto& s : note.limitations) {
            html += QString("<li>%1</li>").arg(s.toHtmlEscaped());
        }
        html += "</ul>";
    }

    // 使用建议
    if (!note.tips.isEmpty()) {
        html += "<h3 style='color: #dcdcaa;'>使用建议</h3><ul>";
        for (const auto& s : note.tips) {
            html += QString("<li>%1</li>").arg(s.toHtmlEscaped());
        }
        html += "</ul>";
    }

    html += "</body></html>";
    m_contentEdit->setHtml(html);
}
