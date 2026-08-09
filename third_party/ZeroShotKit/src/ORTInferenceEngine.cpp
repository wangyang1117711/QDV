// ============================================================================
// M1 核心交付：ONNX Runtime 推理引擎实现
//
// 完整实现 ONNX Runtime C++ API 的模型加载、推理、后处理。
// 替代主项目 InferenceEngine::runONNXRuntime() 的空壳实现。
// ============================================================================

#include "ORTInferenceEngine.h"
#include "ZeroShotKit/Logger.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <algorithm>
#include <cmath>

namespace zsu {

// ============================================================================
// 构造与析构
// ============================================================================

ORTInferenceEngine::ORTInferenceEngine(QObject* parent)
    : QObject(parent)
{
    // Logger 单例需在构造时设置 s_instance 防止递归（项目约束）
}

ORTInferenceEngine::~ORTInferenceEngine()
{
    unloadModel();
}

// ============================================================================
// 可用后端列表
// ============================================================================

QStringList ORTInferenceEngine::availableBackends() {
    QStringList backends;
    backends << "OpenCVDNN";
#ifdef ZSU_HAS_ORT
    backends << "ONNXRuntime";
#endif
    return backends;
}

// ============================================================================
// 错误重置
// ============================================================================

void ORTInferenceEngine::resetError() {
    m_errorState = ErrorState::NoError;
    m_lastError.clear();
}

// ============================================================================
// 模型加载
// ============================================================================

bool ORTInferenceEngine::loadModel(const QString& modelPath,
                                    const QSize& inputSize,
                                    const cv::Scalar& mean,
                                    double scale,
                                    bool swapRB,
                                    bool skipForwardTest) {
    if (m_modelLoaded) {
        unloadModel();
    }

    resetError();
    resetCancelFlag();

    // --- 文件校验 ---
    QFileInfo fileInfo(modelPath);
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        m_errorState = ErrorState::ModelNotFound;
        m_lastError = QString::fromUtf8("模型文件不存在: %1").arg(modelPath);
        ZSU_LOG_WARN("ORTInferenceEngine: " + m_lastError);
        emit modelLoaded(false);
        return false;
    }

    if (!fileInfo.isReadable()) {
        m_errorState = ErrorState::ModelNotFound;
        m_lastError = QString::fromUtf8("模型文件无法读取: %1").arg(modelPath);
        ZSU_LOG_ERROR("ORTInferenceEngine: " + m_lastError);
        emit modelLoaded(false);
        return false;
    }

    qint64 minFileSize = 1024;
    if (fileInfo.size() < minFileSize) {
        m_errorState = ErrorState::ModelLoadFailed;
        m_lastError = QString::fromUtf8("模型文件过小（%1字节），可能损坏: %2")
            .arg(fileInfo.size()).arg(modelPath);
        ZSU_LOG_ERROR("ORTInferenceEngine: " + m_lastError);
        emit modelLoaded(false);
        return false;
    }

    m_inputSize = inputSize;
    m_mean = mean;
    m_scale = scale;
    m_swapRB = swapRB;
    m_modelPath = modelPath;

    try {
        if (m_backend == Backend::ONNXRuntime) {
#ifdef ZSU_HAS_ORT
            ZSU_LOG_INFO("ORTInferenceEngine: 初始化 ONNX Runtime 后端...");
            if (!initORTSession(modelPath)) {
                m_errorState = ErrorState::ModelLoadFailed;
                m_lastError = QString::fromUtf8("ONNX Runtime 会话初始化失败: %1").arg(modelPath);
                ZSU_LOG_ERROR("ORTInferenceEngine: " + m_lastError);
                emit modelLoaded(false);
                return false;
            }

            // 前向测试（可选）
            if (!skipForwardTest) {
                emit stageProgress(QString::fromUtf8("ORT 模型前向测试中..."), 0);
                cv::Mat testInput(m_inputSize.height(), m_inputSize.width(), CV_8UC3, cv::Scalar(128));
                cv::Mat testBlob = cv::dnn::blobFromImage(testInput, m_scale,
                    cv::Size(m_inputSize.width(), m_inputSize.height()),
                    m_mean, m_swapRB, false);
                cv::Mat testOutput;
                if (!runONNXRuntime(testBlob, testOutput) || testOutput.empty()) {
                    m_errorState = ErrorState::ModelForwardTestFailed;
                    m_lastError = QString::fromUtf8("ORT 前向测试失败: %1").arg(modelPath);
                    ZSU_LOG_ERROR("ORTInferenceEngine: " + m_lastError);
                    emit modelLoaded(false);
                    return false;
                }
                ZSU_LOG_INFO("ORTInferenceEngine: 前向测试通过");
                emit stageProgress(QString::fromUtf8("ORT 前向测试通过"), 0);
            } else {
                ZSU_LOG_INFO("ORTInferenceEngine: 跳过前向测试");
                emit stageProgress(QString::fromUtf8("ORT 模型已加载（跳过前向测试）"), 0);
            }
#else
            m_errorState = ErrorState::ModelLoadFailed;
            m_lastError = QString::fromUtf8("ONNX Runtime 未编译（ZSU_HAS_ORT 未定义）");
            ZSU_LOG_ERROR("ORTInferenceEngine: " + m_lastError);
            emit modelLoaded(false);
            return false;
#endif
        } else {
            // --- OpenCV DNN 后端（用于对比基准） ---
            ZSU_LOG_INFO("ORTInferenceEngine: 使用 OpenCV DNN 后端");

            QFile modelFile(modelPath);
            if (!modelFile.open(QIODevice::ReadOnly)) {
                m_errorState = ErrorState::ModelLoadFailed;
                m_lastError = QString::fromUtf8("无法打开模型文件: %1").arg(modelPath);
                ZSU_LOG_ERROR("ORTInferenceEngine: " + m_lastError);
                emit modelLoaded(false);
                return false;
            }
            QByteArray modelData = modelFile.readAll();
            modelFile.close();
            if (modelData.isEmpty()) {
                m_errorState = ErrorState::ModelLoadFailed;
                m_lastError = QString::fromUtf8("模型文件为空: %1").arg(modelPath);
                ZSU_LOG_ERROR("ORTInferenceEngine: " + m_lastError);
                emit modelLoaded(false);
                return false;
            }

            m_net = cv::dnn::readNetFromONNX(modelData.constData(), static_cast<size_t>(modelData.size()));
            if (m_net.empty()) {
                m_errorState = ErrorState::ModelLoadFailed;
                m_lastError = QString::fromUtf8("OpenCV DNN 解析 ONNX 失败: %1").arg(modelPath);
                ZSU_LOG_ERROR("ORTInferenceEngine: " + m_lastError);
                emit modelLoaded(false);
                return false;
            }
            m_net.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
            m_net.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);

            if (!skipForwardTest) {
                emit stageProgress(QString::fromUtf8("OpenCV DNN 前向测试中..."), 0);
                cv::Mat testInput(m_inputSize.height(), m_inputSize.width(), CV_8UC3, cv::Scalar(128));
                cv::Mat testBlob = cv::dnn::blobFromImage(testInput, m_scale,
                    cv::Size(m_inputSize.width(), m_inputSize.height()),
                    m_mean, m_swapRB, false);
                m_net.setInput(testBlob);
                cv::Mat testOutput = m_net.forward();
                if (testOutput.empty()) {
                    m_errorState = ErrorState::ModelForwardTestFailed;
                    m_lastError = QString::fromUtf8("OpenCV DNN 前向测试返回空输出");
                    ZSU_LOG_ERROR("ORTInferenceEngine: " + m_lastError);
                    emit modelLoaded(false);
                    return false;
                }
                emit stageProgress(QString::fromUtf8("OpenCV DNN 前向测试通过"), 0);
            }
        }
    } catch (const std::exception& e) {
        m_errorState = ErrorState::ModelLoadFailed;
        m_lastError = QString::fromUtf8("模型加载异常: %1").arg(e.what());
        ZSU_LOG_ERROR("ORTInferenceEngine: " + m_lastError);
        m_net = cv::dnn::Net();
        emit modelLoaded(false);
        return false;
    } catch (...) {
        m_errorState = ErrorState::FatalError;
        m_lastError = QString::fromUtf8("模型加载发生未知严重异常");
        ZSU_LOG_ERROR("ORTInferenceEngine: " + m_lastError);
        m_net = cv::dnn::Net();
        emit modelLoaded(false);
        return false;
    }

    m_modelLoaded = true;
    m_inputSpec.recommendedSize = m_inputSize;
    ZSU_LOG_INFO(QString("ORTInferenceEngine: 模型加载成功 backend=%1 path=%2")
        .arg(availableBackends().value(static_cast<int>(m_backend)))
        .arg(modelPath));
    emit modelLoaded(true);
    return true;
}

