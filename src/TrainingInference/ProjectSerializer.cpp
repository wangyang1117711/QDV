#include "TrainingInference/ProjectSerializer.h"
#include "Core/Logger.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QByteArray>
#include <QCryptographicHash>
#include <QDateTime>
#include <QCoreApplication>
#include <QDataStream>
#include <QtConcurrent>

// ==================== 构造函数 ====================

ProjectSerializer::ProjectSerializer(QObject* parent)
    : QObject(parent)
{
    // 保存完成信号转发：工作线程结束后由 QFutureWatcher 在主线程发射 finished
    connect(&m_saveWatcher, &QFutureWatcher<bool>::finished, this, [this]() {
        bool success = m_saveWatcher.result();
        QString msg = success ? QString::fromUtf8("保存成功") : m_lastError;
        emit saveFinished(m_currentSavePath, success, msg);
    });

    // 加载完成信号转发
    connect(&m_loadWatcher, &QFutureWatcher<bool>::finished, this, [this]() {
        bool success = m_loadWatcher.result();
        QString msg = success ? QString::fromUtf8("加载成功") : m_lastError;
        emit loadFinished(m_currentLoadPath, success, msg, m_loadedJson, m_missingImages);
    });
}

// ==================== 异步保存 ====================

void ProjectSerializer::saveAsync(const QString& filePath,
                                   TrainingProject* project,
                                   SaveMode mode)
{
    m_currentSavePath = filePath;
    m_currentMode = mode;
    m_lastError.clear();

    emit saveProgress(5, QString::fromUtf8("正在准备数据..."));

    // 关键修复：在主线程预先收集所有数据，工作线程不访问 project 对象
    // 之前工作线程直接调用 project->toJson() 和 project->setImagesSnapshot()，
    // 导致跨线程访问 QObject 段错误 + 打包模式破坏原始路径数据
    QJsonObject projectJson = project->toJson();  // 主线程访问，安全
    QList<ImageEntrySnapshot> imagesCopy = project->imagesSnapshot();  // 深拷贝，安全

    // 在工作线程执行文件 I/O（不访问 project 对象）
    m_saveWatcher.setFuture(QtConcurrent::run([this, filePath, mode, projectJson, imagesCopy]() -> bool {
        QString errMsg;
        bool success = false;
        if (mode == ModeBundled) {
            emit this->saveProgress(10, QString::fromUtf8("正在组装项目数据..."));
            success = saveBundledModeWithData(filePath, projectJson, imagesCopy, &errMsg);
        } else {
            emit this->saveProgress(10, QString::fromUtf8("正在组装项目数据..."));
            success = saveReferenceModeWithData(filePath, projectJson, &errMsg);
        }

        if (success) {
            emit this->saveProgress(100, QString::fromUtf8("完成"));
        }
        this->m_lastError = errMsg;
        return success;
    }));
}

// ==================== 异步加载 ====================

void ProjectSerializer::loadAsync(const QString& filePath)
{
    m_currentLoadPath = filePath;
    m_lastError.clear();
    m_missingImages.clear();
    m_loadedJson = QJsonObject();  // 清空上次加载结果

    emit loadProgress(5, QString::fromUtf8("正在检测文件格式..."));

    // 关键修复：工作线程只做文件 I/O 和 JSON 解析，不创建/访问 TrainingProject (QObject)
    // 之前工作线程创建 m_loadedProject 并跨线程操作，导致：
    // 1. QObject 跨线程访问不安全
    // 2. m_loadedProject 与 TrainingInferenceView::m_project 双重所有权，第二次加载时悬垂指针崩溃
    m_loadWatcher.setFuture(QtConcurrent::run([this, filePath]() -> bool {
        QString errMsg;
        bool success = false;

        SaveMode mode = detectMode(filePath);
        if (mode == ModeBundled) {
            emit this->loadProgress(10, QString::fromUtf8("正在解压项目文件..."));
            success = loadBundledModeToJson(filePath, &this->m_loadedJson, &errMsg);
        } else {
            emit this->loadProgress(10, QString::fromUtf8("正在解析项目文件..."));
            success = loadReferenceModeToJson(filePath, &this->m_loadedJson, &this->m_missingImages, &errMsg);
        }

        if (success) {
            emit this->loadProgress(100, QString::fromUtf8("完成"));
        }
        this->m_lastError = errMsg;
        return success;
    }));
}

