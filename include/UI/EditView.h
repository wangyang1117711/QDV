#ifndef EDITVIEW_H
#define EDITVIEW_H

#include <QWidget>
#include <QTreeWidget>
#include <QSplitter>
#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QAction>
#include <QUndoStack>

class EditView : public QWidget {
    Q_OBJECT

public:
    explicit EditView(QWidget* parent = nullptr);
    ~EditView() override;

    void updatePropertyPanelForTool(const QString& toolType);

signals:
    void schemeModified();
    void toolSelected(const QString& toolId);
    void toolCountChanged(int count);
    void requestRunDetection();

private slots:
    void onToolDoubleClicked(QTreeWidgetItem* item, int column);
    void onUndo();
    void onRedo();
    void onDeleteTool();
    void onToolChainSelectionChanged();

private:
    void setupToolBox(QWidget* parent);
    void setupCanvas(QWidget* parent);
    void setupPropertyPanel(QWidget* parent);

    QSplitter* m_mainSplitter;
    QTreeWidget* m_toolBoxTree;
    QTreeWidget* m_toolChainTree;
    QScrollArea* m_canvasArea;
    QLabel* m_canvasLabel;
    QScrollArea* m_propertyArea;
    QWidget* m_propertyPanel;
    QFormLayout* m_propertyLayout;

    QAction* m_undoAction;
    QAction* m_redoAction;
    QAction* m_deleteAction;

    QUndoStack* m_undoStack;
};

#endif // EDITVIEW_H