// ============================================================================
// 卸载模型
// ============================================================================

bool ORTInferenceEngine::unloadModel() {
#ifdef ZSU_HAS_ORT
    m_ortSession.reset();
    m_ortEnv.reset();
    m_ortMemoryInfo.reset();
    m_inputNames.clear();
    m_outputNames.clear();
    m_inputNamePtrs.clear();
    m_outputNamePtrs.clear();
    m_inputShapes.clear();
    m_outputShapes.clear();
#endif
    m_net = cv::dnn::Net();
    m_modelLoaded = false;
    m_modelPath.clear();
    ZSU_LOG_INFO("ORTInferenceEngine: 模型已卸载");
    return true;
}

// ============================================================================
// ORT 会话初始化
// ============================================================================

#ifdef ZSU_HAS_ORT
bool ORTInferenceEngine::initORTSession(const QString& modelPath) {
    try {
        // 1. 创建环境
        m_ortEnv = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "ZeroShotUpgrade");

        // 2. 配置会话选项
        Ort::SessionOptions sessionOpts;

        // 线程配置
        sessionOpts.SetIntraOpNumThreads(m_ortConfig.intraOpNumThreads);
        sessionOpts.SetInterOpNumThreads(m_ortConfig.interOpNumThreads);

        // 执行模式
        if (m_ortConfig.executionMode == "PARALLEL") {
            sessionOpts.SetExecutionMode(ExecutionMode::ORT_PARALLEL);
        } else {
            sessionOpts.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
        }

        // 图优化级别
        if (m_ortConfig.optimizationLevel == "ALL") {
            sessionOpts.SetGraphOptimizationLevel(
                GraphOptimizationLevel::ORT_ENABLE_ALL);
        } else if (m_ortConfig.optimizationLevel == "EXTENDED") {
            sessionOpts.SetGraphOptimizationLevel(
                GraphOptimizationLevel::ORT_ENABLE_EXTENDED);
        } else if (m_ortConfig.optimizationLevel == "BASIC") {
            sessionOpts.SetGraphOptimizationLevel(
                GraphOptimizationLevel::ORT_ENABLE_BASIC);
        } else {
            sessionOpts.SetGraphOptimizationLevel(
                GraphOptimizationLevel::ORT_DISABLE_ALL);
        }

        // 内存优化
        if (m_ortConfig.enableMemPattern) {
            sessionOpts.EnableMemPattern();
        }
        if (m_ortConfig.enableCpuMemArena) {
            sessionOpts.EnableCpuMemArena();
        }

        // 3. 创建会话（使用宽字符路径支持中文）
        // Windows 下 ORT 需要 wchar_t 路径
        std::wstring wPath = modelPath.toStdWString();
        m_ortSession = std::make_unique<Ort::Session>(*m_ortEnv, wPath.c_str(), sessionOpts);

        // 4. 内存信息
        m_ortMemoryInfo = std::make_unique<Ort::MemoryInfo>(
            Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault));

        // 5. 获取输入/输出名称和形状
        Ort::AllocatorWithDefaultOptions allocator;

        // 输入
        size_t numInputs = m_ortSession->GetInputCount();
        m_inputNames.clear();
        m_inputNamePtrs.clear();
        m_inputShapes.clear();
        for (size_t i = 0; i < numInputs; ++i) {
            auto nameAlloc = m_ortSession->GetInputNameAllocated(i, allocator);
            m_inputNames.push_back(nameAlloc.get());
            m_inputShapes.push_back(m_ortSession->GetInputTypeInfo(i)
                .GetTensorTypeAndShapeInfo().GetShape());
            ZSU_LOG_INFO(QString("ORTInferenceEngine: 输入[%1] name=%2 shape=%3")
                .arg(i).arg(QString::fromStdString(m_inputNames.back()))
                .arg([&]() {
                    QString s = "[";
                    for (size_t j = 0; j < m_inputShapes.back().size(); ++j) {
                        if (j > 0) s += ",";
                        s += QString::number(m_inputShapes.back()[j]);
                    }
                    s += "]";
                    return s;
                }()));
        }
        for (auto& n : m_inputNames) {
            m_inputNamePtrs.push_back(n.c_str());
        }

        // 输出
        size_t numOutputs = m_ortSession->GetOutputCount();
        m_outputNames.clear();
        m_outputNamePtrs.clear();
        m_outputShapes.clear();
        for (size_t i = 0; i < numOutputs; ++i) {
            auto nameAlloc = m_ortSession->GetOutputNameAllocated(i, allocator);
            m_outputNames.push_back(nameAlloc.get());
            m_outputShapes.push_back(m_ortSession->GetOutputTypeInfo(i)
                .GetTensorTypeAndShapeInfo().GetShape());
            ZSU_LOG_INFO(QString("ORTInferenceEngine: 输出[%1] name=%2")
                .arg(i).arg(QString::fromStdString(m_outputNames.back())));
        }
        for (auto& n : m_outputNames) {
            m_outputNamePtrs.push_back(n.c_str());
        }

        ZSU_LOG_INFO(QString("ORTInferenceEngine: ORT 会话创建成功 inputs=%1 outputs=%2")
            .arg(numInputs).arg(numOutputs));
        return true;

    } catch (const Ort::Exception& e) {
        m_lastError = QString::fromUtf8("ORT 异常: %1").arg(e.what());
        ZSU_LOG_ERROR("ORTInferenceEngine: " + m_lastError);
        return false;
    } catch (const std::exception& e) {
        m_lastError = QString::fromUtf8("ORT 初始化异常: %1").arg(e.what());
        ZSU_LOG_ERROR("ORTInferenceEngine: " + m_lastError);
        return false;
    }
}

