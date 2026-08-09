// ============================================================================
// ResultExportDialog — 结果导出对话框实现（spec v2 阶段六 Task 14）
// 支持三种格式：JSON / CSV / 图片叠加标注
// ============================================================================

#include "UI/ResultExportDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QRadioButton>
#include <QButtonGroup>
#include <QLineEdit>
#include <QPushButton>
#include <QComboBox>
#include <QLabel>
#include <QFileDialog>
#include <QMessageBox>
#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QIODevice>
#include <QTextStream>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonArray>

#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>

#include <algorithm>

// ----------------------------------------------------------------------------
// 构造
// ----------------------------------------------------------------------------
ResultExportDialog::ResultExportDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(QStringLiteral("导出检测结果"));
    setMinimumWidth(520);
    setupUI();
    setDefaultOutputPath();
    onFormatChanged(0);  // 初始 JSON 格式
}

// ----------------------------------------------------------------------------
// UI 构建
// ----------------------------------------------------------------------------
void ResultExportDialog::setupUI() {
    QVBoxLayout* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(10);

    // --- 顶部摘要 ---
    m_infoLabel = new QLabel(QStringLiteral("未加载结果"), this);
    m_infoLabel->setStyleSheet(QStringLiteral(
        "QLabel { color: #555; padding: 4px 0; }"));
    root->addWidget(m_infoLabel);

    // --- 格式选择区 ---
    QGroupBox* formatGroup = new QGroupBox(QStringLiteral("导出格式"), this);
    QVBoxLayout* formatLay = new QVBoxLayout(formatGroup);
    m_jsonRadio  = new QRadioButton(QStringLiteral("JSON（含完整字段：图像路径/ROI/检测框/置信度/耗时/模型/时间戳）"), this);
    m_csvRadio   = new QRadioButton(QStringLiteral("CSV（每个检测框一行，便于表格软件打开）"), this);
    m_imageRadio = new QRadioButton(QStringLiteral("图片叠加标注（在原图上绘制检测框+标签+ROI，保存为 PNG/JPG）"), this);
    m_jsonRadio->setChecked(true);

    formatLay->addWidget(m_jsonRadio);
    formatLay->addWidget(m_csvRadio);
    formatLay->addWidget(m_imageRadio);

    // 图片格式选择行（仅图片格式时启用）
    QHBoxLayout* imgFmtLay = new QHBoxLayout();
    m_imageFormatLabel = new QLabel(QStringLiteral("图片格式："), this);
    m_imageFormatCombo = new QComboBox(this);
    m_imageFormatCombo->addItem(QStringLiteral("PNG (.png)"), QStringLiteral(".png"));
    m_imageFormatCombo->addItem(QStringLiteral("JPEG (.jpg)"), QStringLiteral(".jpg"));
    imgFmtLay->addWidget(m_imageFormatLabel);
    imgFmtLay->addWidget(m_imageFormatCombo);
    imgFmtLay->addStretch(1);
    formatLay->addLayout(imgFmtLay);

    root->addWidget(formatGroup);

    // --- 输出路径区 ---
    QGroupBox* pathGroup = new QGroupBox(QStringLiteral("输出路径"), this);
    QFormLayout* pathLay = new QFormLayout(pathGroup);
    m_pathEdit = new QLineEdit(this);
    m_pathEdit->setPlaceholderText(QStringLiteral("默认导出到 D:/QDVExports（点击浏览选择目录或文件）"));
    m_browseBtn = new QPushButton(QStringLiteral("浏览..."), this);
    QHBoxLayout* pathRow = new QHBoxLayout();
    pathRow->addWidget(m_pathEdit, 1);
    pathRow->addWidget(m_browseBtn);
    pathLay->addRow(QStringLiteral("路径："), pathRow);
    root->addWidget(pathGroup);

    // --- 按钮区 ---
    QHBoxLayout* btnLay = new QHBoxLayout();
    btnLay->addStretch(1);
    QPushButton* exportBtn = new QPushButton(QStringLiteral("导出"), this);
    QPushButton* cancelBtn = new QPushButton(QStringLiteral("取消"), this);
    exportBtn->setDefault(true);
    btnLay->addWidget(exportBtn);
    btnLay->addWidget(cancelBtn);
    root->addLayout(btnLay);

    // --- 信号槽 ---
    m_formatGroup = new QButtonGroup(this);
    m_formatGroup->addButton(m_jsonRadio, 0);
    m_formatGroup->addButton(m_csvRadio, 1);
    m_formatGroup->addButton(m_imageRadio, 2);
    connect(m_formatGroup, &QButtonGroup::idClicked,
            this, &ResultExportDialog::onFormatChanged);
    connect(m_browseBtn, &QPushButton::clicked,
            this, &ResultExportDialog::onBrowsePath);
    connect(exportBtn, &QPushButton::clicked,
            this, &ResultExportDialog::onExport);
    connect(cancelBtn, &QPushButton::clicked,
            this, &QDialog::reject);
}

