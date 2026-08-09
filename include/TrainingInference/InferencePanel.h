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
    void refreshModelList();
    QString currentModel() const;
    QString currentModelPath() const;
    bool isBatchMode() const;
    void setStatus(const QString& text, bool isError = false);

    /**
     * 更新推理进度条与状态文本。
     * @param value  当前已完成数量
     * @param total  总数量；<=0 时切换为忙碌动画
     * @param status 状态提示文本（如"处理中..."）
     */
    void setProgress(int value, int total, const QString& status = QString());

public slots:
    void setCurrentModelPath(const QString& path);

signals:
    void inferenceRequested(const QString& modelPath, const QStringList& imagePaths);
    void modelSelected(const QString& modelPath);
    void stopInferenceRequested();

private slots:
    void onRunInference();
    void onStopInference();
    void onModelChanged(int index);
    void onBatchToggled(bool checked);
    void onDefaultModelLoadFailed(const QString& error);

public:
    void resetButtons();

private:
    void setupUI();
    void updateModelInfo(const QString& modelName);

    QComboBox* m_modelCombo;
    QPushButton* m_runBtn;
    QPushButton* m_stopBtn;
    QProgressBar* m_progressBar;
    QLabel* m_statusLabel;
    QListWidget* m_modelInfoList;
    QCheckBox* m_batchCheckBox;
    QLabel* m_modelPathLabel;
    QString m_explicitModelPath;  // 模型库显式选中的路径
};



#endif