// ==================== 格式检测 ====================

ProjectSerializer::SaveMode ProjectSerializer::detectMode(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return ModeReference;  // 默认返回引用模式
    }

    // 读取前 4 字节判断 gzip magic number
    QByteArray header = file.read(4);
    file.close();

    // gzip magic number: 0x1f 0x8b（注：qCompress 默认采用 zlib 格式，
    // 这里保留 0x1f8b 检测作为兼容；非 gzip 头则按引用 JSON 处理）
    if (header.size() >= 2 &&
        (unsigned char)header[0] == 0x1f &&
        (unsigned char)header[1] == 0x8b) {
        return ModeBundled;
    }

    return ModeReference;
}

// ==================== 引用模式保存 ====================

bool ProjectSerializer::saveReferenceMode(const QString& filePath,
                                          TrainingProject* project,
                                          QString* errMsg)
{
    QJsonObject root = project->toJson();
    return saveReferenceModeWithData(filePath, root, errMsg);
}

// 线程安全版本：使用预收集的 JSON，不访问 project 对象
bool ProjectSerializer::saveReferenceModeWithData(const QString& filePath,
                                                   const QJsonObject& projectJson,
                                                   QString* errMsg)
{
    QJsonObject root = projectJson;
    root["mode"] = "reference";  // 覆盖默认 mode

    QJsonDocument doc(root);

    // 使用 QSaveFile 原子写入（先写临时文件再 rename，避免写一半崩溃）
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        if (errMsg) *errMsg = QString::fromUtf8("无法打开文件进行写入: %1").arg(file.errorString());
        return false;
    }

    file.write(doc.toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (errMsg) *errMsg = QString::fromUtf8("写入文件失败: %1").arg(file.errorString());
        return false;
    }

    return true;
}

// ==================== 打包模式保存 ====================

bool ProjectSerializer::saveBundledMode(const QString& filePath,
                                        TrainingProject* project,
                                        QString* errMsg)
{
    QJsonObject root = project->toJson();
    QList<ImageEntrySnapshot> images = project->imagesSnapshot();
    return saveBundledModeWithData(filePath, root, images, errMsg);
}

