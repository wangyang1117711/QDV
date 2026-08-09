#include "UI/OperatorHelpProvider.h"
#include "Core/Logger.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QRegularExpressionMatchIterator>
#include <QCoreApplication>

// ============================================================================
// OperatorHelpProvider 实现（v2.8.0）
// 双源加载：config/operators.json + docs/algorithms/operators/*.md
// ============================================================================

OperatorHelpProvider::OperatorHelpProvider() {
    // 默认路径：exe 同级 config/operators.json 与 docs/algorithms/operators/
    // 测试环境可通过 reload() 注入自定义路径
}

OperatorHelpProvider& OperatorHelpProvider::instance() {
    // Meyer's 单例：C++11 起线程安全
    static OperatorHelpProvider s_instance;
    return s_instance;
}

// ============================================================================
// 路径解析辅助
// ============================================================================
static QString resolveDefaultOperatorsJsonPath() {
    // 优先 exe 同级 config/operators.json（生产环境）
    return QDir(QCoreApplication::applicationDirPath())
                .absoluteFilePath(QStringLiteral("config/operators.json"));
}

static QString resolveDefaultMarkdownDir() {
    // 优先 exe 同级 docs/algorithms/operators/（生产环境）
    return QDir(QCoreApplication::applicationDirPath())
                .absoluteFilePath(QStringLiteral("docs/algorithms/operators"));
}

// ============================================================================
// 懒加载入口（const 方法借助 mutable 触发首次加载）
// ============================================================================
void OperatorHelpProvider::ensureLoaded() const {
    if (m_loaded) return;
    // 去除 const 以调用非 const 的加载方法
    const_cast<OperatorHelpProvider*>(this)->loadOperatorsJson();
    const_cast<OperatorHelpProvider*>(this)->loadMarkdownDocs();
    m_loaded = true;
}

// ============================================================================
// 从 operators.json 加载算子元数据
// 解析所有字段（含 OperatorDescriptors 未提取的 coreMeaning/scenario/caveats）
// ============================================================================
void OperatorHelpProvider::loadOperatorsJson() {
    const QString path = m_operatorsJsonPath.isEmpty()
                             ? resolveDefaultOperatorsJsonPath()
                             : m_operatorsJsonPath;

    QFile f(path);
    if (!f.exists()) {
        QDV::Logger::warn(QStringLiteral("[OperatorHelpProvider] operators.json not found: %1").arg(path));
        return;
    }
    if (!f.open(QIODevice::ReadOnly)) {
        QDV::Logger::warn(QStringLiteral("[OperatorHelpProvider] operators.json open failed: %1")
                          .arg(f.errorString()));
        return;
    }
    const QByteArray data = f.readAll();
    f.close();

    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &perr);
    if (perr.error != QJsonParseError::NoError) {
        QDV::Logger::warn(QStringLiteral("[OperatorHelpProvider] JSON parse error at %1: %2")
                          .arg(perr.offset).arg(perr.errorString()));
        return;
    }
    if (!doc.isObject()) {
        QDV::Logger::warn(QStringLiteral("[OperatorHelpProvider] root is not JSON object"));
        return;
    }
    const QJsonObject root = doc.object();
    const QJsonArray ops = root.value("operators").toArray();
    if (ops.isEmpty()) {
        QDV::Logger::warn(QStringLiteral("[OperatorHelpProvider] operators array is empty"));
        return;
    }

    for (const QJsonValue& v : ops) {
        if (!v.isObject()) continue;
        const QJsonObject obj = v.toObject();
        const QString type = obj.value("type").toString();
        if (type.isEmpty()) continue;

        QVariantMap meta;
        meta["type"]        = type;
        meta["cnName"]      = obj.value("cnName").toString();
        meta["category"]    = obj.value("category").toString();
        meta["description"] = obj.value("description").toString();
        meta["coreMeaning"] = obj.value("coreMeaning").toString();
        meta["scenario"]    = obj.value("scenario").toString();

        // caveats 数组
        QStringList caveats;
        const QJsonArray caveatsArr = obj.value("caveats").toArray();
        caveats.reserve(caveatsArr.size());
        for (const QJsonValue& c : caveatsArr) {
            if (c.isString()) caveats.append(c.toString());
        }
        meta["caveats"] = caveats;

        // params 数组（保留所有字段，包含 help/cnName/type 等）
        QVariantList paramsList;
        const QJsonArray paramsArr = obj.value("params").toArray();
        paramsList.reserve(paramsArr.size());
        for (const QJsonValue& p : paramsArr) {
            if (!p.isObject()) continue;
            paramsList.append(p.toObject().toVariantMap());
        }
        meta["params"] = paramsList;

        m_operatorMeta.insert(type, meta);
    }

    QDV::Logger::info(QStringLiteral("[OperatorHelpProvider] loaded %1 operators from %2")
                      .arg(m_operatorMeta.size()).arg(path));
}

