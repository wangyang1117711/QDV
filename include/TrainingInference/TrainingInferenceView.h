#pragma once

#include <QWidget>
#include <QSplitter>
#include <QToolBar>
#include <QAction>
#include <QLineEdit>
#include <QListWidget>
#include <QStackedWidget>

class ImageViewWidget;
class CategoryPanel;
class InferencePanel;
class ScriptEditorWidget;
class ResultPanel;

class TrainingInferenceView : public QWidget {
    Q_OBJECT

public:
    explicit TrainingInferenceView(QWidget* parent = nullptr);

signals:
    void viewChanged(const QString& viewName);

private slots:
    void onImportImages();
    void onImportFolder();
    void onRunTrainingPipeline();
    void onImagesImported(int count);
    void onImageSelected(int row);
    void onPreviewImage(const QString& filePath);
    void onInferenceRequested(const QString& modelPath, const QStringList& imagePaths);
    void onInferenceCompleted(const QList<InferenceResult>& results);

private:
    void setupUI();
    void setupToolbar(QVBoxLayout* mainLayout);
    void setupImagePanel(QSplitter* splitter);
    void setupCenterPanel(QSplitter* splitter);
    void setupBottomPanel(QSplitter* splitter);

    QToolBar* m_toolbar;
    QSplitter* m_mainSplitter;
    QSplitter* m_centerSplitter;

    QListWidget* m_imageList;
    ImageViewWidget* m_imageView;

    CategoryPanel* m_categoryPanel;
    InferencePanel* m_inferencePanel;

    QStackedWidget* m_centerStack;
    ScriptEditorWidget* m_scriptEditor;
    ResultPanel* m_resultPanel;
};