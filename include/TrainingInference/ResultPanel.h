#ifndef RESULT_PANEL_H
#define RESULT_PANEL_H

#include <QWidget>
#include <QTableWidget>
#include <QPushButton>
#include <QLabel>
#include <QComboBox>
#include <QList>
#include "ExportManager.h"



class ResultPanel : public QWidget {
    Q_OBJECT

public:
    explicit ResultPanel(QWidget* parent = nullptr);

    void setResults(const QList<InferenceResult>& results);
    void addResult(const InferenceResult& result);
    void clearResults();
    QList<InferenceResult> results() const;

signals:
    void exportRequested(const QString& format, const QString& filePath);

private slots:
    void onExportCSV();
    void onExportJSON();
    void onClearResults();
    void onResultClicked(int row, int column);

private:
    void setupUI();
    void drawConfidenceBar(QPainter& painter, const QRect& rect, double confidence);

    QTableWidget* m_resultTable;
    QPushButton* m_exportCsvBtn;
    QPushButton* m_exportJsonBtn;
    QPushButton* m_clearBtn;
    QLabel* m_summaryLabel;
    QWidget* m_chartWidget;
    QList<InferenceResult> m_results;
};



#endif