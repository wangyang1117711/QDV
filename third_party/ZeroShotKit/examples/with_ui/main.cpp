// ============================================================================
// 带 UI 集成示例
// 演示如何将 ZeroShotKit 的 ZeroShotPanel 和 ZeroShotResultPanel 嵌入
// 自定义 QMainWindow，并通过信号槽连接完成端到端推理流程。
//
// 本示例展示完整的工作流：
//   - 左侧 ZeroShotPanel：配置模型类型、路径、阈值、提示词
//   - 右侧 ZeroShotResultPanel：展示推理结果、检测框、热力图、性能指标
//   - 菜单栏：打开图像 / 打开目录 / 退出
//   - 状态栏：显示当前模型、推理耗时、进度
// ============================================================================

#include <QApplication>
#include <QMainWindow>
#include <QSplitter>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QStatusBar>
#include <QMessageBox>
#include <QFileDialog>
#include <QLabel>
#include <QDir>
#include <QDirIterator>
#include <QStringList>
#include <QList>

#include "ZeroShotKit/ZeroShotKit.h"
#include "ZeroShotKit/ZeroShotTypes.h"
#include "ZeroShotPanel.h"
#include "ZeroShotResultPanel.h"

// ============================================================================
// 主窗口
// ============================================================================
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr)
        : QMainWindow(parent)
    {
        setWindowTitle(QString::fromUtf8("ZeroShotKit - UI 集成示例"));
        resize(1280, 800);

        // --- 1. 创建 Kit 实例（生命周期由 this 管理） ---
        m_kit = new zsu::Kit(this);

        // --- 2. 创建左右面板 ---
        m_panel       = new QDVMini::ZeroShotPanel(this);
        m_resultPanel = new QDVMini::ZeroShotResultPanel(this);

        // 将 Kit 内部的引擎、管理器注入到配置面板
        m_panel->setEngine(m_kit->engine());
        m_panel->setModelNotesManager(m_kit->notesManager());
        m_panel->setBadCaseRecorder(m_kit->badCaseRecorder());

        // --- 3. 用 QSplitter 水平布局 ---
        QSplitter* splitter = new QSplitter(Qt::Horizontal, this);
        splitter->addWidget(m_panel);
        splitter->addWidget(m_resultPanel);
        splitter->setStretchFactor(0, 1);  // 左侧配置面板
        splitter->setStretchFactor(1, 3);  // 右侧结果面板
        setCentralWidget(splitter);

        // --- 4. 菜单栏 ---
        setupMenuBar();

        // --- 5. 状态栏 ---
        setupStatusBar();

        // --- 6. 信号槽连接 ---
        connectSignals();
    }

    ~MainWindow() override = default;

private slots:
    // 菜单：打开单张图像
    void onOpenImage()
    {
        const QString path = QFileDialog::getOpenFileName(
            this,
            QString::fromUtf8("选择图像"),
            QString(),
            QString::fromUtf8("图像文件 (*.jpg *.jpeg *.png *.bmp);;所有文件 (*.*)"));
        if (path.isEmpty()) {
            return;
        }
        m_currentImagePath = path;
        m_currentImageDir.clear();
        statusBar()->showMessage(QString::fromUtf8("已选择图像: %1").arg(path), 3000);
    }

    // 菜单：打开目录
    void onOpenDirectory()
    {
        const QString dir = QFileDialog::getExistingDirectory(
            this,
            QString::fromUtf8("选择图像目录"),
            QString());
        if (dir.isEmpty()) {
            return;
        }
        m_currentImageDir = dir;
        m_currentImagePath.clear();
        statusBar()->showMessage(QString::fromUtf8("已选择目录: %1").arg(dir), 3000);
    }