// 线程安全版本：使用预收集的 JSON 和图像副本，不访问 project 对象
// 关键修复：imagesCopy 是值传递的副本，修改不影响原始 project
bool ProjectSerializer::saveBundledModeWithData(const QString& filePath,
                                                const QJsonObject& projectJson,
                                                QList<ImageEntrySnapshot> imagesCopy,
                                                QString* errMsg)
{
    // 创建临时目录
    QString tempBase = QDir::tempPath() + "/qdv_proj_" +
                       QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss_zzz");
    QDir tempDir(tempBase);
    if (!tempDir.mkpath(".")) {
        if (errMsg) *errMsg = QString::fromUtf8("无法创建临时目录: %1").arg(tempBase);
        return false;
    }

    // 复制图像到临时目录并重写路径（操作 imagesCopy 副本，不影响原始 project）
    QString imagesDirPath = tempBase + "/images";
    QDir imagesDir(imagesDirPath);
    if (!imagesDir.mkpath(".")) {
        if (errMsg) *errMsg = QString::fromUtf8("无法创建图像目录: %1").arg(imagesDirPath);
        QDir(tempBase).removeRecursively();
        return false;
    }

    // 安全阈值：单个文件 200MB，总体 1GB；超出建议使用引用模式保存，
    // 避免一次性读取过大图像导致内存分配失败或程序被系统终止。
    constexpr qint64 kMaxSingleFileSize = 200LL * 1024 * 1024;  // 200 MB
    constexpr qint64 kMaxTotalSize      = 1024LL * 1024 * 1024; // 1 GB

    qint64 totalSourceSize = 0;
    for (const auto& snap : imagesCopy) {
        if (snap.filePath.isEmpty()) continue;
        totalSourceSize += QFileInfo(snap.filePath).size();
    }

    if (totalSourceSize > kMaxTotalSize) {
        if (errMsg) {
            *errMsg = QString::fromUtf8("打包保存失败：图像总大小 %1 GB 超过安全阈值 %2 GB，"
                                        "建议使用引用模式保存项目。")
                          .arg(totalSourceSize / (1024.0 * 1024 * 1024), 0, 'f', 2)
                          .arg(kMaxTotalSize / (1024.0 * 1024 * 1024), 0, 'f', 2);
        }
        QDir(tempBase).removeRecursively();
        return false;
    }

    // 直接在 imagesCopy 副本上操作，安全
    for (auto& snap : imagesCopy) {
        if (snap.filePath.isEmpty()) continue;

        QFileInfo srcInfo(snap.filePath);
        if (!srcInfo.exists()) {
            QDV::Logger::warn(QString("ProjectSerializer: 图像文件不存在，跳过: %1").arg(snap.filePath));
            continue;
        }

        qint64 fileSize = srcInfo.size();
        if (fileSize > kMaxSingleFileSize) {
            QDV::Logger::warn(QString("ProjectSerializer: 图像文件 %1 大小 %2 MB 超过 %3 MB，跳过打包")
                                  .arg(snap.filePath)
                                  .arg(fileSize / (1024.0 * 1024), 0, 'f', 2)
                                  .arg(kMaxSingleFileSize / (1024.0 * 1024)));
            continue;
        }

        // 分块计算 SHA-256，避免将整个大文件读入内存
        QCryptographicHash hash(QCryptographicHash::Sha256);
        QFile srcFile(snap.filePath);
        if (!srcFile.open(QIODevice::ReadOnly)) {
            QDV::Logger::warn(QString("ProjectSerializer: 无法读取图像文件: %1").arg(snap.filePath));
            continue;
        }
        const qint64 kHashChunk = 1024 * 1024; // 1 MB
        QByteArray buffer(kHashChunk, Qt::Uninitialized);
        while (!srcFile.atEnd()) {
            qint64 read = srcFile.read(buffer.data(), kHashChunk);
            if (read > 0) {
                hash.addData(buffer.data(), static_cast<int>(read));
            }
        }
        srcFile.close();

        QString sha8 = hash.result().toHex().left(8);
        QString ext = srcInfo.suffix();
        QString newFileName = sha8 + "." + ext;
        QString newPath = imagesDirPath + "/" + newFileName;

        // 使用 QFile::copy 进行文件复制，避免再次将整个文件读入内存
        if (!QFile::exists(newPath)) {
            if (!QFile::copy(snap.filePath, newPath)) {
                QDV::Logger::warn(QString("ProjectSerializer: 复制图像文件失败: %1 -> %2")
                                      .arg(snap.filePath).arg(newPath));
                continue;
            }
        }

        // 保留原始路径，更新 filePath 为包内相对路径（仅修改副本）
        snap.originalPath = snap.filePath;
        snap.filePath = "images/" + newFileName;
    }

    // 使用修改后的 imagesCopy 副本重新组装 JSON 中的图像部分
    QJsonObject root = projectJson;
    root["mode"] = "bundled";

    // 覆盖 images 部分（使用已重写路径的副本）
    QJsonObject imagesObj;
    QJsonArray imagesArr;
    for (const auto& snap : imagesCopy) {
        QJsonObject imgObj;
        imgObj["filePath"] = snap.filePath;
        imgObj["originalPath"] = snap.originalPath;
        imgObj["fileName"] = snap.fileName;
        imgObj["width"] = snap.width;
        imgObj["height"] = snap.height;
        imgObj["channels"] = snap.channels;
        imgObj["format"] = snap.format;
        imgObj["fileSize"] = static_cast<qint64>(snap.fileSize);
        imgObj["isAnnotated"] = snap.isAnnotated;
        imgObj["label"] = snap.label;
        imgObj["isSelected"] = snap.isSelected;
        imagesArr.append(imgObj);
    }
    imagesObj["entries"] = imagesArr;
    root["images"] = imagesObj;

    QJsonDocument doc(root);

    // 写 project.json 到临时目录
    QString projectJsonPath = tempBase + "/project.json";
    QFile projectFile(projectJsonPath);
    if (!projectFile.open(QIODevice::WriteOnly)) {
        if (errMsg) *errMsg = QString::fromUtf8("无法写入 project.json");
        QDir(tempBase).removeRecursively();
        return false;
    }
    projectFile.write(doc.toJson(QJsonDocument::Indented));
    projectFile.close();

    // 收集所有文件数据用于压缩
    QStringList imageFiles = imagesDir.entryList(QDir::Files);

    // 估算 archiveData 大小并预先 reserve，减少多次内存重新分配
    qint64 estimatedArchiveSize = QFileInfo(projectJsonPath).size();
    for (const QString& imgName : imageFiles) {
        estimatedArchiveSize += QFileInfo(imagesDir.absoluteFilePath(imgName)).size();
        // QDataStream 为每个文件写入文件名（QString）和长度前缀，预留少量开销
        estimatedArchiveSize += 64 + imgName.toUtf8().size();
    }

    // 使用 QDataStream 写入多个文件（自定义归档格式）
    // 保守 reserve：最多预分配 100MB，避免一次性请求过大内存
    QByteArray archiveData;
    constexpr qint64 kReserveCap = 100LL * 1024 * 1024;
    if (estimatedArchiveSize > 0) {
        qint64 reserveSize = qMin(estimatedArchiveSize, kReserveCap);
        if (reserveSize <= INT_MAX) {
            archiveData.reserve(static_cast<int>(reserveSize));
        }
    }
    QDataStream stream(&archiveData, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_6_0);

    // 写入 project.json
    {
        QFile f(projectJsonPath);
        if (f.open(QIODevice::ReadOnly)) {
            QByteArray data = f.readAll();
            stream << QString("project.json") << data;
            f.close();
        }
    }

    // 写入所有图像文件
    for (const QString& imgName : imageFiles) {
        QFile f(imagesDir.absoluteFilePath(imgName));
        if (!f.open(QIODevice::ReadOnly)) continue;

        QByteArray data = f.readAll();
        if (data.isEmpty()) {
            f.close();
            continue;
        }
        stream << QString("images/") + imgName << data;
        f.close();
    }

    // 压缩（zlib，最高压缩级别 9）
    QByteArray compressedData = qCompress(archiveData, 9);
    if (compressedData.isEmpty()) {
        if (errMsg) *errMsg = QString::fromUtf8("压缩项目数据失败，数据可能为空或过大");
        QDir(tempBase).removeRecursively();
        return false;
    }

    // 使用 QSaveFile 原子写入 .qdvproj 文件
    QSaveFile outFile(filePath);
    if (!outFile.open(QIODevice::WriteOnly)) {
        if (errMsg) *errMsg = QString::fromUtf8("无法打开输出文件: %1").arg(outFile.errorString());
        QDir(tempBase).removeRecursively();
        return false;
    }

    outFile.write(compressedData);
    if (!outFile.commit()) {
        if (errMsg) *errMsg = QString::fromUtf8("写入文件失败: %1").arg(outFile.errorString());
        QDir(tempBase).removeRecursively();
        return false;
    }

    // 清理临时目录
    QDir(tempBase).removeRecursively();

    return true;
}

