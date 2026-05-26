#include "TrainingInference/ResultPanel.h"
#include "TrainingInference/ExportManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QPainter>
#include <QFileDialog>
#include <QMessageBox>
#include <QLabel>
#include <QGroupBox>
#include <QScrollArea>



ResultPanel::ResultPanel(QWidget* parent) : QWidget(parent) {
    setupUI();
}

void ResultPanel::setupUI() {
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);

    QHBoxLayout* headerLayout = new QHBoxLayout();
    QLabel* titleLabel = new QLabel("推理结果");
    titleLabel->setStyleSheet("font-size: 14px; font-weight: bold; color: #e0e0e0; background: transparent;");
    headerLayout->addWidget(titleLabel);

    m_summaryLabel = new QLabel("共 0 条结果");
    m_summaryLabel->setStyleSheet("color: #aaa; font-size: 12px;");
    headerLayout->addWidget(m_summaryLabel);
    headerLayout->addStretch();

    m_exportCsvBtn = new QPushButton("导出CSV");
    m_exportCsvBtn->setStyleSheet(R"(
        QPushButton {
            background-color: #3d3d3d;
            color: #e0e0e0;
            border: 1px solid #555;
            border-radius: 3px;
            padding: 5px 12px;
            font-size: 12px;
        }
        QPushButton:hover { background-color: #555; }
    )");
    connect(m_exportCsvBtn, &QPushButton::clicked, this, &ResultPanel::onExportCSV);
    headerLayout->addWidget(m_exportCsvBtn);

    m_exportJsonBtn = new QPushButton("导出JSON");
    m_exportJsonBtn->setStyleSheet(R"(
        QPushButton {
            background-color: #3d3d3d;
            color: #e0e0e0;
            border: 1px solid #555;
            border-radius: 3px;
            padding: 5px 12px;
            font-size: 12px;
        }
        QPushButton:hover { background-color: #555; }
    )");
    connect(m_exportJsonBtn, &QPushButton::clicked, this, &ResultPanel::onExportJSON);
    headerLayout->addWidget(m_exportJsonBtn);

    m_clearBtn = new QPushButton("清空");
    m_clearBtn->setStyleSheet(R"(
        QPushButton {
            background-color: #3d3d3d;
            color: #e0e0e0;
            border: 1px solid #555;
            border-radius: 3px;
            padding: 5px 12px;
            font-size: 12px;
        }
        QPushButton:hover { background-color: #555; }
    )");
    connect(m_clearBtn, &QPushButton::clicked, this, &ResultPanel::onClearResults);
    headerLayout->addWidget(m_clearBtn);

    layout->addLayout(headerLayout);

    m_resultTable = new QTableWidget(0, 5);
    m_resultTable->setColumnCount(5);
    m_resultTable->setHorizontalHeaderLabels({"图像", "类别", "置信度", "置信度柱状图", "状态"});
    m_resultTable->horizontalHeader()->setStretchLastSection(true);
    m_resultTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_resultTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_resultTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Fixed);
    m_resultTable->setColumnWidth(3, 150);
    m_resultTable->setColumnWidth(4, 80);
    m_resultTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_resultTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_resultTable->verticalHeader()->setVisible(false);
    m_resultTable->setAlternatingRowColors(true);
    m_resultTable->setStyleSheet(R"(
        QTableWidget {
            background-color: #252525;
            border: 1px solid #444;
            border-radius: 4px;
            gridline-color: #444;
            color: #e0e0e0;
        }
        QTableWidget::item { padding: 4px 8px; }
        QTableWidget::item:selected { background-color: #660874; color: #fff; }
        QHeaderView::section {
            background-color: #333;
            color: #e0e0e0;
            border: none;
            border-bottom: 2px solid #660874;
            padding: 6px 8px;
            font-weight: bold;
        }
    )");
    connect(m_resultTable, &QTableWidget::cellClicked, this, &ResultPanel::onResultClicked);
    layout->addWidget(m_resultTable, 2);

    QGroupBox* chartGroup = new QGroupBox("置信度概览");
    m_chartWidget = new QWidget();
    m_chartWidget->setMinimumHeight(80);
    m_chartWidget->setStyleSheet("background-color: #252525; border-radius: 4px;");
    QVBoxLayout* chartLayout = new QVBoxLayout(chartGroup);
    chartLayout->addWidget(m_chartWidget);
    layout->addWidget(chartGroup);
}

void ResultPanel::setResults(const QList<InferenceResult>& results) {
    m_results = results;
    m_resultTable->setRowCount(0);
    m_resultTable->setRowCount(results.size());

    for (int i = 0; i < results.size(); ++i) {
        const InferenceResult& r = results[i];

        QFileInfo fi(r.imagePath);
        m_resultTable->setItem(i, 0, new QTableWidgetItem(fi.fileName()));

        QTableWidgetItem* catItem = new QTableWidgetItem(r.category);
        catItem->setForeground(QColor(102, 8, 116));
        catItem->setFont(QFont("Microsoft YaHei", 9, QFont::Bold));
        m_resultTable->setItem(i, 1, catItem);

        m_resultTable->setItem(i, 2, new QTableWidgetItem(
            QString("%1%").arg(QString::number(r.confidence * 100.0, 'f', 1))));

        QTableWidgetItem* barItem = new QTableWidgetItem();
        m_resultTable->setItem(i, 3, barItem);

        QString status = r.confidence > 0.8 ? "通过" : (r.confidence > 0.5 ? "存疑" : "拒绝");
        QTableWidgetItem* statusItem = new QTableWidgetItem(status);
        statusItem->setTextAlignment(Qt::AlignCenter);
        if (status == "通过") {
            statusItem->setBackground(QColor(39, 174, 96));
            statusItem->setForeground(Qt::white);
        } else if (status == "存疑") {
            statusItem->setBackground(QColor(243, 156, 18));
            statusItem->setForeground(Qt::white);
        } else {
            statusItem->setBackground(QColor(231, 76, 60));
            statusItem->setForeground(Qt::white);
        }
        m_resultTable->setItem(i, 4, statusItem);
    }

    m_summaryLabel->setText(QString("共 %1 条结果").arg(results.size()));

    m_chartWidget->update();
}

void ResultPanel::addResult(const InferenceResult& result) {
    m_results.append(result);
    setResults(m_results);
}

void ResultPanel::clearResults() {
    m_results.clear();
    m_resultTable->setRowCount(0);
    m_summaryLabel->setText("共 0 条结果");
    m_chartWidget->update();
}

QList<InferenceResult> ResultPanel::results() const {
    return m_results;
}

void ResultPanel::onExportCSV() {
    if (m_results.isEmpty()) return;
    QString path = QFileDialog::getSaveFileName(this, "导出CSV", "", "CSV Files (*.csv)");
    if (path.isEmpty()) return;
    ExportManager::instance()->exportToCSV(path, m_results);
}

void ResultPanel::onExportJSON() {
    if (m_results.isEmpty()) return;
    QString path = QFileDialog::getSaveFileName(this, "导出JSON", "", "JSON Files (*.json)");
    if (path.isEmpty()) return;
    ExportManager::instance()->exportToJSON(path, m_results);
}

void ResultPanel::onClearResults() {
    clearResults();
}

void ResultPanel::onResultClicked(int row, int column) {
    Q_UNUSED(column);
    Q_UNUSED(row);
}

void ResultPanel::drawConfidenceBar(QPainter& painter, const QRect& rect, double confidence) {
    painter.save();
    painter.setPen(Qt::NoPen);

    QColor barColor;
    if (confidence > 0.8) {
        barColor = QColor(39, 174, 96);
    } else if (confidence > 0.5) {
        barColor = QColor(243, 156, 18);
    } else {
        barColor = QColor(231, 76, 60);
    }

    painter.setBrush(QColor(60, 60, 60));
    painter.drawRoundedRect(rect, 3, 3);

    int barWidth = static_cast<int>(rect.width() * confidence);
    QRect barRect(rect.x(), rect.y(), barWidth, rect.height());
    painter.setBrush(barColor);
    painter.drawRoundedRect(barRect, 3, 3);

    painter.setPen(Qt::white);
    painter.setFont(QFont("Microsoft YaHei", 8));
    painter.drawText(rect, Qt::AlignCenter,
                     QString("%1%").arg(QString::number(confidence * 100.0, 'f', 1)));

    painter.restore();
}