// ----------------------------------------------------------------------------
// 默认输出路径：建议 D 盘（遵守 AGENTS.md 存储要求）
// ----------------------------------------------------------------------------
void ResultExportDialog::setDefaultOutputPath() {
    // 优先 D 盘；D 盘不存在时回退到用户文档目录
    const QString dDrive = QStringLiteral("D:/QDVExports");
    QDir d(dDrive);
    if (d.exists() || QDir().mkpath(dDrive)) {
        m_pathEdit->setText(dDrive);
    } else {
        m_pathEdit->setText(QDir::homePath() + QStringLiteral("/QDVExports"));
    }
}

// ----------------------------------------------------------------------------
// 导入待导出的结果数据
// ----------------------------------------------------------------------------
void ResultExportDialog::setResults(const QJsonObject& results,
                                    const cv::Mat& image,
                                    const QVariantMap& metadata) {
    m_results  = results;
    m_image    = image;
    m_metadata = metadata;

    // 更新顶部摘要
    const int detCount = results.value("num_detections").toInt(
        results.value("detections").toArray().size());
    const QString imagePath = metadata.value("imagePath").toString();
    const QString modelName = metadata.value("modelName").toString();
    m_infoLabel->setText(QStringLiteral(
        "图像: <b>%1</b><br>"
        "检测框: <b>%2</b> 个 &nbsp;|&nbsp; 模型: <b>%3</b>")
        .arg(QFileInfo(imagePath).fileName().toHtmlEscaped())
        .arg(detCount)
        .arg(modelName.toHtmlEscaped()));
}

// ----------------------------------------------------------------------------
// 浏览输出路径：JSON/CSV 选文件，图片格式选目录（保存为多个文件场景）
// 实际策略：所有格式都选"保存文件"对话框，由用户指定完整路径
// ----------------------------------------------------------------------------
void ResultExportDialog::onBrowsePath() {
    const int fmtId = m_formatGroup->checkedId();
    if (fmtId == 2) {
        // 图片格式：选保存文件
        const QString ext = m_imageFormatCombo->currentData().toString();
        const QString filter = (ext == QStringLiteral(".png"))
            ? QStringLiteral("PNG 图像 (*.png)")
            : QStringLiteral("JPEG 图像 (*.jpg *.jpeg)");
        const QString path = QFileDialog::getSaveFileName(
            this, QStringLiteral("选择导出图片路径"),
            m_pathEdit->text() + defaultFileName(ext), filter);
        if (!path.isEmpty()) m_pathEdit->setText(path);
    } else if (fmtId == 1) {
        // CSV：选保存文件
        const QString path = QFileDialog::getSaveFileName(
            this, QStringLiteral("选择导出 CSV 路径"),
            m_pathEdit->text() + defaultFileName(".csv"),
            QStringLiteral("CSV 文件 (*.csv)"));
        if (!path.isEmpty()) m_pathEdit->setText(path);
    } else {
        // JSON：选保存文件
        const QString path = QFileDialog::getSaveFileName(
            this, QStringLiteral("选择导出 JSON 路径"),
            m_pathEdit->text() + defaultFileName(".json"),
            QStringLiteral("JSON 文件 (*.json)"));
        if (!path.isEmpty()) m_pathEdit->setText(path);
    }
}