// ==================== 打包图像 ====================

bool ProjectSerializer::bundleImages(TrainingProject* project,
                                     const QString& tempDir,
                                     QString* errMsg)
{
    QString imagesDir = tempDir + "/images";
    QDir dir(imagesDir);
    if (!dir.mkpath(".")) {
        if (errMsg) *errMsg = QString::fromUtf8("无法创建图像目录: %1").arg(imagesDir);
        return false;
    }

    // 获取图像快照的可变引用（需要重写路径）
    QList<ImageEntrySnapshot> snapshots = project->imagesSnapshot();

    for (auto& snap : snapshots) {
        if (snap.filePath.isEmpty()) continue;

        QFile srcFile(snap.filePath);
        if (!srcFile.exists()) {
            // 图像文件不存在，跳过并记录警告
            QDV::Logger::warn(QString("ProjectSerializer: 图像文件不存在，跳过: %1").arg(snap.filePath));
            continue;
        }

        // 计算 SHA256 前 8 位作为文件名（内容寻址，避免重名冲突）
        if (!srcFile.open(QIODevice::ReadOnly)) {
            QDV::Logger::warn(QString("ProjectSerializer: 无法读取图像文件: %1").arg(snap.filePath));
            continue;
        }
        QByteArray fileData = srcFile.readAll();
        srcFile.close();

        QByteArray hash = QCryptographicHash::hash(fileData, QCryptographicHash::Sha256);
        QString sha8 = hash.toHex().left(8);

        // 获取原文件扩展名
        QFileInfo fi(snap.filePath);
        QString ext = fi.suffix();

        // 新文件名: <sha8>.<ext>
        QString newFileName = sha8 + "." + ext;
        QString newPath = imagesDir + "/" + newFileName;

        // 复制文件
        QFile destFile(newPath);
        if (destFile.open(QIODevice::WriteOnly)) {
            destFile.write(fileData);
            destFile.close();
        }

        // 保留原始路径，更新 filePath 为包内相对路径
        snap.originalPath = snap.filePath;
        snap.filePath = "images/" + newFileName;
    }

    // 更新项目中的图像快照
    project->setImagesSnapshot(snapshots);

    return true;
}

