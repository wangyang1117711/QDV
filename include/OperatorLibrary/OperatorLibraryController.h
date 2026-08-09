#ifndef QDV_OPERATORLIBRARY_CONTROLLER_H
#define QDV_OPERATORLIBRARY_CONTROLLER_H

#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QVariantList>
#include "OperatorLibrary/OperatorDefinition.h"

namespace QDV {
namespace OperatorLibrary {

/// 控制器门面（供 Bridge/QML 调用）
/// 聚合：RegistryStore / Validator / Importer / VersionManager /
///       Tester / DocGenerator / PermissionManager / DependencyGraph
class OperatorLibraryController : public QObject {
    Q_OBJECT
public:
    static OperatorLibraryController* instance();

    void init(const QString& operatorsPath, const QString& versionsPath);

    // 查询
    QVariantList query(const QString& category, const QString& keyword) const;
    QVariantMap getOperator(const QString& type) const;
    QStringList categories() const;

    // CRUD
    bool createOperator(const OperatorDef& def, QString* err = nullptr);
    bool updateOperator(const OperatorDef& def, QString* err = nullptr);
    /// 删除（先做依赖检查，被依赖则拒绝并返回 blockedBy）
    QVariantMap deleteOperator(const QString& type);

    // 发布（可选）：写入 operators.json，使其进入 EditView 算子面板
    bool publishOperator(const QString& type, bool enabled, QString* err = nullptr);

    // 校验
    ValidationResult validate(const OperatorDef& def) const;

    // 导入
    ImportPreview importFile(const QString& path, QString* err = nullptr);
    bool commitImport(const ImportPreview& preview, QString* err = nullptr);
    /// 删除已导入算子（spec §2.3：RegistryStore::remove + 内存清理 + 发信号）
    /// 画布预检在 Bridge 层完成，Controller 不感知画布
    QVariantMap removeImported(const QString& type);

    // 训练产物自动注册为算子（spec D：训练产物自动注册为算子）
    // 信号链：TrainingBridge::trainingCompleted → ModelManager::registerTrainedModel
    //         → OperatorLibraryController::registerTrainedModelAsOperator
    // @param modelId     模型唯一标识（暂未直接使用，预留；当前以 modelName 作为 type 后缀）
    // @param onnxPath    训练产物 ONNX 文件路径（锁定为算子 modelPath 默认值）
    // @param labelsPath  训练产物 labels 文件路径（用于填充 categoryLabels 默认值；可为空）
    // @param modelName   模型显示名（用作算子 type 后缀和 cnName 拼接）
    // @return true=注册成功（写入 .qdvop + 即时注册到算子面板）
    bool registerTrainedModelAsOperator(const QString& modelId,
                                        const QString& onnxPath,
                                        const QString& labelsPath,
                                        const QString& modelName);

    // 版本
    QVariantList history(const QString& type) const;
    bool rollback(const QString& type, const QString& version, QString* err = nullptr);

    // 测试与文档
    TestResult testOperator(const QString& type, const QString& sampleInput, QString* err = nullptr);
    QString generateDoc(const QString& type) const;

    // 权限
    QVariantMap permissions(const QString& type) const;
    bool setPermissions(const QString& type, const QVariantMap& roles, QString* err = nullptr);
    bool canCurrentUser(const QString& action, const QString& type) const;

signals:
    void storeChanged();
    /// 算子导入/删除通知（spec §2.2/§2.3）
    /// action: "added" | "removed"
    void importedChanged(const QString& type, const QString& action);

private:
    explicit OperatorLibraryController(QObject* parent = nullptr);
    static OperatorLibraryController* s_instance;
};

} // namespace OperatorLibrary
} // namespace QDV

#endif // QDV_OPERATORLIBRARY_CONTROLLER_H