// ============================================================================
// 创建输入张量
// ============================================================================

Ort::Value ORTInferenceEngine::createInputTensor(const cv::Mat& blob) {
    // blob 维度: [1, C, H, W], 类型 CV_32F
    // ORT 需要 float32 连续内存
    CV_Assert(blob.type() == CV_32F);

    // 获取 blob 形状
    int c = blob.size[1];
    int h = blob.size[2];
    int w = blob.size[3];

    std::vector<int64_t> inputShape = {1, c, h, w};

    // 计算总元素数
    size_t totalElements = static_cast<size_t>(c) * h * w;

    // 创建 ORT 张量（不拷贝数据，使用 blob 的内存）
    // 注意：blob 必须在 Run() 完成前保持有效
    Ort::Value inputTensor = Ort::Value::CreateTensor<float>(
        *m_ortMemoryInfo,
        reinterpret_cast<float*>(blob.data),
        totalElements,
        inputShape.data(),
        inputShape.size()
    );

    return inputTensor;
}
#endif // ZSU_HAS_ORT

// ============================================================================
// 预处理
// ============================================================================

cv::Mat ORTInferenceEngine::preprocess(const cv::Mat& input) {
    QElapsedTimer timer;
    timer.start();

    if (input.empty()) {
        m_errorState = ErrorState::PreprocessFailed;
        m_lastError = QString::fromUtf8("预处理输入图片为空");
        ZSU_LOG_WARN("ORTInferenceEngine: " + m_lastError);
        return cv::Mat();
    }

    cv::Mat resized;
    try {
        cv::resize(input, resized, cv::Size(m_inputSize.width(), m_inputSize.height()));
    } catch (const cv::Exception& e) {
        m_errorState = ErrorState::PreprocessFailed;
        m_lastError = QString::fromUtf8("图片缩放失败: %1").arg(e.what());
        ZSU_LOG_ERROR("ORTInferenceEngine: " + m_lastError);
        return cv::Mat();
    }

    cv::Mat blob;
    try {
        // 仅依赖 blobFromImage 的 swapRB 做一次 R<->B 交换
        // 修复主项目 P0-3：避免 cvtColor + swapRB 双重交换
        if (input.channels() == 3) {
            cv::Size cvSize(m_inputSize.width(), m_inputSize.height());
            blob = cv::dnn::blobFromImage(resized, m_scale, cvSize, m_mean, m_swapRB, false);
        } else {
            std::vector<cv::Mat> channels = {resized};
            blob = cv::dnn::blobFromImages(channels);
        }
    } catch (const cv::Exception& e) {
        m_errorState = ErrorState::PreprocessFailed;
        m_lastError = QString::fromUtf8("Blob 创建失败: %1").arg(e.what());
        ZSU_LOG_ERROR("ORTInferenceEngine: " + m_lastError);
        return cv::Mat();
    }

    m_lastMetrics.preprocessMs = timer.elapsed();
    return blob;
}

// ============================================================================
// OpenCV DNN 后端推理（对比基准用）
// ============================================================================

bool ORTInferenceEngine::runOpenCVDNN(const cv::Mat& blob, cv::Mat& output) {
    QElapsedTimer timer;
    timer.start();

    try {
        m_net.setInput(blob);
        output = m_net.forward();
        m_lastMetrics.inferenceMs = timer.elapsed();
        return !output.empty();
    } catch (const cv::Exception& e) {
        m_errorState = ErrorState::InferenceFailed;
        m_lastError = QString::fromUtf8("OpenCV DNN 推理失败: %1").arg(e.what());
        ZSU_LOG_ERROR("ORTInferenceEngine: " + m_lastError);
        return false;
    }
}

// ============================================================================
// ONNX Runtime 后端推理（M1 核心）
// ============================================================================