// ==================== 引用模式加载 ====================

bool ProjectSerializer::loadReferenceMode(const QString& filePath,
                                          TrainingProject* project,
                                          QStringList* missingImages,
                                          QString* errMsg)
{
    QJsonObject json;
    if (!loadReferenceModeToJson(filePath, &json, missingImages, errMsg)) {
        return false;
    }
    return project->fromJson(json, errMsg);
}

// 线程安全版本：只做文件 I/O 和 JSON 解析，不访问 project 对象
bool ProjectSerializer::loadReferenceModeToJson(const QString& filePath,
                                                QJsonObject* outJson,
                                                QStringList* missingImages,
                                                QString* errMsg)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errMsg) *errMsg = QString::fromUtf8("无法打开文件: %1").arg(file.errorString());
        return false;
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    file.close();

    if (parseError.error != QJsonParseError::NoError) {
        if (errMsg) *errMsg = QString::fromUtf8("JSON解析错误: %1").arg(parseError.errorString());
        return false;
    }

    *outJson = doc.object();

    // 检查图像路径是否存在（引用模式依赖外部文件）
    if (missingImages) {
        QList<ImageEntrySnapshot> images = extractImagesFromJson(*outJson);
        *missingImages = checkMissingImages(images);
    }

    return true;
}

// ==================== 打包模式加载 ====================

bool ProjectSerializer::loadBundledMode(const QString& filePath,
                                        TrainingProject* project,
                                        QString* errMsg)
{
    QJsonObject json;
    if (!loadBundledModeToJson(filePath, &json, errMsg)) {
        return false;
    }
    return project->fromJson(json, errMsg);
}