// ----------------------------------------------------------------------------
// 格式切换：图片格式时启用图片选项
// ----------------------------------------------------------------------------
void ResultExportDialog::onFormatChanged(int id) {
    const bool isImage = (id == 2);
    m_imageFormatCombo->setEnabled(isImage);
    m_imageFormatLabel->setEnabled(isImage);

    // 切换格式时自动更新路径后缀（仅当路径无后缀或后缀不匹配时）
    QString curPath = m_pathEdit->text().trimmed();
    if (curPath.isEmpty()) return;
    QFileInfo fi(curPath);
    QString base = fi.path() + "/" + fi.completeBaseName();
    if (base.endsWith("/")) base.chop(1);

    QString newSuffix;
    if (id == 0) newSuffix = ".json";
    else if (id == 1) newSuffix = ".csv";
    else newSuffix = m_imageFormatCombo->currentData().toString();

    // 仅当当前路径是文件路径（不是目录）才更新后缀
    if (!QFileInfo(curPath).isDir()) {
        m_pathEdit->setText(base + newSuffix);
    }
}

// ----------------------------------------------------------------------------
// 执行导出
// ----------------------------------------------------------------------------
void ResultExportDialog::onExport() {
    QString path = m_pathEdit->text().trimmed();
    if (path.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("路径为空"),
            QStringLiteral("请先指定输出路径。"));
        return;
    }

    // 若用户填的是目录，则拼接默认文件名
    if (QFileInfo(path).isDir()) {
        QString suffix;
        const int fmtId = m_formatGroup->checkedId();
        if (fmtId == 0) suffix = ".json";
        else if (fmtId == 1) suffix = ".csv";
        else suffix = m_imageFormatCombo->currentData().toString();
        path = path + "/" + defaultFileName(suffix);
        // QDir 路径分隔符统一
        path = QDir::toNativeSeparators(path);
    }

    // 确保目录存在
    QDir().mkpath(QFileInfo(path).absolutePath());

    const int fmtId = m_formatGroup->checkedId();
    QString err;
    bool ok = false;
    if (fmtId == 0) {
        ok = exportJson(path, &err);
    } else if (fmtId == 1) {
        ok = exportCsv(path, &err);
    } else {
        // 图片格式：若后缀不匹配则补全
        const QString wantExt = m_imageFormatCombo->currentData().toString();
        if (!path.endsWith(wantExt, Qt::CaseInsensitive)) {
            path += wantExt;
        }
        ok = exportAnnotatedImage(path, &err);
    }

    if (ok) {
        QMessageBox::information(this, QStringLiteral("导出成功"),
            QStringLiteral("已导出到：\n%1").arg(path));
        accept();
    } else {
        QMessageBox::warning(this, QStringLiteral("导出失败"), err);
    }
}

// ============================================================================
// JSON 导出（SubTask 14.2）
// 字段：imagePath/roi/detections(每项 cx/cy/w/h/confidence/classId/className)
//      /anomalyScore/confidence/elapsedMs/modelName/timestamp
// ============================================================================
bool ResultExportDialog::exportJson(const QString& filePath, QString* err) {
    QJsonObject obj;

    // --- 元数据 ---
    obj["imagePath"]  = m_metadata.value("imagePath").toString();
    obj["modelName"]  = m_metadata.value("modelName").toString();
    // QVariantMap::value(key, defaultValue) 返回 QVariant，再 .toDouble() 取值
    // （QVariant::toDouble(bool*) 不接收默认值参数，需用 value(key, default) 模式）
    obj["elapsedMs"]  = m_metadata.value("elapsedMs",
                            m_results.value("latency_ms").toDouble()).toDouble();
    obj["timestamp"]  = m_metadata.value("timestamp",
                            QDateTime::currentDateTime().toString(Qt::ISODate)).toString();

    // --- ROI（保持原 JSON 结构，可能是 {type,x,y,w,h} 或 {type,points:[...]}） ---
    const QVariantMap roi = m_metadata.value("roi").toMap();
    if (!roi.isEmpty()) {
        QJsonObject roiObj;
        const QString type = roi.value("type").toString();
        roiObj["type"] = type;
        if (type == QStringLiteral("rect")) {
            roiObj["x"] = roi.value("x").toInt();
            roiObj["y"] = roi.value("y").toInt();
            roiObj["w"] = roi.value("w").toInt();
            roiObj["h"] = roi.value("h").toInt();
        } else if (type == QStringLiteral("polygon")) {
            QJsonArray pts;
            const QVariantList ptsList = roi.value("points").toList();
            for (const QVariant& v : ptsList) {
                const QVariantMap pm = v.toMap();
                QJsonObject pt;
                pt["x"] = pm.value("x").toInt();
                pt["y"] = pm.value("y").toInt();
                pts.append(pt);
            }
            roiObj["points"] = pts;
        }
        obj["roi"] = roiObj;
    }

    // --- 标量结果 ---
    obj["anomalyScore"] = m_results.value("anomaly_score").toDouble(
                              m_results.value("anomalyScore").toDouble(0.0));
    obj["confidence"]   = m_results.value("confidence").toDouble(0.0);
    obj["category"]     = m_results.value("category").toString();

    // --- detections 数组 ---
    // 输入兼容两种格式：
    //   a) m_results["detections"] 是数组（ZeroShotResult.toJson，归一化坐标）
    //   b) m_metadata["detections"] 是 QVariantList（ZeroShotDetectTool 输出，像素坐标）
    // 优先使用 metadata 中的像素坐标版本（更直观，与图片叠加标注一致）
    QJsonArray detArray;
    const QVariantList metaDets = m_metadata.value("detections").toList();
    if (!metaDets.isEmpty()) {
        for (const QVariant& v : metaDets) {
            const QVariantMap d = v.toMap();
            QJsonObject det;
            det["cx"]         = d.value("cx").toDouble();
            det["cy"]         = d.value("cy").toDouble();
            det["w"]          = d.value("w").toDouble();
            det["h"]          = d.value("h").toDouble();
            det["confidence"] = d.value("confidence").toDouble();
            det["classId"]    = d.value("classId").toInt();
            det["className"]  = d.value("className").toString();
            detArray.append(det);
        }
    } else if (m_results.contains("detections")) {
        // 回退：使用 m_results["detections"]（归一化坐标）
        detArray = m_results.value("detections").toArray();
    }
    obj["detections"]     = detArray;
    obj["numDetections"]  = detArray.size();

    // --- 写文件（格式化缩进输出） ---
    QJsonDocument doc(obj);
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (err) *err = QStringLiteral("无法写入文件：%1\n%2")
                            .arg(filePath, file.errorString());
        return false;
    }
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();
    return true;
}