bool ORTInferenceEngine::runONNXRuntime(const cv::Mat& blob, cv::Mat& output) {
#ifdef ZSU_HAS_ORT
    if (!m_ortSession) {
        m_errorState = ErrorState::InferenceFailed;
        m_lastError = QString::fromUtf8("ORT 会话未初始化");
        ZSU_LOG_ERROR("ORTInferenceEngine: " + m_lastError);
        return false;
    }

    QElapsedTimer timer;
    timer.start();

    try {
        // 1. 创建输入张量
        Ort::Value inputTensor = createInputTensor(blob);

        // 2. 执行推理
        auto outputTensors = m_ortSession->Run(
            Ort::RunOptions{nullptr},
            m_inputNamePtrs.data(),
            &inputTensor,
            1,                          // 输入数量
            m_outputNamePtrs.data(),
            m_outputNamePtrs.size()     // 输出数量
        );

        m_lastMetrics.inferenceMs = timer.elapsed();

        if (outputTensors.empty()) {
            m_errorState = ErrorState::InferenceFailed;
            m_lastError = QString::fromUtf8("ORT 推理返回空输出");
            ZSU_LOG_ERROR("ORTInferenceEngine: " + m_lastError);
            return false;
        }

        // 3. 将第一个输出张量转为 cv::Mat
        auto& firstOutput = outputTensors[0];
        auto typeInfo = firstOutput.GetTensorTypeAndShapeInfo();
        auto shape = typeInfo.GetShape();

        // 确定 cv::Mat 维度和形状
        std::vector<int> matShape;
        for (auto dim : shape) {
            matShape.push_back(static_cast<int>(dim));
        }

        // 创建 cv::Mat（共享 ORT 张量内存，需 clone 以独立持有）
        // ORT 张量数据为 float32
        cv::Mat mat;
        if (matShape.size() == 1) {
            mat = cv::Mat(matShape[0], 1, CV_32F, firstOutput.GetTensorMutableData<float>());
        } else if (matShape.size() == 2) {
            mat = cv::Mat(matShape[0], matShape[1], CV_32F, firstOutput.GetTensorMutableData<float>());
        } else if (matShape.size() == 3) {
            mat = cv::Mat(static_cast<int>(matShape.size()), matShape.data(), CV_32F,
                         firstOutput.GetTensorMutableData<float>());
        } else if (matShape.size() == 4) {
            mat = cv::Mat(static_cast<int>(matShape.size()), matShape.data(), CV_32F,
                         firstOutput.GetTensorMutableData<float>());
        } else {
            m_errorState = ErrorState::PostprocessFailed;
            m_lastError = QString::fromUtf8("不支持的输出维度: %1").arg(matShape.size());
            ZSU_LOG_ERROR("ORTInferenceEngine: " + m_lastError);
            return false;
        }

        output = mat.clone();   // 独立拷贝（脱离 ORT 张量生命周期）
        return true;

    } catch (const Ort::Exception& e) {
        m_errorState = ErrorState::InferenceFailed;
        m_lastError = QString::fromUtf8("ORT 推理异常: %1").arg(e.what());
        ZSU_LOG_ERROR("ORTInferenceEngine: " + m_lastError);
        return false;
    } catch (const std::exception& e) {
        m_errorState = ErrorState::InferenceFailed;
        m_lastError = QString::fromUtf8("推理异常: %1").arg(e.what());
        ZSU_LOG_ERROR("ORTInferenceEngine: " + m_lastError);
        return false;
    }
#else
    Q_UNUSED(blob)
    Q_UNUSED(output)
    m_errorState = ErrorState::InferenceFailed;
    m_lastError = QString::fromUtf8("ONNX Runtime 未编译");
    ZSU_LOG_ERROR("ORTInferenceEngine: " + m_lastError);
    return false;
#endif
}

// ============================================================================
// ONNX Runtime 多输入多输出推理（M6: MobileSAM 解码器）
// inputBlobs 顺序与模型输入声明顺序一致，各 blob 为连续 CV_32F
// ============================================================================