// ============================================================================
// 从 Markdown 手册加载完整文档
// 遍历 docs/algorithms/operators/ 目录下所有 *.md 文件，
// 按 `## <Type>（<中文名>）` 模式分割算子段落，解析章节
// ============================================================================
void OperatorHelpProvider::loadMarkdownDocs() {
    const QString dirPath = m_markdownDir.isEmpty()
                                ? resolveDefaultMarkdownDir()
                                : m_markdownDir;
    QDir dir(dirPath);
    if (!dir.exists()) {
        QDV::Logger::warn(QStringLiteral("[OperatorHelpProvider] markdown dir not found: %1").arg(dirPath));
        return;
    }
    const QStringList mdFiles = dir.entryList(QStringList() << QStringLiteral("*.md"),
                                              QDir::Files | QDir::Readable, QDir::Name);
    if (mdFiles.isEmpty()) {
        QDV::Logger::warn(QStringLiteral("[OperatorHelpProvider] no .md files in %1").arg(dirPath));
        return;
    }

    // 正则：匹配 `## <Type>（<中文名>）` 形式的二级标题
    // 中英文括号都支持（手册使用中文全角括号）
    // 捕获组 1 = Type，组 2 = 中文名
    static const QRegularExpression kOpHeader(
        QStringLiteral("^##\\s+(.+?)[（(](.+?)[）)]\\s*$"));
    int totalParsed = 0;

    for (const QString& fileName : mdFiles) {
        const QString filePath = dir.absoluteFilePath(fileName);
        QFile f(filePath);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            continue;
        }
        const QString content = QString::fromUtf8(f.readAll());
        f.close();

        // 按 `## ` 行分割段落
        // 第 0 段是文件标题（# xxx算子手册）+ 元信息，跳过
        const QStringList sections = content.split(
            QRegularExpression(QStringLiteral("^##\\s+"), QRegularExpression::MultilineOption));
        for (int i = 1; i < sections.size(); ++i) {
            const QString& section = sections[i];
            // 取第一行作为标题行，匹配 `Type（中文名）`
            const int newlineIdx = section.indexOf('\n');
            const QString headerLine = newlineIdx < 0 ? section : section.left(newlineIdx);
            const QRegularExpressionMatch m = kOpHeader.match(headerLine.trimmed());
            if (!m.hasMatch()) continue;
            const QString type = m.captured(1).trimmed();
            // body 为标题行之后的内容
            const QString body = newlineIdx < 0 ? QString() : section.mid(newlineIdx + 1);

            const QVariantMap parsed = parseOperatorMarkdown(type, body);
            if (!parsed.isEmpty()) {
                m_operatorDocs.insert(type, parsed);
                ++totalParsed;
            }
        }
    }

    QDV::Logger::info(QStringLiteral("[OperatorHelpProvider] loaded %1 markdown docs from %2")
                      .arg(totalParsed).arg(dirPath));
}

