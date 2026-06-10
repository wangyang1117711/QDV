#pragma once

#include <QWidget>
#include <QSplitter>
#include <QToolBar>
#include <QAction>
#include <QLineEdit>
#include <QListWidget>
#include <QStackedWidget>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include <QTextEdit>

class ImageViewWidget;
class CategoryPanel;
class InferencePanel;
class ScriptEditorWidget;
class ResultPanel;
struct InferenceResult;
class QVBoxLayout;

namespace QDV { class TrainingBridge; }
class TrainingProgressDialog;

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
    void onDeleteSelected();
    void onClearAll();
    void onImageRemoved(const QString& filePath);
    void onImagesCleared();
    void onInferenceRequested(const QString& modelPath, const QStringList& imagePaths);
    void onInferenceCompleted(const QList<InferenceResult>& results);
    void onSelectAllClicked();
    void onInvertSelectionClicked();
    void onAddToCategoryRequested(const QString& categoryId, const QString& categoryName);
    void onSelectionChanged();
    void onTrainingProgress(const QVariantMap& progress);
    void onTrainingCompleted(const QVariantMap& result);
    void onTrainingError(const QString& phase, const QString& message);
    void onTrainingLogOutput(const QString& message);

private:
    void setupUI();
    void setupToolbar(QVBoxLayout* mainLayout);
    void setupImagePanel(QSplitter* splitter);
    void setupCenterPanel(QSplitter* splitter);
    void setupBottomPanel(QSplitter* splitter);
    void rebuildImageList();
    void updateSelectAllButton();
    void updateCategoryPanelButtons();

    QDV::TrainingBridge* m_trainingBridge;
    TrainingProgressDialog* m_progressDialog = nullptr;
    QTextEdit* m_consoleLog;

    QToolBar* m_toolbar;
    QSplitter* m_mainSplitter;
    QSplitter* m_centerSplitter;

    QListWidget* m_imageList;
    ImageViewWidget* m_imageView;
    QPushButton* m_selectAllBtn;
    QPushButton* m_invertSelectionBtn;
    QLabel* m_selectionLabel;

    CategoryPanel* m_categoryPanel;
    InferencePanel* m_inferencePanel;

    QStackedWidget* m_centerStack;
    ScriptEditorWidget* m_scriptEditor;
    ResultPanel* m_resultPanel;
};