// 线程安全版本：解压 + JSON 解析 + 路径重写，不访问 project 对象
bool ProjectSerializer::loadBundledModeToJson(const QString& filePath,
                                              QJsonObject* outJson,
                                              QString* errMsg)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errMsg) *errMsg = QString::fromUtf8("无法打开文件: %1").arg(file.errorString());
        return false;
    }

    QByteArray compressedData = file.readAll();
    file.close();

    // 解压
    QByteArray archiveData = qUncompress(compressedData);
    if (archiveData.isEmpty()) {
        if (errMsg) *errMsg = QString::fromUtf8("解压失败: 文件可能已损坏");
        return false;
    }

    // 创建临时目录用于存放解压后的文件
    QString tempBase = QDir::tempPath() + "/qdv_proj_loaded_" +
                       QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss_zzz");
    QDir tempDir(tempBase);
    if (!tempDir.mkpath(".")) {
        if (errMsg) *errMsg = QString::fromUtf8("无法创建临时目录: %1").arg(tempBase);
        return false;
    }

    // 使用 QDataStream 读取多个文件
    QDataStream stream(&archiveData, QIODevice::ReadOnly);
    stream.setVersion(QDataStream::Qt_6_0);

    while (!stream.atEnd()) {
        QString fileName;
        QByteArray fileData;
        stream >> fileName >> fileData;

        QString fullPath = tempBase + "/" + fileName;
        QFileInfo fi(fullPath);
        QDir().mkpath(fi.absolutePath());

        QFile f(fullPath);
        if (f.open(QIODevice::WriteOnly)) {
            f.write(fileData);
            f.close();
        }
    }

    // 读取 project.json
    QString projectJsonPath = tempBase + "/project.json";
    QFile projectFile(projectJsonPath);
    if (!projectFile.open(QIODevice::ReadOnly)) {
        if (errMsg) *errMsg = QString::fromUtf8("无法读取 project.json");
        QDir(tempBase).removeRecursively();
        return false;
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(projectFile.readAll(), &parseError);
    projectFile.close();

    if (parseError.error != QJsonParseError::NoError) {
        if (errMsg) *errMsg = QString::fromUtf8("project.json 解析错误: %1").arg(parseError.errorString());
        QDir(tempBase).removeRecursively();
        return false;
    }

    QJsonObject root = doc.object();

    // 重写图像路径：将包内相对路径替换为临时目录的绝对路径
    // 直接操作 JSON 中的 images 数组，不通过 project 对象
    QJsonObject imagesObj = root["images"].toObject();
    QJsonArray imagesArr = imagesObj["entries"].toArray();
    for (int i = 0; i < imagesArr.size(); ++i) {
        QJsonObject imgObj = imagesArr[i].toObject();
        QString fp = imgObj["filePath"].toString();
        if (fp.startsWith("images/")) {
            imgObj["filePath"] = tempBase + "/" + fp;
            imagesArr[i] = imgObj;
        }
    }
    imagesObj["entries"] = imagesArr;
    root["images"] = imagesObj;

    *outJson = root;

    // 注意：不清理临时目录，因为图像文件需要保留供 ImageManager 加载
    // 临时目录会在程序退出时由系统清理，或由用户手动清理

    return true;
}

// ==================== 从 JSON 提取图像快照 ====================

QList<ImageEntrySnapshot> ProjectSerializer::extractImagesFromJson(const QJsonObject& root)
{
    QList<ImageEntrySnapshot> images;
    QJsonObject imagesObj = root["images"].toObject();
    QJsonArray imagesArr = imagesObj["entries"].toArray();
    for (int i = 0; i < imagesArr.size(); ++i) {
        QJsonObject imgObj = imagesArr[i].toObject();
        ImageEntrySnapshot snap;
        snap.filePath = imgObj["filePath"].toString();
        snap.originalPath = imgObj["originalPath"].toString();
        snap.fileName = imgObj["fileName"].toString();
        snap.width = imgObj["width"].toInt(0);
        snap.height = imgObj["height"].toInt(0);
        snap.channels = imgObj["channels"].toInt(0);
        snap.format = imgObj["format"].toString();
        snap.fileSize = static_cast<qint64>(imgObj["fileSize"].toVariant().toLongLong());
        snap.isAnnotated = imgObj["isAnnotated"].toBool(false);
        snap.label = imgObj["label"].toString();
        snap.isSelected = imgObj["isSelected"].toBool(false);
        images.append(snap);
    }
    return images;
}

// ==================== 检查缺失图像 ====================

QStringList ProjectSerializer::checkMissingImages(const QList<ImageEntrySnapshot>& images)
{
    QStringList missing;
    for (const auto& snap : images) {
        if (!QFile::exists(snap.filePath)) {
            missing.append(snap.filePath);
        }
    }
    return missing;
}

// ==================== 重定位缺失图像 ====================

QStringList ProjectSerializer::relocateMissingImages(QList<ImageEntrySnapshot>& images,
                                                      const QString& newDir)
{
    QStringList relocated;

    for (auto& snap : images) {
        if (!QFile::exists(snap.filePath)) {
            // 在新目录中按 fileName 匹配
            QString newPath = newDir + "/" + snap.fileName;
            if (QFile::exists(newPath)) {
                snap.filePath = newPath;
                relocated.append(snap.fileName);
            }
        }
    }

    return relocated;
}