bool ORTInferenceEngine::runMultiONNXRuntime(const std::vector<cv::Mat>& inputBlobs,
                                             std::vector<cv::Mat>& outputBlobs) {
#ifdef ZSU_HAS_ORT
    if (!m_ortSession) {
        m_errorState = ErrorState::InferenceFailed;
        m_lastError = QString::fromUtf8("ORT 会话未初始化");
        ZSU_LOG_ERROR("ORTInferenceEngine: " + m_lastError);
        return false;
    }

    const size_t numInputs = m_inputNamePtrs.size();
    if (inputBlobs.size() != numInputs) {
        m_errorState = ErrorState::InferenceFailed;
        m_lastError = QString::fromUtf8("多输入推理输入数量不匹配: 期望 %1，实际 %2")
            .arg(numInputs).arg(inputBlobs.size());
        ZSU_LOG_ERROR("ORTInferenceEngine: " + m_lastError);
        return false;
    }

    QElapsedTimer timer;
    timer.start();

    try {
        // 1. 为每个输入创建 ORT 张量（按模型输入声明顺序）
        std::vector<Ort::Value> inputTensors;
        inputTensors.reserve(numInputs);
        for (size_t i = 0; i < numInputs; ++i) {
            const cv::Mat& blob = inputBlobs[i];
            CV_Assert(blob.type() == CV_32F && blob.isContinuous());

            // 使用模型声明的输入 shape（保证与 ORT 期望一致，如 has_mask_input=[1]）
            // 若 shape 含动态维度(-1)，回退用 blob 实际维度
            std::vector<int64_t> shape = m_inputShapes[i];
            bool hasDynamic = false;
            for (int64_t d : shape) if (d <= 0) { hasDynamic = true; break; }
            if (hasDynamic || shape.empty()) {
                shape.clear();
                if (blob.dims >= 2) {
                    for (int d = 0; d < blob.dims; ++d) shape.push_back(blob.size[d]);
                } else {
                    shape.push_back(blob.rows);
                    shape.push_back(blob.cols);
                }
            }

            inputTensors.push_back(Ort::Value::CreateTensor<float>(
                *m_ortMemoryInfo,
                reinterpret_cast<float*>(blob.data),
                blob.total(),
                shape.data(),
                shape.size()));
        }

        // 2. 执行推理
        auto outputTensors = m_ortSession->Run(
            Ort::RunOptions{nullptr},
            m_inputNamePtrs.data(),
            inputTensors.data(),
            m_inputNamePtrs.size(),
            m_outputNamePtrs.data(),
            m_outputNamePtrs.size());

        m_lastMetrics.inferenceMs = timer.elapsed();

        if (outputTensors.empty()) {
            m_errorState = ErrorState::InferenceFailed;
            m_lastError = QString::fromUtf8("ORT 多输入推理返回空输出");
            ZSU_LOG_ERROR("ORTInferenceEngine: " + m_lastError);
            return false;
        }

        // 3. 将所有输出张量转为 cv::Mat
        outputBlobs.clear();
        for (auto& out : outputTensors) {
            auto typeInfo = out.GetTensorTypeAndShapeInfo();
            auto shape = typeInfo.GetShape();

            std::vector<int> matShape;
            for (auto dim : shape) matShape.push_back(static_cast<int>(dim));

            cv::Mat mat;
            if (matShape.size() == 1) {
                mat = cv::Mat(matShape[0], 1, CV_32F, out.GetTensorMutableData<float>());
            } else if (matShape.size() == 2) {
                mat = cv::Mat(matShape[0], matShape[1], CV_32F, out.GetTensorMutableData<float>());
            } else if (matShape.size() == 3 || matShape.size() == 4) {
                mat = cv::Mat(static_cast<int>(matShape.size()), matShape.data(), CV_32F,
                              out.GetTensorMutableData<float>());
            } else {
                m_errorState = ErrorState::PostprocessFailed;
                m_lastError = QString::fromUtf8("多输入推理输出维度不支持: %1").arg(matShape.size());
                ZSU_LOG_ERROR("ORTInferenceEngine: " + m_lastError);
                return false;
            }
            outputBlobs.push_back(mat.clone());  // 独立拷贝，脱离 ORT 张量生命周期
        }
        return true;

    } catch (const Ort::Exception& e) {
        m_errorState = ErrorState::InferenceFailed;
        m_lastError = QString::fromUtf8("ORT 多输入推理异常: %1").arg(e.what());
        ZSU_LOG_ERROR("ORTInferenceEngine: " + m_lastError);
        return false;
    } catch (const std::exception& e) {
        m_errorState = ErrorState::InferenceFailed;
        m_lastError = QString::fromUtf8("多输入推理异常: %1").arg(e.what());
        ZSU_LOG_ERROR("ORTInferenceEngine: " + m_lastError);
        return false;
    }
#else
    Q_UNUSED(inputBlobs)
    Q_UNUSED(outputBlobs)
    m_errorState = ErrorState::InferenceFailed;
    m_lastError = QString::fromUtf8("ONNX Runtime 未编译");
    ZSU_LOG_ERROR("ORTInferenceEngine: " + m_lastError);
    return false;
#endif
}

// ============================================================================
// 多输入多输出推理入口（跳过预处理）
// ============================================================================

bool ORTInferenceEngine::inferMultiRaw(const std::vector<cv::Mat>& inputBlobs,
                                       std::vector<cv::Mat>& outputBlobs,
                                       QJsonObject& result) {
    if (!m_modelLoaded) {
        m_errorState = ErrorState::ModelLoadFailed;
        m_lastError = QString::fromUtf8("未加载模型");
        result["status"] = "no_model";
        result["error"] = m_lastError;
        return false;
    }

    for (const cv::Mat& blob : inputBlobs) {
        if (blob.empty()) {
            m_errorState = ErrorState::PreprocessFailed;
            m_lastError = QString::fromUtf8("多输入存在空 blob");
            result["status"] = "empty_input";
            result["error"] = m_lastError;
            return false;
        }
    }

    resetError();

    QElapsedTimer totalTimer;
    totalTimer.start();

    try {
        m_lastMetrics.preprocessMs = 0;

        bool ok = false;
        if (m_backend == Backend::ONNXRuntime) {
            ok = runMultiONNXRuntime(inputBlobs, outputBlobs);
        } else {
            m_errorState = ErrorState::InferenceFailed;
            m_lastError = QString::fromUtf8("多输入推理仅支持 ONNX Runtime 后端");
            ZSU_LOG_ERROR("ORTInferenceEngine: " + m_lastError);
            result["status"] = "inference_failed";
            result["error"] = m_lastError;
            emit inferenceCompleted(false);
            return false;
        }

        if (!ok || outputBlobs.empty()) {
            result["status"] = "inference_failed";
            result["error"] = m_lastError;
            result["latency_ms"] = totalTimer.elapsed();
            emit inferenceCompleted(false);
            return false;
        }

        m_lastMetrics.postprocessMs = 0;
        m_lastMetrics.totalMs = totalTimer.elapsed();

        result["status"] = "success";
        result["latency_ms"] = m_lastMetrics.totalMs;
        result["inference_ms"] = m_lastMetrics.inferenceMs;
        result["backend"] = availableBackends().value(static_cast<int>(m_backend));

        emit inferenceCompleted(true);
        return true;

    } catch (const std::exception& e) {
        m_errorState = ErrorState::UnexpectedError;
        m_lastError = QString::fromUtf8("多输入推理异常: %1").arg(e.what());
        ZSU_LOG_ERROR("ORTInferenceEngine: " + m_lastError);
        result["status"] = "unexpected_error";
        result["error"] = m_lastError;
        result["latency_ms"] = totalTimer.elapsed();
        emit inferenceCompleted(false);
        return false;
    } catch (...) {
        m_errorState = ErrorState::FatalError;
        m_lastError = QString::fromUtf8("多输入推理发生严重错误");
        ZSU_LOG_ERROR("ORTInferenceEngine: " + m_lastError);
        result["status"] = "fatal_error";
        result["error"] = m_lastError;
        result["latency_ms"] = totalTimer.elapsed();
        emit inferenceCompleted(false);
        return false;
    }
}

// ============================================================================
// 推理入口
// ============================================================================