// ============================================================================
// CSV 导出（SubTask 14.3）
// 表头：imagePath,classId,className,cx,cy,w,h,confidence,anomalyScore,elapsedMs,modelName
// 每个检测框一行
// ============================================================================
bool ResultExportDialog::exportCsv(const QString& filePath, QString* err) {
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        if (err) *err = QStringLiteral("无法写入文件：%1\n%2")
                            .arg(filePath, file.errorString());
        return false;
    }
    QTextStream ts(&file);
    ts.setEncoding(QStringConverter::Utf8);
    // UTF-8 BOM（让 Excel 正确识别中文）
    ts << QChar(0xFEFF);

    // 表头
    ts << QStringLiteral("imagePath,classId,className,cx,cy,w,h,confidence,anomalyScore,elapsedMs,modelName\n");

    // 元数据公共字段（每行重复）
    const QString imagePath = m_metadata.value("imagePath").toString();
    const QString modelName = m_metadata.value("modelName").toString();
    // QVariantMap::value(key, defaultValue) 返回 QVariant，再 .toDouble() 取值
    const double  elapsedMs = m_metadata.value("elapsedMs",
                                  m_results.value("latency_ms").toDouble()).toDouble();
    const double  anomalyScore = m_results.value("anomaly_score").toDouble(
                                     m_results.value("anomalyScore").toDouble(0.0));

    // 检测框（优先 metadata 像素坐标版本）
    const QVariantList dets = extractDetections();
    if (dets.isEmpty()) {
        // 无检测框：写一行空检测（保留 imagePath/元数据，便于统计"无缺陷"图像）
        ts << imagePath << ",,,"
           << ",,,,," << anomalyScore << "," << elapsedMs << "," << modelName << "\n";
    } else {
        for (const QVariant& v : dets) {
            const QVariantMap d = v.toMap();
            ts << imagePath << ","
               << d.value("classId").toInt() << ","
               << escapeCsv(d.value("className").toString()) << ","
               << d.value("cx").toDouble() << ","
               << d.value("cy").toDouble() << ","
               << d.value("w").toDouble() << ","
               << d.value("h").toDouble() << ","
               << d.value("confidence").toDouble() << ","
               << anomalyScore << ","
               << elapsedMs << ","
               << modelName << "\n";
        }
    }
    file.close();
    return true;
}

