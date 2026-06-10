#include "IOView.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QToolBar>
#include <QAction>
#include <QTableWidget>
#include <QHeaderView>
#include <QGroupBox>
#include <QTimer>
#include <QDateTime>

IOView::IOView(QWidget* parent) : QWidget(parent) {
    setupUI();
}

IOView::~IOView() {
}

void IOView::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    QToolBar* toolbar = new QToolBar();
    toolbar->setStyleSheet(R"(
        QToolBar {
            background-color: #2d2d2d;
            border-bottom: 1px solid #444;
            padding: 4px 8px;
            spacing: 6px;
        }
        QToolBar QToolButton {
            background: transparent;
            border: 1px solid transparent;
            border-radius: 4px;
            padding: 6px 12px;
            color: #e0e0e0;
            font-size: 13px;
        }
        QToolBar QToolButton:hover {
            background-color: #555;
            border-color: #555;
        }
        QToolBar QToolButton:disabled {
            color: #666;
        }
    )");

    QAction* refreshAction = toolbar->addAction("刷新");
    QAction* exportAction = toolbar->addAction("导出日志");
    exportAction->setEnabled(false);
    exportAction->setToolTip("即将推出");

    mainLayout->addWidget(toolbar);

    QTableWidget* ioTable = new QTableWidget(0, 4);
    ioTable->setHorizontalHeaderLabels({"通道", "名称", "状态", "更新时间"});
    ioTable->horizontalHeader()->setStretchLastSection(true);
    ioTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ioTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    ioTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ioTable->verticalHeader()->setVisible(false);

    QStringList channels = {"DI-1", "DI-2", "DI-3", "DO-1", "DO-2", "AI-1", "AO-1"};
    QStringList names = {"传感器1", "传感器2", "光电开关", "电磁阀1", "电磁阀2", "模拟输入1", "模拟输出1"};

    ioTable->setRowCount(channels.size());
    for (int i = 0; i < channels.size(); ++i) {
        ioTable->setItem(i, 0, new QTableWidgetItem(channels[i]));
        ioTable->setItem(i, 1, new QTableWidgetItem(names[i]));
        ioTable->setItem(i, 2, new QTableWidgetItem("OFF"));
        ioTable->setItem(i, 3, new QTableWidgetItem(QDateTime::currentDateTime().toString("HH:mm:ss")));
    }

    mainLayout->addWidget(ioTable);

    connect(refreshAction, &QAction::triggered, [ioTable]() {
        for (int i = 0; i < ioTable->rowCount(); ++i) {
            ioTable->item(i, 2)->setText("OFF");
            ioTable->item(i, 3)->setText(QDateTime::currentDateTime().toString("HH:mm:ss"));
        }
    });
}