bool ORTInferenceEngine::infer(const cv::Mat& input, cv::Mat& rawOutput, QJsonObject& result) {
    if (!m_modelLoaded) {
        m_errorState = ErrorState::ModelLoadFailed;
        m_lastError = QString::fromUtf8("未加载模型");
        result["status"] = "no_model";
        result["error"] = m_lastError;
        return false;
    }

    if (input.empty()) {
        m_errorState = ErrorState::PreprocessFailed;
        m_lastError = QString::fromUtf8("输入图片为空");
        result["status"] = "empty_input";
        result["error"] = m_lastError;
        return false;
    }

    if (isCancelled()) {
        m_errorState = ErrorState::Cancelled;
        m_lastError = QString::fromUtf8("推理已被用户取消");
        result["status"] = "cancelled";
        result["error"] = m_lastError;
        emit inferenceCompleted(false);
        return false;
    }

    resetError();

    QElapsedTimer totalTimer;
    totalTimer.start();

    try {
        // 1. 预处理
        emit stageProgress(QString::fromUtf8("预处理中"), 0);
        cv::Mat blob = preprocess(input);
        if (blob.empty()) {
            result["status"] = "preprocess_failed";
            result["error"] = m_lastError;
            return false;
        }

        if (isCancelled()) {
            m_errorState = ErrorState::Cancelled;
            m_lastError = QString::fromUtf8("推理已被用户取消");
            result["status"] = "cancelled";
            result["error"] = m_lastError;
            emit inferenceCompleted(false);
            return false;
        }

        // 2. 推理
        emit stageProgress(QString::fromUtf8("模型推理中"), m_lastMetrics.preprocessMs);
        bool ok = false;
        if (m_backend == Backend::ONNXRuntime) {
            ok = runONNXRuntime(blob, rawOutput);
        } else {
            ok = runOpenCVDNN(blob, rawOutput);
        }

        if (isCancelled()) {
            m_errorState = ErrorState::Cancelled;
            m_lastError = QString::fromUtf8("推理已被用户取消");
            result["status"] = "cancelled";
            result["error"] = m_lastError;
            emit inferenceCompleted(false);
            return false;
        }

        if (!ok || rawOutput.empty()) {
            result["status"] = "inference_failed";
            result["error"] = m_lastError;
            result["latency_ms"] = totalTimer.elapsed();
            emit inferenceCompleted(false);
            return false;
        }

        // 3. 后处理
        emit stageProgress(QString::fromUtf8("后处理中"), m_lastMetrics.inferenceMs);

        // 输出形状诊断日志
        QString shapeStr;
        if (rawOutput.dims == 1) shapeStr = QString("[%1]").arg(rawOutput.size[0]);
        else if (rawOutput.dims == 2) shapeStr = QString("[%1,%2]").arg(rawOutput.size[0]).arg(rawOutput.size[1]);
        else if (rawOutput.dims == 3) shapeStr = QString("[%1,%2,%3]").arg(rawOutput.size[0]).arg(rawOutput.size[1]).arg(rawOutput.size[2]);
        else if (rawOutput.dims == 4) shapeStr = QString("[%1,%2,%3,%4]").arg(rawOutput.size[0]).arg(rawOutput.size[1]).arg(rawOutput.size[2]).arg(rawOutput.size[3]);
        else shapeStr = QString("dims=%1").arg(rawOutput.dims);
        ZSU_LOG_INFO(QString("ORTInferenceEngine: 输出 shape=%1 type=%2")
            .arg(shapeStr)
            .arg(rawOutput.type() == CV_32F ? "F32" : QString::number(rawOutput.type())));

        result = postprocess(rawOutput, blob);

        m_lastMetrics.totalMs = totalTimer.elapsed();
        result["latency_ms"] = m_lastMetrics.totalMs;
        result["preprocess_ms"] = m_lastMetrics.preprocessMs;
        result["inference_ms"] = m_lastMetrics.inferenceMs;
        result["postprocess_ms"] = m_lastMetrics.postprocessMs;
        result["backend"] = availableBackends().value(static_cast<int>(m_backend));
        result["status"] = "success";

        emit inferenceCompleted(true);
        return true;

    } catch (const std::exception& e) {
        m_errorState = ErrorState::UnexpectedError;
        m_lastError = QString::fromUtf8("推理异常: %1").arg(e.what());
        ZSU_LOG_ERROR("ORTInferenceEngine: " + m_lastError);
        result["status"] = "unexpected_error";
        result["error"] = m_lastError;
        result["latency_ms"] = totalTimer.elapsed();
        emit inferenceCompleted(false);
        return false;
    } catch (...) {
        m_errorState = ErrorState::FatalError;
        m_lastError = QString::fromUtf8("推理发生严重错误");
        ZSU_LOG_ERROR("ORTInferenceEngine: " + m_lastError);
        result["status"] = "fatal_error";
        result["error"] = m_lastError;
        result["latency_ms"] = totalTimer.elapsed();
        emit inferenceCompleted(false);
        return false;
    }
}

bool ORTInferenceEngine::infer(const cv::Mat& input, QJsonObject& result) {
    cv::Mat output;
    return infer(input, output, result);
}

// ============================================================================
// 跳过预处理的推理（输入已是 NCHW float32 blob）
// 适用于 ZeroShotEngine 等已自定义预处理的场景，直接执行模型前向 + 返回原始输出
// ============================================================================