// ----------------------------------------------------------------------------
// CSV 字段转义：含逗号/引号/换行时用双引号包裹，内部引号翻倍
// ----------------------------------------------------------------------------
QString ResultExportDialog::escapeCsv(const QString& s) const {
    if (!s.contains(',') && !s.contains('"') && !s.contains('\n')) {
        return s;
    }
    QString escaped = s;
    escaped.replace('"', "\"\"");
    return QStringLiteral("\"%1\"").arg(escaped);
}

// ============================================================================
// 图片叠加标注导出（SubTask 14.4）
// 在原图上绘制：检测框（cv::rectangle）+ 标签+置信度（cv::putText）
//              + ROI（如有，用不同颜色绘制）
// 保存为 PNG/JPG
// ============================================================================
bool ResultExportDialog::exportAnnotatedImage(const QString& filePath, QString* err) {
    if (m_image.empty()) {
        if (err) *err = QStringLiteral("原图为空，无法生成标注图。");
        return false;
    }

    // 拷贝原图（避免修改源数据）
    cv::Mat canvas = m_image.clone();
    if (canvas.channels() == 1) {
        cv::cvtColor(canvas, canvas, cv::COLOR_GRAY2BGR);
    } else if (canvas.channels() == 4) {
        cv::cvtColor(canvas, canvas, cv::COLOR_BGRA2BGR);
    }

    // 绘制标注
    drawAnnotations(canvas);

    // 保存（cv::imwrite 不支持中文路径，使用 QFile + imencode 规避）
    const QString ext = m_imageFormatCombo->currentData().toString().toLower();
    const std::string extStd = ext.toStdString();  // ".png" / ".jpg"
    std::vector<uchar> buf;
    bool enc = false;
    if (ext == QStringLiteral(".png")) {
        enc = cv::imencode(".png", canvas, buf);
    } else {
        // JPEG：质量 95
        std::vector<int> params = {cv::IMWRITE_JPEG_QUALITY, 95};
        enc = cv::imencode(".jpg", canvas, buf, params);
    }
    if (!enc) {
        if (err) *err = QStringLiteral("图像编码失败。");
        return false;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (err) *err = QStringLiteral("无法写入文件：%1\n%2")
                            .arg(filePath, file.errorString());
        return false;
    }
    file.write(reinterpret_cast<const char*>(buf.data()), static_cast<qint64>(buf.size()));
    file.close();
    return true;
}

