// =====================================================================
// TrainingDialog.qml — 标训一键闭环 配置与进度对话框
//
// 串联：导出 YOLO 数据集(dataset.yaml) → 拉起 training/yolo_train.py
// 通过 session.exportAndTrain() 一键完成，并实时展示训练进度/日志/产物。
// =====================================================================
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QDVAnnotator 1.0 as Tok

Dialog {
    id: dlg
    title: "标训一键闭环（标注 → 训练）"
    modal: true
    width: 560; height: 620
    standardButtons: Dialog.Close

    property string outDir: ""
    property var lastResult: ({})

    function openFor() { lastResult = {}; logModel.clear(); progressMap = {}; dlg.open() }

    // 本地进度缓存
    property var progressMap: ({})

    ListModel { id: logModel }

    // ---- 监听训练信号 ----
    Connections {
        target: session
        function onTrainProgress(p) { dlg.progressMap = p; }
        function onTrainLog(m) { logModel.append({ t: m }); if (logModel.count > 500) logModel.remove(0) }
        function onTrainCompleted(r) { dlg.lastResult = r; }
        function onTrainError(phase, msg) { dlg.lastResult = { success: false, errorMessage: phase + ": " + msg } }
    }

    FolderDialog {
        id: dirDlg
        title: "选择输出目录（数据集与模型将写入此目录）"
        onAccepted: {
            outDir = selectedFolder.toString().replace("file:///", "")
            dirField.text = outDir
        }
    }

    // S1：可训练模型下拉由能力注册表 taskModels 驱动（obb + detection 两组）
    property var modelModel: []
    function buildModelModel() {
        var arr = []
        var obb = capabilities.taskModels("obb")
        for (var i = 0; i < obb.length; i++) arr.push({ text: obb[i] + " (旋转矩形)", val: obb[i] })
        var det = capabilities.taskModels("detection")
        for (var j = 0; j < det.length; j++) arr.push({ text: det[j] + " (轴对齐)", val: det[j] })
        return arr
    }
    Component.onCompleted: modelModel = buildModelModel()

    // 使用已导出的 OBB 数据集训练时，选择其 dataset.yaml
    FileDialog {
        id: yamlDlg
        title: "选择已导出的 dataset.yaml（yolo_obb 数据集）"
        nameFilters: ["YOLO 数据集配置 (*.yaml *.yml)"]
        onAccepted: yamlField.text = yamlDlg.selectedFile.toString().replace("file:///", "")
    }

    // 有向矩形(vec_rect)工程 → 标训闭环提示（轴对齐 YOLO 无法表达角度，引导改用 yolo_obb）
    Dialog {
        id: vecWarn
        title: "检测到有向矩形标注"
        modal: true
        width: 460
        standardButtons: Dialog.Close
        contentItem: ColumnLayout {
            spacing: 10
            Label {
                text: "当前工程为「有向矩形(vec_rect)」标注，含角度与根数。\n\n"
                      + "标训闭环固定使用轴对齐 YOLO(yolo_detect)，无法表达旋转角度，直接训练会产出空标签，已中止本次训练。\n\n"
                      + "请改用「导出」→「YOLO 有向矩形 OBB (yolo_obb)」格式导出数据集，再做有向矩形训练。"
                color: Tok.DesignTokens.textPrimary
                font.family: Tok.DesignTokens.fontFamilyCJK
                font.pixelSize: Tok.DesignTokens.fontSizeSm
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
        }
    }

    contentItem: ScrollView {
        contentWidth: availableWidth
        ColumnLayout {
            spacing: 10
            width: parent.width

            // ---- 数据集来源 ----
            Label { text: "数据集来源"; font.bold: true; font.family: Tok.DesignTokens.fontFamilyCJK; color: Tok.DesignTokens.textSecondary }
            RowLayout {
                Layout.fillWidth: true
                Tok.WideComboBox {
                    id: dataSrc
                    Layout.fillWidth: true
                    textRole: "text"; valueRole: "val"
                    model: [
                        { text: "自动导出当前工程（yolo_detect）", val: "auto" },
                        { text: "使用已导出的 OBB 数据集（yolo_obb）", val: "obb" }
                    ]
                    currentIndex: 0
                }
            }
            // 仅"使用已导出的 OBB 数据集"时显示 dataset.yaml 选择
            RowLayout {
                Layout.fillWidth: true
                visible: dataSrc.currentValue === "obb"
                Label { text: "dataset.yaml"; font.family: Tok.DesignTokens.fontFamilyCJK; color: Tok.DesignTokens.textPrimary }
                TextField {
                    id: yamlField
                    Layout.fillWidth: true
                    placeholderText: "选择 yolo_obb 导出目录下的 dataset.yaml"
                    color: Tok.DesignTokens.textPrimary
                    font.family: Tok.DesignTokens.fontFamilyCJK
                    background: Rectangle { color: Tok.DesignTokens.bgSurface; border.color: Tok.DesignTokens.borderDefault; radius: 2 }
                }
                Button { text: "浏览"; onClicked: yamlDlg.open() }
            }

            // ---- 输出目录 ----
            RowLayout {
                Label { text: "输出目录"; font.family: Tok.DesignTokens.fontFamilyCJK; color: Tok.DesignTokens.textPrimary }
                TextField {
                    id: dirField
                    Layout.fillWidth: true
                    placeholderText: "选择数据集/模型输出目录"
                    text: dlg.outDir
                }
                Button { text: "浏览"; onClicked: dirDlg.open() }
            }

            // ---- Python 解释器 ----
            RowLayout {
                Label { text: "Python"; font.family: Tok.DesignTokens.fontFamilyCJK; color: Tok.DesignTokens.textPrimary }
                TextField {
                    id: pyField
                    Layout.fillWidth: true
                    placeholderText: "需含 ultralytics / torch 的 Python 解释器路径"
                    // 优先用独立训练 venv（CUDA12.8 + PyTorch2.7.1 + ultralytics）
                    // 全局 D:/Program Files/Python314 的 torch import 会死锁，不可用于训练
                    text: "E:/anchor/Trae/QDV/training/venv/Scripts/python.exe"
                }
                Button {
                    text: "检测"
                    onClicked: {
                        // 真实验证 ultralytics + torch + CUDA（不是只跑 --version）
                        var msg = session.checkPythonEnv(pyField.text)
                        if (msg.ok) trainLogNote.text = "Python 环境可用: " + msg.detail
                        else trainLogNote.text = "Python 环境不可用: " + msg.detail
                    }
                }
            }
            Label { id: trainLogNote; color: Tok.DesignTokens.textTertiary; font.pixelSize: Tok.DesignTokens.fontSizeXs; font.family: Tok.DesignTokens.fontFamilyCJK }

            // ---- 训练超参 ----
            GridLayout {
                columns: 2; columnSpacing: 12; rowSpacing: 8
                Label { text: "模型"; font.family: Tok.DesignTokens.fontFamilyCJK; color: Tok.DesignTokens.textPrimary }
                Tok.WideComboBox {
                    id: modelCombo
                    textRole: "text"; valueRole: "val"
                    model: dlg.modelModel
                    currentIndex: 0
                }
                Label { text: "轮数"; font.family: Tok.DesignTokens.fontFamilyCJK; color: Tok.DesignTokens.textPrimary }
                SpinBox { id: epochSpin; from: 1; to: 1000; value: 50 }
                Label { text: "批大小"; font.family: Tok.DesignTokens.fontFamilyCJK; color: Tok.DesignTokens.textPrimary }
                // 默认 8：8GB 小显存卡跑默认 imgsz=640 时 batch16 易 OOM 而把软件闪退，
                // 训练脚本还会按实际显存/虚拟内存自动降档兜底。
                SpinBox { id: batchSpin; from: 1; to: 256; value: 8 }
                Label { text: "图像尺寸"; font.family: Tok.DesignTokens.fontFamilyCJK; color: Tok.DesignTokens.textPrimary }
                Tok.WideComboBox {
                    id: sizeCombo
                    textRole: "text"; valueRole: "val"
                    model: [
                        { text: "320", val: 320 },
                        { text: "416", val: 416 },
                        { text: "640", val: 640 }
                    ]
                    currentIndex: 2
                }
                Label { text: "学习率"; font.family: Tok.DesignTokens.fontFamilyCJK; color: Tok.DesignTokens.textPrimary }
                TextField { id: lrField; text: "0.01"; implicitWidth: 100 }
            }

            // ---- 动作 ----
            RowLayout {
                Button {
                    text: "开始标训闭环"
                    highlighted: true
                    enabled: !session.isTraining && dirField.text !== ""
                    onClicked: {
                        var opt = {
                            pythonPath: pyField.text,
                            modelType: modelCombo.currentValue,
                            numEpochs: epochSpin.value,
                            batchSize: batchSpin.value,
                            imageSize: sizeCombo.currentValue,
                            learningRate: parseFloat(lrField.text) || 0.01
                        }
                        if (dataSrc.currentValue === "obb") {
                            // 使用已导出的 yolo_obb 数据集：跳过导出，直接训练
                            if (yamlField.text === "") { trainLogNote.text = "请先选择已导出的 dataset.yaml"; return }
                            if (!session.trainFromDataYaml(yamlField.text, dirField.text, opt))
                                trainLogNote.text = "启动失败: " + session.lastError
                            return
                        }
                        // 自动导出当前工程（yolo_detect）：
                        // 有向矩形(vec_rect)工程：轴对齐 yolo_detect 无法表达角度，
                        // 即便调用也只会失败或产出空标签。提前明确提示并引导改用 yolo_obb。
                        if (session.hasVecRectAnnotations()) { vecWarn.open(); return }
                        if (session.labels.length === 0) { trainLogNote.text = "请先添加至少一个标签"; return }
                        if (!session.exportAndTrain(dirField.text, opt))
                            trainLogNote.text = "启动失败: " + session.lastError
                    }
                }
                Button {
                    text: "取消训练"
                    enabled: session.isTraining
                    onClicked: session.cancelTraining()
                }
            }

            // ---- 进度 ----
            Frame {
                Layout.fillWidth: true
                background: Rectangle { color: Tok.DesignTokens.bgSurface; radius: 4; border.color: Tok.DesignTokens.borderDefault }
                ColumnLayout {
                    spacing: 4
                    Label {
                        text: session.isTraining ? "训练中…" : "状态：空闲"
                        font.bold: true; font.family: Tok.DesignTokens.fontFamilyCJK
                        color: session.isTraining ? Tok.DesignTokens.accentPrimary : Tok.DesignTokens.textSecondary
                    }
                    Label {
                        text: (dlg.progressMap.epoch !== undefined)
                              ? ("Epoch " + dlg.progressMap.epoch + "/" + dlg.progressMap.totalEpochs
                                 + "  训练损失 " + dlg.progressMap.trainLoss.toFixed(4)
                                 + "  mAP@0.5 " + dlg.progressMap.valAccuracy.toFixed(4)
                                 + "  用时 " + dlg.progressMap.elapsedSeconds.toFixed(1) + "s")
                              : "等待训练开始…"
                        font.family: Tok.DesignTokens.fontFamilyCJK
                        color: Tok.DesignTokens.textPrimary
                        font.pixelSize: Tok.DesignTokens.fontSizeXs
                    }
                    ProgressBar {
                        Layout.fillWidth: true
                        value: (dlg.progressMap.totalEpochs > 0) ? dlg.progressMap.epoch / dlg.progressMap.totalEpochs : 0
                        visible: session.isTraining
                    }
                }
            }

            // ---- 结果 ----
            Label {
                visible: dlg.lastResult.success !== undefined
                text: dlg.lastResult.success
                      ? ("✅ 训练完成  mAP@0.5=" + (dlg.lastResult.map50 !== undefined ? dlg.lastResult.map50.toFixed(4) : "?")
                         + "  产物: " + dlg.lastResult.onnxPath)
                      : ("❌ 训练失败: " + (dlg.lastResult.errorMessage || dlg.lastResult.onnxPath || "未知错误"))
                color: dlg.lastResult.success ? Tok.DesignTokens.accentSuccess : Tok.DesignTokens.accentError
                font.family: Tok.DesignTokens.fontFamilyCJK
                font.pixelSize: Tok.DesignTokens.fontSizeXs
                wrapMode: Text.Wrap
            }

            // ---- 日志 ----
            Label { text: "训练日志"; font.bold: true; font.family: Tok.DesignTokens.fontFamilyCJK; color: Tok.DesignTokens.textSecondary }
            ScrollView {
                Layout.fillWidth: true
                Layout.preferredHeight: 140
                background: Rectangle { color: Tok.DesignTokens.logBg; radius: 4 }
                ListView {
                    model: logModel
                    clip: true
                    delegate: Text {
                        // ListModel 仅有角色 "t"，须经 model.t 访问；modelData 在角色模型中为 undefined
                        text: model.t || ""
                        color: Tok.DesignTokens.logFg
                        font.family: "Consolas, monospace"
                        font.pixelSize: 11
                        wrapMode: Text.Wrap
                    }
                }
            }
        }
    }
}
