// =====================================================================
// ShapeRegistry.h — 能力注册表只读加载器（S0）
//
// 作用：读取 capabilities.json，把「形状元数据 / 导出映射 / 训练任务映射」
//       解析为可供 C++ 与 QML 查询的只读数据。加新形状/新导出/新训练任务
//       只需改 capabilities.json，主体代码（分派逻辑）保持不变。
//
// 加载优先级（满足"改配置即生效、无需重编译"）：
//   1) 环境变量 QDV_CAPABILITIES 指向的绝对路径
//   2) 可执行文件目录向上回溯 6 层内的 capabilities.json
//   3) 当前工作目录的 capabilities.json
//   4) qrc:/capabilities/capabilities.json（随 exe 打包的兜底副本）
//
// 查询接口（供 AnnotationCanvas.qml / ExportDispatch 等分派方调用）：
//   - shapes() / shapeInfo(key)          —— 形状列表/单形状详情
//   - overlayTypeOf(shape)               —— 该形状应使用哪个覆盖层绘制类型
//     （rect -> "rect"，vec_rect -> "vec_rect"，polygon/freeform -> "polygon"）
//   - exportNeedsShapes(format)           —— 某导出格式需要哪些形状
    //   - taskModels(task)                    —— 某任务可训练模型
    // =====================================================================
#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QStringList>

class ShapeRegistry : public QObject
{
    Q_OBJECT
    // ---- 暴露给 QML 的只读属性 ----
    Q_PROPERTY(QVariantList shapes READ shapes NOTIFY shapesChanged)
    Q_PROPERTY(QVariantList exports READ exports NOTIFY shapesChanged)
    Q_PROPERTY(QVariantList tasks READ tasks NOTIFY shapesChanged)
    Q_PROPERTY(bool loaded READ loaded NOTIFY shapesChanged)
    Q_PROPERTY(QString source READ source NOTIFY shapesChanged)

public:
    explicit ShapeRegistry(QObject* parent = nullptr);

    // 外部显式指定配置文件（main.cpp 调用；找不到时自动回退 qrc 兜底）
    void load(const QString& overridePath = QString());

    // ---- 只读访问 ----
    QVariantList shapes() const { return m_shapes; }
    QVariantList exports() const { return m_exports; }
    QVariantList tasks() const { return m_tasks; }
    bool loaded() const { return m_loaded; }
    QString source() const { return m_source; }

    // 是否登记了某形状（key 精确匹配；未登记视为不存在）
    Q_INVOKABLE bool hasShape(const QString& key) const;
    // 某形状的 overlayType（未登记回退 "rect"）
    Q_INVOKABLE QString overlayTypeOf(const QString& key) const;
    // 某形状是否已启用（enabled 缺省视为 true）
    Q_INVOKABLE bool isShapeEnabled(const QString& key) const;
    // 单形状详情（未登记返回空 map）
    Q_INVOKABLE QVariantMap shapeInfo(const QString& key) const;
    // 某导出格式是否接受某形状（needsShape 为空=接受任意/不校验）
    Q_INVOKABLE bool supportsShape(const QString& format, const QString& shape) const;
    // 某导出格式需要哪些形状（供空标签防护提示）
    Q_INVOKABLE QStringList exportNeedsShapes(const QString& format) const;
    // 某任务可用的默认形状（无返回空串）
    Q_INVOKABLE QString taskDefaultShape(const QString& task) const;
    // 某任务可训练的模型清单（无返回空列表）
    Q_INVOKABLE QStringList taskModels(const QString& task) const;

signals:
    void shapesChanged();

private:
    // 尝试从本地路径读取；成功返回 true
    bool tryLoadFromFile(const QString& path);
    // 从 qrc 读取
    bool loadFromResource();
    void parse(const QByteArray& json);

    QVariantList m_shapes;
    QVariantList m_exports;
    QVariantList m_tasks;
    bool m_loaded = false;
    QString m_source;

    // 索引缓存
    QMap<QString, QVariantMap> m_shapeByIdx;
};