// ============================================================================
// 解析单个算子的 Markdown 段落
// 输入 body：从 `## <Type>（...）` 标题下一行开始，到下一个 `## ` 或文档末尾
// 提取章节：核心含义 / 适用场景 / 注意事项 / 关联算子
// ============================================================================
QVariantMap OperatorHelpProvider::parseOperatorMarkdown(const QString& type,
                                                        const QString& markdown) const {
    QVariantMap result;
    if (type.isEmpty() || markdown.isEmpty()) return result;

    // 按 `### ` 分割章节
    const QStringList parts = markdown.split(
        QRegularExpression(QStringLiteral("^###\\s+"), QRegularExpression::MultilineOption));

    for (int i = 1; i < parts.size(); ++i) {
        const QString& part = parts[i];
        const int newlineIdx = part.indexOf('\n');
        const QString heading = (newlineIdx < 0 ? part : part.left(newlineIdx)).trimmed();
        const QString body    = newlineIdx < 0 ? QString() : part.mid(newlineIdx + 1).trimmed();

        if (heading == QStringLiteral("核心含义")) {
            result["coreMeaning"] = body;
        } else if (heading == QStringLiteral("适用场景")) {
            result["scenario"] = body;
        } else if (heading == QStringLiteral("注意事项")) {
            // 注意事项为 - 开头的列表项
            QStringList caveats;
            const QStringList lines = body.split('\n');
            for (const QString& line : lines) {
                const QString trimmed = line.trimmed();
                if (trimmed.startsWith('-') || trimmed.startsWith('*')) {
                    // 去掉列表标记 "- " 或 "* "，保留内容
                    QString item = trimmed.mid(1).trimmed();
                    if (!item.isEmpty()) caveats.append(item);
                }
            }
            result["caveats"] = caveats;
        } else if (heading == QStringLiteral("关联算子")) {
            // 关联算子格式：`- **TypeName**：说明`
            // 提取 ** ** 中的算子 type
            QStringList related;
            static const QRegularExpression kRelated(
                QStringLiteral("\\*\\*(.+?)\\*\\*"));
            auto it = kRelated.globalMatch(body);
            while (it.hasNext()) {
                const QRegularExpressionMatch m = it.next();
                const QString name = m.captured(1).trimmed();
                if (!name.isEmpty()) related.append(name);
            }
            result["relatedOperators"] = related;
        }
    }
    return result;
}

// ============================================================================
// ParamType int → 中文名映射
// 0=Int, 1=Double, 2=Enum, 3=Bool, 4=String, 5=Vector, 6=Mat
// ============================================================================
QString OperatorHelpProvider::paramTypeName(int typeInt) {
    switch (typeInt) {
        case 0: return QStringLiteral("Int");
        case 1: return QStringLiteral("Double");
        case 2: return QStringLiteral("Enum");
        case 3: return QStringLiteral("Bool");
        case 4: return QStringLiteral("String");
        case 5: return QStringLiteral("Vector");
        case 6: return QStringLiteral("Mat");
        default: return QStringLiteral("Unknown");
    }
}

// ============================================================================
// getShortDesc：coreMeaning 优先 → description → cnName
// 超过 100 字符自动截断加省略号
// ============================================================================
QString OperatorHelpProvider::getShortDesc(const QString& type) const {
    ensureLoaded();
    if (type.isEmpty()) return QString();

    // 优先从 Markdown 文档取（更详细的核心含义）
    auto docIt = m_operatorDocs.constFind(type);
    if (docIt != m_operatorDocs.constEnd()) {
        const QString v = docIt.value().value("coreMeaning").toString();
        if (!v.isEmpty()) {
            // Markdown 文档的 coreMeaning 较长，取第一段并截断
            // 仅取首段（双换行分段）
            const int paragraphEnd = v.indexOf(QStringLiteral("\n\n"));
            const QString firstParagraph = paragraphEnd > 0
                ? v.left(paragraphEnd).trimmed() : v.trimmed();
            if (firstParagraph.size() > 100) {
                return firstParagraph.left(97) + QStringLiteral("...");
            }
            return firstParagraph;
        }
    }

    // 回退到 operators.json 的 coreMeaning
    auto metaIt = m_operatorMeta.constFind(type);
    if (metaIt != m_operatorMeta.constEnd()) {
        const QVariantMap& meta = metaIt.value();
        const QString core = meta.value("coreMeaning").toString();
        if (!core.isEmpty()) {
            return core.size() > 100 ? core.left(97) + QStringLiteral("...") : core;
        }
        const QString desc = meta.value("description").toString();
        if (!desc.isEmpty()) {
            return desc.size() > 100 ? desc.left(97) + QStringLiteral("...") : desc;
        }
        return meta.value("cnName").toString();
    }
    return QString();
}