bool ORTInferenceEngine::inferRaw(const cv::Mat& preprocessedBlob, cv::Mat& rawOutput, QJsonObject& result) {
    if (!m_modelLoaded) {
        m_errorState = ErrorState::ModelLoadFailed;
        m_lastError = QString::fromUtf8("未加载模型");
        result["status"] = "no_model";
        result["error"] = m_lastError;
        return false;
    }

    if (preprocessedBlob.empty()) {
        m_errorState = ErrorState::PreprocessFailed;
        m_lastError = QString::fromUtf8("输入 blob 为空");
        result["status"] = "empty_input";
        result["error"] = m_lastError;
        return false;
    }

    resetError();

    QElapsedTimer totalTimer;
    totalTimer.start();

    try {
        // 直接执行推理（跳过 preprocess）
        m_lastMetrics.preprocessMs = 0;

        bool ok = false;
        if (m_backend == Backend::ONNXRuntime) {
            ok = runONNXRuntime(preprocessedBlob, rawOutput);
        } else {
            ok = runOpenCVDNN(preprocessedBlob, rawOutput);
        }

        if (!ok || rawOutput.empty()) {
            result["status"] = "inference_failed";
            result["error"] = m_lastError;
            result["latency_ms"] = totalTimer.elapsed();
            emit inferenceCompleted(false);
            return false;
        }

        // 跳过 postprocess，直接返回原始输出
        m_lastMetrics.postprocessMs = 0;
        m_lastMetrics.totalMs = totalTimer.elapsed();

        result["status"] = "success";
        result["latency_ms"] = m_lastMetrics.totalMs;
        result["inference_ms"] = m_lastMetrics.inferenceMs;
        result["backend"] = availableBackends().value(static_cast<int>(m_backend));

        emit inferenceCompleted(true);
        return true;

    } catch (const std::exception& e) {
        m_errorState = ErrorState::UnexpectedError;
        m_lastError = QString::fromUtf8("推理异常: %1").arg(e.what());
        ZSU_LOG_ERROR("ORTInferenceEngine: " + m_lastError);
        result["status"] = "unexpected_error";
        result["error"] = m_lastError;
        result["latency_ms"] = totalTimer.elapsed();
        emit inferenceCompleted(false);
        return false;
    } catch (...) {
        m_errorState = ErrorState::FatalError;
        m_lastError = QString::fromUtf8("推理发生严重错误");
        ZSU_LOG_ERROR("ORTInferenceEngine: " + m_lastError);
        result["status"] = "fatal_error";
        result["error"] = m_lastError;
        result["latency_ms"] = totalTimer.elapsed();
        emit inferenceCompleted(false);
        return false;
    }
}

// ============================================================================
// 批量推理
// ============================================================================

bool ORTInferenceEngine::inferBatch(const QList<cv::Mat>& inputs, QList<QJsonObject>& results) {
    results.clear();
    int total = inputs.size();
    for (int i = 0; i < total; ++i) {
        if (isCancelled()) {
            ZSU_LOG_WARN("ORTInferenceEngine: 批量推理被取消");
            break;
        }
        QJsonObject result;
        infer(inputs[i], result);
        results.append(result);
        emit progressUpdated(i + 1, total);
    }
    return !results.isEmpty();
}

// ============================================================================
// 后处理（分派）
// ============================================================================

QJsonObject ORTInferenceEngine::postprocess(const cv::Mat& output, const cv::Mat& preprocessed) {
    QElapsedTimer timer;
    timer.start();

    QJsonObject result;

    if (output.dims == 2) {
        // --- 分类输出 [1, N] ---
        // softmax + top-5 + argmax
        cv::Mat softmax;
        cv::Mat output2D = output.reshape(1, 1);   // 展平为 [1, N]

        // softmax
        double maxVal = 0;
        cv::minMaxLoc(output2D, nullptr, &maxVal);
        cv::exp(output2D - maxVal, softmax);
        double sum = cv::sum(softmax)[0];
        if (sum > 0) softmax /= sum;

        // top-1
        cv::Point maxLoc;
        cv::minMaxLoc(softmax, nullptr, nullptr, nullptr, &maxLoc);
        int bestClass = maxLoc.x;
        float bestConf = softmax.at<float>(0, bestClass);

        // 类别标签映射
        QString categoryName;
        if (!m_classLabels.isEmpty() && bestClass >= 0 && bestClass < m_classLabels.size()) {
            categoryName = m_classLabels[bestClass];
        } else {
            categoryName = QString("Class_%1").arg(bestClass);
        }
        result["category"] = categoryName;
        result["confidence"] = bestConf;
        result["class_index"] = bestClass;

        // top-5
        QJsonArray top5;
        cv::Mat sortedIdx;
        cv::sortIdx(softmax, sortedIdx, cv::SORT_EVERY_ROW | cv::SORT_DESCENDING);
        for (int i = 0; i < std::min(5, softmax.cols); ++i) {
            int idx = sortedIdx.at<int>(0, i);
            QJsonObject item;
            item["class_id"] = idx;
            item["confidence"] = softmax.at<float>(0, idx);
            if (!m_classLabels.isEmpty() && idx >= 0 && idx < m_classLabels.size()) {
                item["class_name"] = m_classLabels[idx];
            } else {
                item["class_name"] = QString("Class_%1").arg(idx);
            }
            top5.append(item);
        }
        result["top5"] = top5;
        result["model_type"] = "classification";

    } else if (output.dims == 3) {
        // --- YOLO 检测输出 ---
        postprocessYOLO(output, result);

    } else if (output.dims == 4) {
        // --- 4D 输出（仅记录形状） ---
        result["model_type"] = "4d_output";
        result["output_shape"] = QString("[%1,%2,%3,%4]")
            .arg(output.size[0]).arg(output.size[1])
            .arg(output.size[2]).arg(output.size[3]);
    } else {
        result["model_type"] = "unknown";
        result["error"] = QString::fromUtf8("不支持的输出维度: %1").arg(output.dims);
    }

    m_lastMetrics.postprocessMs = timer.elapsed();
    return result;
}

// ============================================================================
// YOLOv5/v8 后处理
// ============================================================================

