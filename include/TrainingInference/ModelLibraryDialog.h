#ifndef MODELLIBRARYDIALOG_H
#define MODELLIBRARYDIALOG_H

#include <QDialog>
#include <QListWidget>
#include <QLabel>
#include <QPushButton>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QMap>

struct BuiltinModelEntry {
    QString id;
    QString name;
    QString path;
    QString description;
    QString category;
    int width = 224;
    int height = 224;
};

class ModelLibraryDialog : public QDialog {
    Q_OBJECT

public:
    explicit ModelLibraryDialog(QWidget* parent = nullptr);

    QString selectedModelPath() const;
    QString selectedModelName() const;

    void setCurrentModelPath(const QString& path);
    void scanDirectory(const QString& dir);

    static QList<BuiltinModelEntry> builtinModels();

private slots:
    void onScanDirectory();
    void onModelSelectionChanged();
    void onLoadSelected();
    void onModelDoubleClicked(QListWidgetItem* item);

private:
    void setupUI();
    void setupModelList(QListWidget* list, const QStringList& paths);
    void showModelDetails(const QString& modelPath, const QString& modelName);
    void populateBuiltinModels();
    void populateUserModels();

    QTabWidget* m_tabWidget;
    QListWidget* m_builtinList;
    QListWidget* m_userList;
    QListWidget* m_scannedList;
    QLabel* m_detailLabel;
    QPushButton* m_loadBtn;
    QPushButton* m_scanBtn;
    QPushButton* m_cancelBtn;

    QString m_selectedPath;
    QString m_selectedName;
    QString m_lastScanDir;
    QMap<QString, BuiltinModelEntry> m_builtinRegistry;
};

#endif