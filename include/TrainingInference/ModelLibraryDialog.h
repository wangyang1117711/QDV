#ifndef MODELLIBRARYDIALOG_H
#define MODELLIBRARYDIALOG_H

#include <QDialog>
#include <QListWidget>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QTabWidget>
#include <QCheckBox>
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

// v2.7.2：列表项携带的元数据结构
struct ModelListItemData {
    QString id;
    QString name;
    QString path;
    QString source;     // builtin / loaded / registered / scanned / trash
    bool fileExists = false;
    bool loadable = false;       // OpenCV DNN 是否可加载
    bool loadableKnown = false;  // 是否已完成加载性预检
    QString loadError;           // 不兼容时的错误摘要
};

class ModelLibraryDialog : public QDialog {
    Q_OBJECT

public:
    explicit ModelLibraryDialog(QWidget* parent = nullptr);

    QString selectedModelPath() const;
    QString selectedModelName() const;

    void setCurrentModelPath(const QString& path);
    void scanDirectory(const QString& dir, bool switchToScanTab = true);

    static QList<BuiltinModelEntry> builtinModels();

private slots:
    void onScanDirectory();
    void onModelSelectionChanged();
    void onLoadSelected();
    void onRegisterScannedModel();      // 将单个扫描结果注册到模型库
    void onModelDoubleClicked(QListWidgetItem* item);

    // v2.7.1 新增：搜索/导入/删除/验证
    void onSearchChanged(const QString& text);
    void onImportModel();
    void onDeleteModel();
    void onVerifySelected();
    void onVerifyAll();

    // v2.7.2 新增：批量操作
    void onBatchRegister();
    void onBatchDelete();
    void onBatchVerify();

private:
    void setupUI();
    void showModelDetails(const QString& modelPath, const QString& modelName);
    void showMultiSelectionSummary(const QList<ModelListItemData>& selected);
    void populateBuiltinModels();
    void populateUserModels();
    void populateTrashModels();          // 回收站标签页：扫描 .trash 文件
    void applySearchFilter(const QString& text);  // 跨标签页实时过滤
    void refreshAllLists();              // 重新加载所有标签页数据
    void showIntegrityResult(bool ok, const QString& detail);  // 显示完整性校验结果

    // v2.7.2 新增工具函数
    QListWidget* currentListWidget() const;
    QList<ModelListItemData> selectedItemDatas() const;       // 获取当前列表所有选中项元数据
    void setItemData(QListWidgetItem* item, const ModelListItemData& data);
    ModelListItemData itemData(QListWidgetItem* item) const;
    void probeAndShowLoadability(const QString& modelPath);   // 预检并在详情区显示兼容性
    QString formatFileSize(qint64 bytes) const;

    QTabWidget* m_tabWidget;
    QListWidget* m_builtinList;
    QListWidget* m_userList;
    QListWidget* m_scannedList;
    QListWidget* m_trashList;            // v2.7.1 新增：回收站列表
    QLabel* m_detailLabel;
    QLabel* m_integrityLabel;            // v2.7.1 新增：完整性校验结果显示
    QLabel* m_loadabilityLabel;          // v2.7.2 新增：加载兼容性结果显示
    QLineEdit* m_searchEdit;             // v2.7.1 新增：搜索框
    QPushButton* m_loadBtn;
    QPushButton* m_scanBtn;
    QPushButton* m_cancelBtn;
    QPushButton* m_importBtn;            // v2.7.1 新增：导入模型按钮
    QPushButton* m_deleteBtn;            // v2.7.1 新增：删除模型按钮
    QPushButton* m_verifyOneBtn;         // v2.7.1 新增：单项验证按钮
    QPushButton* m_verifyAllBtn;         // v2.7.1 新增：批量验证按钮
    QPushButton* m_batchRegisterBtn;     // v2.7.2 新增：批量注册按钮
    QPushButton* m_batchDeleteBtn;       // v2.7.2 新增：批量删除按钮
    QPushButton* m_batchVerifyBtn;       // v2.7.2 新增：批量验证按钮
    QCheckBox* m_deleteRelatedCheck;     // v2.7.1 新增：删除时清理关联数据

    QString m_selectedPath;
    QString m_selectedName;
    QString m_lastScanDir;               // 最后一次扫描的目录；refreshAllLists() 会用它重新扫描
    QMap<QString, BuiltinModelEntry> m_builtinRegistry;
};

#endif
