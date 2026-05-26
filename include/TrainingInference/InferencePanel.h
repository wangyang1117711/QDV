#ifndef INFERENCE_PANEL_H
#define INFERENCE_PANEL_H

#include <QWidget>
#include <QComboBox>
#include <QPushButton>
#include <QProgressBar>
#include <QLabel>
#include <QListWidget>
#include <QCheckBox>



class InferencePanel : public QWidget {
    Q_OBJECT

public:
    explicit InferencePanel(QWidget* parent = nullptr);

    void setModelList(const QStringList& models);
    QString currentModel() const;
    bool isBatchMode() const;

signals:
    void inferenceRequested(const QString& modelPath, const QStringList& imagePaths);
    void modelSelected(const QString& modelPath);

private slots:
    void onRunInference();
    void onModelChanged(int index);
    void onBatchToggled(bool checked);

private:
    void setupUI();

    QComboBox* m_modelCombo;
    QPushButton* m_runBtn;
    QPushButton* m_stopBtn;
    QProgressBar* m_progressBar;
    QLabel* m_statusLabel;
    QListWidget* m_modelInfoList;
    QCheckBox* m_batchCheckBox;
};



#endif