// ----------------------------------------------------------------------------
// 在画布上绘制：检测框 + 标签 + 置信度 + ROI
// ----------------------------------------------------------------------------
void ResultExportDialog::drawAnnotations(cv::Mat& canvas) const {
    // --- 1. ROI（如有，先画，避免被检测框覆盖） ---
    const QVariantMap roi = m_metadata.value("roi").toMap();
    const QString roiType = roi.value("type").toString();
    const cv::Scalar roiColor(255, 0, 128);  // 紫红色（与检测框颜色区分）
    const int roiThickness = 2;
    if (roiType == QStringLiteral("rect")) {
        const int x = roi.value("x").toInt();
        const int y = roi.value("y").toInt();
        const int w = roi.value("w").toInt();
        const int h = roi.value("h").toInt();
        cv::rectangle(canvas, cv::Point(x, y), cv::Point(x + w, y + h),
                      roiColor, roiThickness);
        // ROI 标签
        cv::putText(canvas, "ROI", cv::Point(x + 4, y + 16),
                    cv::FONT_HERSHEY_SIMPLEX, 0.5, roiColor, 1);
    } else if (roiType == QStringLiteral("polygon")) {
        const QVariantList pts = roi.value("points").toList();
        if (pts.size() >= 2) {
            std::vector<cv::Point> cvPts;
            for (const QVariant& v : pts) {
                const QVariantMap pm = v.toMap();
                cvPts.emplace_back(pm.value("x").toInt(), pm.value("y").toInt());
            }
            // 画多边形轮廓
            for (size_t i = 0; i < cvPts.size(); ++i) {
                cv::line(canvas, cvPts[i], cvPts[(i + 1) % cvPts.size()],
                         roiColor, roiThickness);
            }
        }
    }

    // --- 2. 检测框 + 标签 + 置信度 ---
    // 按类别分配颜色（与 ZeroShotDetectTool 风格一致）
    static const cv::Scalar kColors[] = {
        cv::Scalar(0, 0, 255),     // 红
        cv::Scalar(0, 255, 0),     // 绿
        cv::Scalar(255, 0, 0),     // 蓝
        cv::Scalar(0, 255, 255),   // 黄
        cv::Scalar(255, 0, 255),   // 紫
        cv::Scalar(255, 255, 0),   // 青
        cv::Scalar(128, 0, 0),     // 深蓝
        cv::Scalar(0, 128, 0),     // 深绿
    };
    const int numColors = sizeof(kColors) / sizeof(kColors[0]);

    const QVariantList dets = extractDetections();
    for (const QVariant& v : dets) {
        const QVariantMap d = v.toMap();
        const int classId = d.value("classId").toInt();
        const double conf = d.value("confidence").toDouble();
        const double cx = d.value("cx").toDouble();
        const double cy = d.value("cy").toDouble();
        const double w  = d.value("w").toDouble();
        const double h  = d.value("h").toDouble();

        // 中心点 + 宽高 → 左上角 + 右下角
        int x1 = static_cast<int>(cx - w / 2.0);
        int y1 = static_cast<int>(cy - h / 2.0);
        int x2 = static_cast<int>(cx + w / 2.0);
        int y2 = static_cast<int>(cy + h / 2.0);
        // 钳制到图像边界
        x1 = std::max(0, std::min(x1, canvas.cols - 1));
        y1 = std::max(0, std::min(y1, canvas.rows - 1));
        x2 = std::max(0, std::min(x2, canvas.cols - 1));
        y2 = std::max(0, std::min(y2, canvas.rows - 1));

        const cv::Scalar color = kColors[std::abs(classId) % numColors];
        cv::rectangle(canvas, cv::Point(x1, y1), cv::Point(x2, y2), color, 2);

        // 标签：className + 置信度
        QString className = d.value("className").toString();
        if (className.isEmpty()) {
            className = QStringLiteral("Class_%1").arg(classId);
        }
        const QString label = QStringLiteral("%1: %2%")
            .arg(className).arg(QString::number(conf * 100, 'f', 1));
        const std::string labelStr = label.toStdString();

        int baseline = 0;
        const double fontScale = 0.5;
        const int thickness = 1;
        const cv::Size textSize = cv::getTextSize(labelStr, cv::FONT_HERSHEY_SIMPLEX,
                                                   fontScale, thickness, &baseline);
        const int textY = std::max(y1 - textSize.height - 4, 0);
        cv::rectangle(canvas, cv::Point(x1, textY),
                      cv::Point(x1 + textSize.width + 4, textY + textSize.height + 4),
                      color, cv::FILLED);
        cv::putText(canvas, labelStr, cv::Point(x1 + 2, textY + textSize.height),
                    cv::FONT_HERSHEY_SIMPLEX, fontScale, cv::Scalar(0, 0, 0), thickness);
    }
}

// ----------------------------------------------------------------------------
// 默认文件名（含时间戳，避免覆盖）
// ----------------------------------------------------------------------------
QString ResultExportDialog::defaultFileName(const QString& suffix) const {
    const QString base = QStringLiteral("zeroshot_result");
    const QString ts = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"));
    return base + "_" + ts + suffix;
}

// ----------------------------------------------------------------------------
// 提取检测项列表（优先 metadata 像素坐标，回退 results 归一化坐标）
// ----------------------------------------------------------------------------
QVariantList ResultExportDialog::extractDetections() const {
    const QVariantList metaDets = m_metadata.value("detections").toList();
    if (!metaDets.isEmpty()) {
        return metaDets;
    }
    // 回退：m_results["detections"]（归一化坐标）→ 乘以图像宽高转像素坐标
    QVariantList out;
    if (!m_results.contains("detections")) return out;
    const QJsonArray arr = m_results.value("detections").toArray();
    const int imgW = m_image.cols;
    const int imgH = m_image.rows;
    for (const QJsonValue& v : arr) {
        const QJsonObject d = v.toObject();
        QVariantMap m;
        m["cx"]         = d.value("cx").toDouble() * imgW;
        m["cy"]         = d.value("cy").toDouble() * imgH;
        m["w"]          = d.value("w").toDouble()  * imgW;
        m["h"]          = d.value("h").toDouble()  * imgH;
        m["confidence"] = d.value("confidence").toDouble();
        m["classId"]    = d.value("class_id").toInt(d.value("classId").toInt());
        m["className"]  = d.value("class_name").toString(d.value("className").toString());
        out.append(m);
    }
    return out;
}
