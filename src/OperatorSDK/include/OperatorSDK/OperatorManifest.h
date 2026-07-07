#ifndef QDV_OPERATOR_MANIFEST_H
#define QDV_OPERATOR_MANIFEST_H

#include <QString>
#include <QJsonObject>
#include <QList>
#include "UI/OperatorDescriptors.h"

namespace QDV {

/// 算子清单结构（替代散落元数据，供 ManifestLoader 解析）
struct OperatorManifest {
    QString                 type;        ///< 算子类型（与 IOperator::type() 一致）
    QString                 version;     ///< 接口版本（与 IOperator::version() 一致）
    QString                 cnName;      ///< 中文显示名
    QString                 category;    ///< 分类
    QString                 iconPath;    ///< 图标路径
    QString                 description; ///< 简要描述
    QString                 library;     ///< 动态库文件名（如 "Histogram.dll"）
    QList<QDV::UI::ParamSpec> params;    ///< 参数列表（复用 UI::ParamSpec）

    /// 序列化为 JSON
    QJsonObject toJson() const;

    /// 从 JSON 反序列化；解析失败时 outError 写入原因
    static OperatorManifest fromJson(const QJsonObject& obj, QString* outError = nullptr);
};

/// 从 path 读取 JSON 并解析为 OperatorManifest（RT-004）
/// 缺必填字段返回 false，outError 写入 "missing required field: <field>"
bool loadManifest(const QString& path, OperatorManifest& outManifest, QString* outError = nullptr);

/// 校验 manifest 必填字段（type/version/cnName/category/library 非空）
bool validateManifest(const OperatorManifest& manifest, QString* outError = nullptr);

/// 加载 .dll 并读取同目录 manifest.json（RT-002）
/// .dll 缺失返回 false 不崩溃；接口版本不匹配返回 false（RT-003）
bool loadPlugin(const QString& libraryPath, OperatorManifest& outManifest, QString* outError = nullptr);

/// loadPlugin + registerOperator（RT-003：版本不匹配返回 false）
bool loadAndRegister(const QString& libraryPath, QString* outError = nullptr);

} // namespace QDV

#endif // QDV_OPERATOR_MANIFEST_H