private:
    // 构建菜单栏
    void setupMenuBar()
    {
        QMenu* fileMenu = menuBar()->addMenu(QString::fromUtf8("文件(&F)"));

        // 打开图像
        QAction* openImageAct = new QAction(QString::fromUtf8("打开图像...(&I)"), this);
        openImageAct->setShortcut(QKeySequence::Open);
        connect(openImageAct, &QAction::triggered, this, &MainWindow::onOpenImage);
        fileMenu->addAction(openImageAct);

        // 打开目录
        QAction* openDirAct = new QAction(QString::fromUtf8("打开目录...(&D)"), this);
        connect(openDirAct, &QAction::triggered, this, &MainWindow::onOpenDirectory);
        fileMenu->addAction(openDirAct);

        fileMenu->addSeparator();

        // 退出
        QAction* exitAct = new QAction(QString::fromUtf8("退出(&X)"), this);
        exitAct->setShortcut(QKeySequence::Quit);
        connect(exitAct, &QAction::triggered, this, &QMainWindow::close);
        fileMenu->addAction(exitAct);
    }

    // 构建状态栏
    void setupStatusBar()
    {
        m_modelLabel    = new QLabel(QString::fromUtf8("模型: 未加载"), this);
        m_latencyLabel  = new QLabel(QString::fromUtf8("耗时: -"), this);
        m_progressLabel = new QLabel(QString::fromUtf8("就绪"), this);

        statusBar()->addWidget(m_modelLabel);
        statusBar()->addWidget(m_latencyLabel);
        statusBar()->addPermanentWidget(m_progressLabel);
    }

    // 连接所有信号槽
    void connectSignals()
    {
        // --- 配置面板 → Kit：加载模型 ---
        connect(m_panel, &QDVMini::ZeroShotPanel::modelLoadRequested,
                this, [this](zsu::ZeroShotModelType type, const QString& path) {
            const bool ok = m_kit->loadModel(type, path);
            const QString typeName = m_panel->modelTypeToString(type);
            if (ok) {
                m_panel->updateModelStatus(true, typeName);
                m_modelLabel->setText(QString::fromUtf8("模型: %1").arg(typeName));
                statusBar()->showMessage(QString::fromUtf8("模型加载成功"), 3000);
            } else {
                m_panel->updateModelStatus(false, typeName,
                                           QString::fromUtf8("模型加载失败，请检查路径"));
                QMessageBox::warning(this, QString::fromUtf8("错误"),
                                     QString::fromUtf8("模型加载失败，请检查路径是否正确:\n%1").arg(path));
            }
        });

        // --- 配置面板 → Kit：推理当前 ---
        connect(m_panel, &QDVMini::ZeroShotPanel::inferenceRequested, this, [this]() {
            if (!ensureModelLoaded()) return;
            if (m_currentImagePath.isEmpty()) {
                QMessageBox::information(this, QString::fromUtf8("提示"),
                                         QString::fromUtf8("请先通过 菜单 > 文件 > 打开图像 选择一张图像"));
                return;
            }
            runInference(QStringList{ m_currentImagePath });
        });

        // --- 配置面板 → Kit：推理全部 ---
        connect(m_panel, &QDVMini::ZeroShotPanel::inferenceAllRequested, this, [this]() {
            if (!ensureModelLoaded()) return;
            if (m_currentImageDir.isEmpty()) {
                QMessageBox::information(this, QString::fromUtf8("提示"),
                                         QString::fromUtf8("请先通过 菜单 > 文件 > 打开目录 选择图像目录"));
                return;
            }
            const QStringList files = collectImageFiles(m_currentImageDir);
            if (files.isEmpty()) {
                QMessageBox::information(this, QString::fromUtf8("提示"),
                                         QString::fromUtf8("所选目录下未找到图像文件"));
                return;
            }
            runInference(files);
        });

        // --- 配置面板 → Kit：停止 ---
        connect(m_panel, &QDVMini::ZeroShotPanel::stopRequested, this, [this]() {
            m_kit->cancel();
            m_progressLabel->setText(QString::fromUtf8("已请求停止"));
        });

        // --- 配置面板：任何配置变化 → 同步到 Kit ---
        connect(m_panel, &QDVMini::ZeroShotPanel::settingsChanged, this, [this]() {
            syncPanelToKit();
        });

        // --- Kit → 结果面板：单张推理完成 ---
        connect(m_kit, &zsu::Kit::inferenceCompleted, this,
                [this](const zsu::ZeroShotResult& r) {
            m_resultPanel->setResult(r);
            m_latencyLabel->setText(QString::fromUtf8("耗时: %1 ms").arg(r.metrics.totalMs));
            m_progressLabel->setText(QString::fromUtf8("推理完成"));
            if (!r.success) {
                QMessageBox::warning(this, QString::fromUtf8("推理失败"), r.errorMessage);
            }
        });

        // --- Kit → 结果面板：批量推理完成 ---
        connect(m_kit, &zsu::Kit::batchCompleted, this,
                [this](const QList<zsu::ZeroShotResult>& rs) {
            m_resultPanel->setResults(rs);
            qint64 totalMs = 0;
            for (const auto& r : rs) {
                totalMs += r.metrics.totalMs;
            }
            const double avg = rs.isEmpty() ? 0.0 : static_cast<double>(totalMs) / rs.size();
            m_latencyLabel->setText(QString::fromUtf8("平均耗时: %1 ms").arg(avg, 0, 'f', 1));
            m_progressLabel->setText(QString::fromUtf8("批量完成 (%1 张)").arg(rs.size()));
        });

        // --- Kit → 状态栏：进度更新 ---
        connect(m_kit, &zsu::Kit::progressUpdated, this,
                [this](int current, int total) {
            m_resultPanel->setProgress(current, total);
            m_progressLabel->setText(
                QString::fromUtf8("进度: %1 / %2").arg(current).arg(total));
        });

        // --- Kit → 弹窗：错误 ---
        connect(m_kit, &zsu::Kit::errorOccurred, this, [this](const QString& msg) {
            m_progressLabel->setText(QString::fromUtf8("发生错误"));
            QMessageBox::warning(this, QString::fromUtf8("错误"), msg);
        });
    }

    // 将面板上的配置同步到 Kit
    void syncPanelToKit()
    {
        m_kit->setTextPrompts(m_panel->textPrompts());
        m_kit->setAnomalyThreshold(m_panel->anomalyThreshold());
        m_kit->setDetectionThreshold(m_panel->detectionThreshold());

        // 稳定性配置
        zsu::StabilityConfig sc;
        sc.enableMultiRunStability = m_panel->multiRunStabilityEnabled();
        sc.numRuns                 = m_panel->multiRunCount();
        sc.nmsIouThreshold         = m_panel->nmsIouThreshold();
        m_kit->setStabilityConfig(sc);
    }

    // 确保模型已加载
    bool ensureModelLoaded()
    {
        if (m_kit->isModelLoaded()) {
            return true;
        }
        QMessageBox::warning(this, QString::fromUtf8("提示"),
                             QString::fromUtf8("请先在左侧面板加载模型"));
        return false;
    }

    // 启动推理（自动选择单张 / 批量异步接口）
    void runInference(const QStringList& imagePaths)
    {
        if (m_kit->isRunning()) {
            QMessageBox::information(this, QString::fromUtf8("提示"),
                                     QString::fromUtf8("当前已有推理任务运行中"));
            return;
        }
        // 推理前同步最新配置
        syncPanelToKit();

        m_resultPanel->clearResults();
        m_progressLabel->setText(QString::fromUtf8("推理中..."));

        // 单张走 inferAsync(imagePath)，多张走 inferBatchAsync
        if (imagePaths.size() == 1) {
            m_kit->inferAsync(imagePaths.first());
        } else {
            m_kit->inferBatchAsync(imagePaths);
        }
    }

    // 收集目录下所有图像文件（递归子目录）
    static QStringList collectImageFiles(const QString& dirPath)
    {
        const QStringList filters = { "*.jpg", "*.jpeg", "*.png", "*.bmp" };
        QStringList collected;
        QDirIterator it(dirPath, filters, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            collected << it.next();
        }
        collected.sort();
        return collected;
    }

private:
    // Kit 门面实例（由 this 作为 parent 管理生命周期）
    zsu::Kit*                   m_kit         = nullptr;
    // 左侧配置面板
    QDVMini::ZeroShotPanel*     m_panel       = nullptr;
    // 右侧结果面板
    QDVMini::ZeroShotResultPanel* m_resultPanel = nullptr;

    // 状态栏标签
    QLabel* m_modelLabel    = nullptr;
    QLabel* m_latencyLabel  = nullptr;
    QLabel* m_progressLabel = nullptr;

    // 当前选中的图像 / 目录
    QString m_currentImagePath;
    QString m_currentImageDir;
};

// ============================================================================
// 入口
// ============================================================================
int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QString::fromUtf8("zeroshot_with_ui"));

    MainWindow w;
    w.show();
    return app.exec();
}

// 由于 MainWindow 使用了 Q_OBJECT 宏且定义在 .cpp 文件中，
// 需要 include MOC 生成的元对象代码（AUTOMOC 开启时由 CMake 自动生成 main.moc）
#include "main.moc"