void ORTInferenceEngine::postprocessYOLO(const cv::Mat& output, QJsonObject& result) {
    int dim1 = output.size[1];
    int dim2 = output.size[2];

    // YOLOv8: dim1(4+nc) < dim2(anchors)；YOLOv5: dim1(anchors) > dim2(5+nc)
    bool is_yolov8 = (dim1 < dim2);

    int num_anchors = is_yolov8 ? dim2 : dim1;
    int num_attrs = is_yolov8 ? dim1 : dim2;
    int num_classes = is_yolov8 ? (num_attrs - 4) : (num_attrs - 5);

    result["output_shape"] = QString("%1x%2x%3").arg(output.size[0]).arg(dim1).arg(dim2);
    result["model_type"] = is_yolov8 ? "yolov8" : "yolov5";
    result["num_classes"] = num_classes;
    result["num_anchors"] = num_anchors;

    if (num_classes <= 0 || num_anchors <= 0) {
        result["category"] = QString::fromUtf8("类别数或锚点数无效");
        result["confidence"] = 0.0;
        result["note"] = "invalid_yolo_shape";
        return;
    }

    // 检测框
    struct Detection {
        float cx, cy, w, h;
        float confidence;
        int class_id;
    };
    std::vector<Detection> dets;

    const float conf_threshold = m_confThreshold;
    const float iou_threshold = m_iouThreshold;

    auto sigmoid = [](float x) {
        return 1.0f / (1.0f + std::exp(-x));
    };

    const float* data = (const float*)output.data;

    float max_conf_all = 0.0f;
    int max_conf_class = -1;

    for (int i = 0; i < num_anchors; ++i) {
        float cx, cy, w, h, conf = 0.0f;
        int class_id = 0;

        if (is_yolov8) {
            // YOLOv8: [1, 4+nc, anchors]
            cx = data[0 * num_anchors + i];
            cy = data[1 * num_anchors + i];
            w = data[2 * num_anchors + i];
            h = data[3 * num_anchors + i];

            float max_score = 0.0f;
            for (int c = 0; c < num_classes; ++c) {
                float score = data[(4 + c) * num_anchors + i];
                if (score > max_score) {
                    max_score = score;
                    class_id = c;
                }
            }
            conf = max_score;
        } else {
            // YOLOv5: [1, anchors, 5+nc]
            const float* row = data + i * num_attrs;
            cx = row[0]; cy = row[1]; w = row[2]; h = row[3];
            float obj = sigmoid(row[4]);

            float max_score = 0.0f;
            for (int c = 0; c < num_classes; ++c) {
                float score = sigmoid(row[5 + c]) * obj;
                if (score > max_score) {
                    max_score = score;
                    class_id = c;
                }
            }
            conf = max_score;
        }

        if (conf > max_conf_all) {
            max_conf_all = conf;
            max_conf_class = class_id;
        }
        if (conf >= conf_threshold) {
            dets.push_back({cx, cy, w, h, conf, class_id});
        }
    }

    // 按置信度降序排序
    std::sort(dets.begin(), dets.end(), [](const Detection& a, const Detection& b) {
        return a.confidence > b.confidence;
    });

    // NMS
    auto computeIoU = [](const Detection& a, const Detection& b) {
        float ax1 = a.cx - a.w / 2, ay1 = a.cy - a.h / 2;
        float ax2 = a.cx + a.w / 2, ay2 = a.cy + a.h / 2;
        float bx1 = b.cx - b.w / 2, by1 = b.cy - b.h / 2;
        float bx2 = b.cx + b.w / 2, by2 = b.cy + b.h / 2;

        float inter_x1 = std::max(ax1, bx1);
        float inter_y1 = std::max(ay1, by1);
        float inter_x2 = std::min(ax2, bx2);
        float inter_y2 = std::min(ay2, by2);
        float inter_w = std::max(0.0f, inter_x2 - inter_x1);
        float inter_h = std::max(0.0f, inter_y2 - inter_y1);
        float inter_area = inter_w * inter_h;
        float area_a = (ax2 - ax1) * (ay2 - ay1);
        float area_b = (bx2 - bx1) * (by2 - by1);
        float union_area = area_a + area_b - inter_area;
        return union_area > 0 ? inter_area / union_area : 0.0f;
    };

    std::vector<int> keep;
    std::vector<bool> suppressed(dets.size(), false);
    for (size_t i = 0; i < dets.size(); ++i) {
        if (suppressed[i]) continue;
        keep.push_back((int)i);
        for (size_t j = i + 1; j < dets.size(); ++j) {
            if (suppressed[j]) continue;
            if (computeIoU(dets[i], dets[j]) > iou_threshold) {
                suppressed[j] = true;
            }
        }
    }

    // 输出
    if (keep.empty()) {
        ZSU_LOG_WARN(QString("ORTInferenceEngine YOLO: 无检测. max_conf=%1 (class=%2) threshold=%3 anchors=%4 classes=%5")
            .arg(max_conf_all, 0, 'f', 4).arg(max_conf_class)
            .arg(conf_threshold, 0, 'f', 2).arg(num_anchors).arg(num_classes));
        result["category"] = QString::fromUtf8("无检测");
        result["confidence"] = 0.0;
        result["note"] = "no_detection_above_threshold";
        result["max_confidence"] = max_conf_all;
        result["max_conf_class"] = max_conf_class;
        result["conf_threshold"] = conf_threshold;
    } else {
        const Detection& best = dets[keep[0]];
        QString categoryName;
        if (!m_classLabels.isEmpty() && best.class_id >= 0 && best.class_id < m_classLabels.size()) {
            categoryName = m_classLabels[best.class_id];
        } else {
            categoryName = QString("Class_%1").arg(best.class_id);
        }
        result["category"] = categoryName;
        result["confidence"] = best.confidence;
        result["class_index"] = best.class_id;

        QJsonArray detections;
        for (int idx : keep) {
            const Detection& d = dets[idx];
            QJsonObject det;
            det["class_id"] = d.class_id;
            det["confidence"] = d.confidence;
            det["cx"] = d.cx;
            det["cy"] = d.cy;
            det["w"] = d.w;
            det["h"] = d.h;
            detections.append(det);
        }
        result["detections"] = detections;
        result["num_detections"] = (int)keep.size();
    }
}

// ============================================================================
// 预热
// ============================================================================

bool ORTInferenceEngine::warmUp(int iterations) {
    if (!m_modelLoaded) return false;

    cv::Mat testImage(m_inputSize.height(), m_inputSize.width(), CV_8UC3, cv::Scalar(128));
    double totalMs = 0;

    for (int i = 0; i < iterations; ++i) {
        QJsonObject result;
        if (!infer(testImage, result)) {
            ZSU_LOG_WARN(QString("ORTInferenceEngine: warmUp 迭代 %1 失败").arg(i));
            return false;
        }
        totalMs += m_lastMetrics.totalMs;
    }

    double avgMs = totalMs / iterations;
    ZSU_LOG_INFO(QString("ORTInferenceEngine: warmUp 完成 %1 次, 平均 %2 ms").arg(iterations).arg(avgMs, 0, 'f', 1));
    return true;
}

} // namespace zsu
