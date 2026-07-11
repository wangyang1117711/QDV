#include "SchemeView.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QToolBar>
#include <QAction>
#include <QTreeWidget>
#include <QHeaderView>
#include <QInputDialog>
#include <QFileDialog>
#include <QMessageBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QDateTime>

SchemeView::SchemeView(QWidget* parent) : QWidget(parent) {
    setupUI();
}

SchemeView::~SchemeView() {
}

void SchemeView::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    QToolBar* toolbar = new QToolBar();
    toolbar->setStyleSheet(R"(
        QToolBar {
            background-color: #2d2d2d;
            border-bottom: 1px solid #444;
            padding: 3px 12px;
            spacing: 6px;
        }
        QToolBar QToolButton {
            background: transparent;
            border: 1px solid transparent;
            border-radius: 4px;
            padding: 4px 14px;
            color: #e0e0e0;
            font-size: 12px;
        }
        QToolBar QToolButton:hover {
            background-color: #555;
            border-color: #555;
        }
        QToolBar QToolButton:disabled {
            color: #666;
        }
    )");

    QAction* newAction = toolbar->addAction("新建方案");
    QAction* saveAction = toolbar->addAction("保存方案");
    QAction* loadAction = toolbar->addAction("加载方案");
    QAction* deleteAction = toolbar->addAction("删除方案");
    deleteAction->setEnabled(false);
    deleteAction->setToolTip("即将推出");

    mainLayout->addWidget(toolbar);

    // v5.0：添加内容区域 margin
    QWidget* treeWrapper = new QWidget();
    QVBoxLayout* treeLayout = new QVBoxLayout(treeWrapper);
    treeLayout->setContentsMargins(16, 12, 16, 12);
    treeLayout->setSpacing(0);

    m_schemeTree = new QTreeWidget();
    m_schemeTree->setHeaderLabels({"方案名称", "版本", "工具数", "更新时间"});
    m_schemeTree->setColumnWidth(0, 200);
    m_schemeTree->setColumnWidth(1, 80);
    m_schemeTree->setColumnWidth(2, 80);
    m_schemeTree->setAlternatingRowColors(true);
    m_schemeTree->setSelectionMode(QAbstractItemView::SingleSelection);
    treeLayout->addWidget(m_schemeTree);
    mainLayout->addWidget(treeWrapper, 1);  // v5.0：占满剩余空间

    connect(newAction, &QAction::triggered, this, &SchemeView::onNewScheme);
    connect(saveAction, &QAction::triggered, this, &SchemeView::onSaveScheme);
    connect(loadAction, &QAction::triggered, this, &SchemeView::onLoadScheme);
}

void SchemeView::onNewScheme() {
    bool ok;
    QString name = QInputDialog::getText(this, "新建方案", "方案名称:", QLineEdit::Normal, "", &ok);
    if (!ok || name.trimmed().isEmpty()) return;

    SchemeData data;
    data.name = name.trimmed();
    data.version = "1.0";
    data.author = "admin";
    data.description = "";
    m_schemeMap[name.trimmed()] = data;

    refreshSchemeTree();
    emit schemeCountChanged(m_schemeMap.size());
}

void SchemeView::onSaveScheme() {
    QString filePath = QFileDialog::getSaveFileName(this, "保存方案", "", "JSON Files (*.json)");
    if (filePath.isEmpty()) return;
    saveSchemesToFile(filePath);
}

void SchemeView::onLoadScheme() {
    QString filePath = QFileDialog::getOpenFileName(this, "加载方案", "", "JSON Files (*.json)");
    if (filePath.isEmpty()) return;
    loadSchemesFromFile(filePath);
    refreshSchemeTree();
    emit schemeCountChanged(m_schemeMap.size());
}

void SchemeView::saveSchemesToFile(const QString& filePath) {
    QJsonArray arr;
    for (auto it = m_schemeMap.begin(); it != m_schemeMap.end(); ++it) {
        QJsonObject obj;
        obj["name"] = it.value().name;
        obj["version"] = it.value().version;
        obj["author"] = it.value().author;
        obj["description"] = it.value().description;
        QJsonArray toolsArr;
        for (const auto& tool : it.value().tools) {
            QJsonArray toolArr;
            for (const auto& field : tool) {
                toolArr.append(field);
            }
            toolsArr.append(toolArr);
        }
        obj["tools"] = toolsArr;
        arr.append(obj);
    }
    QJsonDocument doc(arr);
    QFile file(filePath);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(doc.toJson(QJsonDocument::Indented));
        file.close();
    }
}

void SchemeView::loadSchemesFromFile(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) return;

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    if (!doc.isArray()) return;

    QJsonArray arr = doc.array();
    for (int i = 0; i < arr.size(); ++i) {
        QJsonObject obj = arr[i].toObject();
        SchemeData data;
        data.name = obj["name"].toString();
        data.version = obj["version"].toString();
        data.author = obj["author"].toString();
        data.description = obj["description"].toString();
        QJsonArray toolsArr = obj["tools"].toArray();
        for (int j = 0; j < toolsArr.size(); ++j) {
            QJsonArray toolArr = toolsArr[j].toArray();
            QStringList fields;
            for (int k = 0; k < toolArr.size(); ++k) {
                fields.append(toolArr[k].toString());
            }
            data.tools.append(fields);
        }
        m_schemeMap[data.name] = data;
    }
}

void SchemeView::refreshSchemeTree() {
    m_schemeTree->clear();
    for (auto it = m_schemeMap.begin(); it != m_schemeMap.end(); ++it) {
        QTreeWidgetItem* item = new QTreeWidgetItem();
        item->setText(0, it.value().name);
        item->setText(1, it.value().version);
        item->setText(2, QString::number(it.value().tools.size()));
        item->setText(3, QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm"));
        m_schemeTree->addTopLevelItem(item);
    }
}