#ifndef MODELNOTESDIALOG_H
#define MODELNOTESDIALOG_H

// ============================================================================
// 模型注意事项对话框
// 点击 ZeroShotPanel 中的 ℹ️ 按钮后弹出，展示当前模型的注意事项
// ============================================================================

#include <QDialog>
#include <QLabel>
#include <QTextEdit>
#include "ZeroShotKit/ZeroShotTypes.h"

namespace zsu {
class ModelNotesManager;
}

class ModelNotesDialog : public QDialog {
    Q_OBJECT

public:
    explicit ModelNotesDialog(zsu::ModelNotesManager* manager,
                              zsu::ZeroShotModelType modelType,
                              QWidget* parent = nullptr);

private:
    void setupUI();
    void loadNotes();

    zsu::ModelNotesManager* m_manager;
    zsu::ZeroShotModelType m_modelType;

    QLabel* m_titleLabel = nullptr;
    QLabel* m_descLabel = nullptr;
    QTextEdit* m_contentEdit = nullptr;
};

#endif // MODELNOTESDIALOG_H