// ============================================================================
// getFullDoc：合并 operators.json 元数据 + Markdown 文档
// Markdown 文档优先（更详细），缺失字段回退到 JSON
// ============================================================================
QVariantMap OperatorHelpProvider::getFullDoc(const QString& type) const {
    ensureLoaded();
    if (type.isEmpty()) return QVariantMap();

    // 1) 先从 operators.json 取基础元数据
    auto metaIt = m_operatorMeta.constFind(type);
    if (metaIt == m_operatorMeta.constEnd()) {
        return QVariantMap();  // 算子不存在
    }
    const QVariantMap& meta = metaIt.value();

    QVariantMap result;
    result["type"]        = meta.value("type");
    result["cnName"]      = meta.value("cnName");
    result["category"]    = meta.value("category");
    result["description"] = meta.value("description");
    result["params"]      = meta.value("params");
    // JSON 源的 coreMeaning/scenario/caveats 作为回退默认值
    result["coreMeaning"]      = meta.value("coreMeaning");
    result["scenario"]         = meta.value("scenario");
    result["caveats"]          = meta.value("caveats");
    result["relatedOperators"] = QStringList();  // JSON 无此字段，默认空

    // 2) Markdown 文档覆盖（若已加载）
    auto docIt = m_operatorDocs.constFind(type);
    if (docIt != m_operatorDocs.constEnd()) {
        const QVariantMap& doc = docIt.value();
        // Markdown 源的 coreMeaning 优先（非空时覆盖）
        const QString mdCore = doc.value("coreMeaning").toString();
        if (!mdCore.isEmpty()) result["coreMeaning"] = mdCore;
        const QString mdScenario = doc.value("scenario").toString();
        if (!mdScenario.isEmpty()) result["scenario"] = mdScenario;
        const QStringList mdCaveats = doc.value("caveats").toStringList();
        if (!mdCaveats.isEmpty()) result["caveats"] = mdCaveats;
        const QStringList mdRelated = doc.value("relatedOperators").toStringList();
        if (!mdRelated.isEmpty()) result["relatedOperators"] = mdRelated;
    }
    return result;
}

// ============================================================================
// getParamHelp：返回参数的 help 字段；为空时回退 "cnName (类型名)"
// ============================================================================
QString OperatorHelpProvider::getParamHelp(const QString& type, const QString& paramName) const {
    ensureLoaded();
    if (type.isEmpty() || paramName.isEmpty()) return QString();

    auto metaIt = m_operatorMeta.constFind(type);
    if (metaIt == m_operatorMeta.constEnd()) return QString();

    const QVariantList params = metaIt.value().value("params").toList();
    for (const QVariant& v : params) {
        const QVariantMap p = v.toMap();
        if (p.value("name").toString() == paramName) {
            const QString help = p.value("help").toString();
            if (!help.isEmpty()) return help;
            // 回退：cnName (类型名)
            const QString cnName = p.value("cnName").toString();
            const int typeInt = p.value("type").toInt();
            return QStringLiteral("%1 (%2)").arg(cnName).arg(paramTypeName(typeInt));
        }
    }
    return QString();  // 参数不存在
}

// ============================================================================
// 测试辅助：重新加载（注入自定义路径）
// ============================================================================
void OperatorHelpProvider::reload(const QString& operatorsJsonPath,
                                  const QString& markdownDir) {
    m_loaded = false;
    m_operatorMeta.clear();
    m_operatorDocs.clear();
    m_operatorsJsonPath = operatorsJsonPath;
    m_markdownDir = markdownDir;
    ensureLoaded();
}

// ============================================================================
// 测试辅助：清空缓存
// ============================================================================
void OperatorHelpProvider::clearCache() {
    m_loaded = false;
    m_operatorMeta.clear();
    m_operatorDocs.clear();
    m_operatorsJsonPath.clear();
    m_markdownDir.clear();
}
