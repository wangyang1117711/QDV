# Q-DetectVision — SCMVS 功能克隆版开发规划

> **基于分析报告**：`SCMVS_Architecture_Analysis.html`
> **参考原型**：海康机器人 SCMVS v3.3.0 (Smart Camera Machine Vision System)
> **规划日期**：2026-05-14
> **规划版本**：v1.0

---

## 一、项目愿景与目标

### 1.1 核心定位

**Q-DetectVision（QDV）** 是一款面向工业质检场景的一体化机器视觉客户端软件，实现：
- **方案可视化编辑**：拖拽式构建视觉检测方案（工具链 + 分支控制）
- **深度学习全流程**：标注 → 训练 → 推理（本地端到端）
- **多协议输出**：TCP / 串口 / FTP / IO 数字量
- **GigE Vision 相机管理**：设备发现、参数配置、固件升级

### 1.2 功能范围对照表

| 核心功能 | SCMVS 原版 | QDV 克隆版 | 优先级 |
|---------|-----------|-----------|--------|
| 方案管理（创建/编辑/保存/切换） | ✅ | ✅ | P0 |
| 工具箱（传统视觉算法） | ✅ 50+ 工具 | ✅ MVP 20个 | P0 |
| AI 深度学习推理 | ✅ 6类任务 | ✅ 4类任务 | P0 |
| AI 本地训练（标注+训练+导出） | ✅ | ✅ | P1 |
| GigE Vision 相机管理 | ✅ | ✅ | P0 |
| 结果渲染（ROI / 叠加 / 直方图） | ✅ | ✅ | P0 |
| 工业通信（TCP/串口/IO） | ✅ | ✅ | P0 |
| 运行监控界面（多相机/统计） | ✅ | ✅ | P1 |
| 方案版本管理 / 历史记录 | ✅ | ✅ | P2 |
| 固件升级 / 日志管理 | ✅ | ✅ | P2 |

---

## 二、技术栈选型

### 2.1 架构决策

| 维度 | 方案A（推荐） | 方案B（备选） |
|------|-------------|-------------|
| **前端 UI** | **Qt6 + QML** | Electron + Vue3 |
| **业务逻辑** | C++ / Qt 信号槽 | TypeScript / Rust |
| **传统算法** | **OpenCV 4.x** | C++ SDK |
| **AI 推理** | **ONNX Runtime** (统一 CPU/GPU) | OpenVINO (CPU) + CUDA (GPU) |
| **AI 训练** | **PyTorch** (Python 端) | TensorFlow |
| **通信协议** | 原生 Socket / Serial | Python asyncio |
| **数据库** | **SQLite3** | PostgreSQL |
| **构建系统** | **CMake** + vcpkg | CMake + Conan |
| **版本控制** | Git LFS (大文件) | — |
| **目标平台** | **Windows x64** | 跨平台 (macOS/Linux) |

> **选型理由**：Qt6 + C++ 组合最接近原版 SCMVS 的技术基因（Qt5/C++），性能最优、生态成熟。ONNX Runtime 作为推理统一层，可同时支持 CPU（OpenVINO backend）和 GPU（CUDA backend），避免维护两套独立的推理引擎 DLL。

### 2.2 技术栈详表

```
┌─────────────────────────────────────────────────────────────┐
│                      Q-DetectVision                       │
├──────────────┬──────────────┬───────────────┬────────────────┤
│   Qt6/QML   │  C++ (核心)  │  Python RPC   │   SQLite3      │
│   UI 层     │  业务逻辑     │  AI 训练服务   │   本地存储      │
├──────────────┴──────────────┴───────────────┴────────────────┤
│                    OpenCV 4.x  (传统视觉算法)                   │
├─────────────────────────────┬─────────────────────────────────┤
│      ONNX Runtime           │         PyTorch 2.x             │
│  (CPU/GPU 统一推理引擎)       │         (本地模型训练)            │
├──────────────┬──────────────┴───────────────┬────────────────┤
│   GigE Vision SDK  │   TCP/UDP Socket  │   串口 (Boost.Asio) │
├───────────────────┴────────────────────┴─────────────────────┤
│                Windows x64 (primary)                          │
└──────────────────────────────────────────────────────────────┘
```

### 2.3 核心依赖版本

| 依赖 | 版本 | 用途 |
|------|------|------|
| Qt6 | 6.7+ | UI 框架 |
| CMake | 3.27+ | 构建系统 |
| vcpkg | 2024.04+ | C++ 包管理 |
| OpenCV | 4.9.0 | 传统视觉算法 |
| ONNX Runtime | 1.17+ | AI 推理引擎 |
| PyTorch | 2.3+ | 模型训练 |
| OpenVINO | 2024.1+ | CPU 推理加速 |
| CUDA | 12.1+ | GPU 推理加速（可选） |
| SQLite3 | 3.45+ | 本地数据库 |
| Protocol Buffers | 4.25+ | C++/Python RPC 通信 |

---

## 三、系统架构设计

### 3.1 分层架构

```
┌────────────────────────────────────────────────────────────────┐
│                    LAYER 1 · 主程序入口                         │
│  SmartVision.exe · main.cpp · QApplication · 单例窗口管理        │
├────────────────────────────────────────────────────────────────┤
│                    LAYER 2 · 界面层 (Qt6 QML)                   │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐         │
│  │ LoginView│ │ RunView  │ │ EditView │ │MoniView  │         │
│  └──────────┘ └──────────┘ └──────────┘ └──────────┘         │
│  + QML 组件：CameraPanel / ToolBox / RenderWidget / IOConfig   │
├────────────────────────────────────────────────────────────────┤
│                    LAYER 3 · 业务逻辑层 (C++)                   │
│  SchemeManager · ToolChainExecutor · OutputController          │
│  CameraManager · CommunicationManager · ModelManager           │
│  + Qt Signal/Slot 事件驱动总线                                  │
├────────────────────────────────────────────────────────────────┤
│                    LAYER 4 · 服务线程层 (C++ Thread)            │
│  CameraGrabThread · ImageProcessThread · TrainServerThread     │
│  FileAccessThread · AsyncOperateThread · SchemeRefreshThread   │
├────────────────────────────────────────────────────────────────┤
│                    LAYER 5 · SDK / 算法层 (C++)                │
│  ┌──────────────┐  ┌──────────────────┐  ┌──────────────────┐  │
│  │ OpenCV 4.x   │  │ ONNX Runtime     │  │ OpenVINO / CUDA  │  │
│  │ 传统视觉工具   │  │ 统一推理接口      │  │ 推理后端加速      │  │
│  └──────────────┘  └──────────────────┘  └──────────────────┘  │
│  + PyTorch gRPC 服务（AI 训练）                                  │
├────────────────────────────────────────────────────────────────┤
│                    LAYER 6 · 硬件/系统层                         │
│  GigE Vision (GenICam) · TCP/UDP · Serial · SQLite3            │
└────────────────────────────────────────────────────────────────┘
```

### 3.2 核心领域模型

```cpp
// 方案 (Scheme) — 核心领域对象
class Scheme {
    QString id;                    // UUID
    QString name;                  // 方案名称
    QList<Tool*> toolChain;        // 工具链 (有序)
    QMap<QString, BranchNode*> branches; // 分支节点
    CameraConfig camera;           // 相机配置
    TriggerConfig trigger;         // 触发配置
    OutputConfig output;           // 输出配置
    ModelBinding model;            // AI 模型绑定
};

// 工具基类 (Tool)
class VisionTool {
    virtual QString type() = 0;   // "TemplateMatch" / "EdgeDetect" / "CNNPresence" ...
    virtual bool configure(const QJsonObject& params) = 0;
    virtual bool execute(const cv::Mat& input, ToolResult& result) = 0;
    virtual QJsonObject serialize() const = 0;
    virtual void deserialize(const QJsonObject& data) = 0;
};

// 工具结果 (ToolResult)
struct ToolResult {
    bool ok;                       // 单工具判定
    cv::Mat overlayImage;          // 叠加渲染图
    QJsonObject data;              // 具体数据 (坐标/数值/分类)
    double score;                  // 置信度
    qint64 elapsedMs;              // 执行耗时
};
```

### 3.3 插件化架构

参考 SCMVS 的 `SCPlugins` 热插拔机制：

```
plugins/
├── SCFormPlugin.dll          → 动态表单渲染
├── SCImageDisplayPlugin.dll  → 图像显示增强
├── SCChartPlugin.dll         → 折线图/趋势图
├── SCStatisticsPlugin.dll    → 生产统计
└── SCRenderPlugin.dll        → 大图渲染

// 插件接口
class IPlugin {
    virtual QString name() const = 0;
    virtual QString version() const = 0;
    virtual void initialize(QWidget* parent) = 0;
    virtual void onSchemeLoaded(Scheme* scheme) = 0;
    virtual void onFrameAcquired(const cv::Mat& frame) = 0;
    virtual void onToolResult(const ToolResult& result) = 0;
};
```

---

## 四、模块分解与开发计划

### 4.1 MVP 工具箱（20个核心工具）

#### 传统视觉工具（14个）

| # | 工具名称 | 英文标识 | OpenCV 实现 | 优先级 |
|---|---------|---------|------------|--------|
| 1 | 模板匹配 | TemplateMatch | `matchTemplate()` | P0 |
| 2 | 边缘检测 | EdgeDetect | `Canny()` | P0 |
| 3 | 斑块检测（Blob） | BlobDetect | `SimpleBlobDetector` | P0 |
| 4 | 几何测量 | GeometryMeasure | `arcLength()` / `contourArea()` | P0 |
| 5 | 灰度匹配 | GrayMatch | `matchTemplate()` | P0 |
| 6 | 轮廓分析 | ContourAnalyze | `findContours()` | P0 |
| 7 | 颜色识别 | ColorDetect | `inRange()` HSV | P0 |
| 8 | 图像预处理 | ImagePreprocess | `bilateralFilter()` / `morphologyEx()` | P0 |
| 9 | 阈值分割 | Threshold | `threshold()` | P1 |
| 10 | 直线/圆检测 | LineCircleDetect | `HoughLines()` / `HoughCircles()` | P1 |
| 11 | 图像运算 | ImageArithmetic | `add()` / `subtract()` | P1 |
| 12 | 图像变换 | ImageTransform | `warpAffine()` / `perspectiveTransform()` | P1 |
| 13 | 直方图 | Histogram | `calcHist()` | P2 |
| 14 | 字符识别（OCR） | OCRDetect | Tesseract OCR | P2 |

#### AI 深度学习工具（4个 MVP）

| # | 工具名称 | 英文标识 | 任务类型 | 优先级 |
|---|---------|---------|---------|--------|
| 1 | 有无检测 | CNNPresenceDetect | 异常/缺陷检测 | P0 |
| 2 | 分类识别 | CNNClassify | 二分类/多分类 | P0 |
| 3 | 目标检测 | CNNDetect | 矩形框定位 | P1 |
| 4 | 学习计数 | CNNCount | 数量统计 | P1 |

#### 流程控制工具（2个）

| # | 工具名称 | 英文标识 | 功能 | 优先级 |
|---|---------|---------|------|--------|
| 1 | 分支控制 | BranchControl | 条件跳转（OK/NG/数值范围） | P0 |
| 2 | 图像合并 | ImageMerge | 多图拼接/叠加 | P1 |

### 4.2 开发阶段划分

```
阶段 1 (8周) ── 核心框架与 UI Shell
阶段 2 (8周) ── 传统视觉工具链
阶段 3 (8周) ── AI 深度学习引擎
阶段 4 (6周) ── 相机管理与触发系统
阶段 5 (6周) ── 通信协议与 IO
阶段 6 (6周) ── 运行监控与数据管理
阶段 7 (4周) ── 插件系统与扩展
阶段 8 (4周) ── 集成测试与优化
────────────────────────────
合计: 约 50 周 (~12个月)
```

---

## 五、阶段详细计划

### 阶段 1：核心框架与 UI Shell（第 1-8 周）

**目标**：构建可运行的应用骨架，包含主界面导航框架。

#### 1.1 项目初始化（Week 1-2）

```
□ CMake + vcpkg 项目脚手架
  - 目录结构：src/ | include/ | tests/ | docs/ | plugins/
  - Qt6 集成：QML + C++ 混合
  - 单元测试框架：Catch2
□ 日志系统（spdlog）
□ 异常处理框架
□ 基础数据结构（Scheme / ToolResult）
```

#### 1.2 主程序与窗口框架（Week 3-4）

```
□ main.cpp — 单例窗口管理
□ LoginView — 登录界面（含记住密码 / AES 加密）（默认账户：a，默认密码：a）
□ CentralWindow — 中央调度窗口
□ NavBar — 顶部 6 栏导航（相机/方案/工具/IO/通信/监控）
□ 主题系统（深色/浅色/跟随系统）
□ 响应式布局（适配不同分辨率）
```

#### 1.3 方案管理模块（Week 5-6）

```
□ Scheme 数据模型（JSON 序列化）
□ 方案列表（SchemeListWidget）
□ 方案创建 / 导入 / 导出 / 删除
□ 方案切换（状态机：Idle → Loaded → Running）
□ LocalConfig.ini（AES 加密用户信息）
```

#### 1.4 方案编辑器基础（Week 7-8）

```
□ EditView 三栏布局（工具箱 | 画布 | 参数面板）
□ 工具箱列表（TreeView，支持分类展开）
□ 工具拖拽到画布（Drag & Drop）
□ 工具链树状展示（TreeWidget）
□ 基础参数面板渲染（JSON Schema → QML Form）
□ 方案文件保存格式（.svscheme，JSON 格式）
```

**交付物**：
- 可运行的 UI Shell（登录 → 主界面 → 方案编辑器）
- 方案 CRUD 完整流程
- 工具箱基础框架（可扩展）

---

### 阶段 2：传统视觉工具链（第 9-16 周）

**目标**：实现 14 个传统视觉工具，完成完整的工具链执行引擎。

#### 2.1 工具执行引擎（Week 9-10）

```
□ VisionTool 基类设计
□ ToolChainExecutor — 顺序/并行执行
□ BranchExecutor — 条件分支跳转
□ 工具结果聚合（MultiToolResult → SchemeResult）
□ 执行性能统计（各工具耗时）
□ 工具链调试模式（单步执行 / 断点）
```

#### 2.2 图像渲染引擎（Week 11-12）

```
□ RenderWidget（QML Canvas / QImage 混合）
□ ROI 绘制（矩形/圆形/多边形/线段）
□ 结果叠加（OK=绿色 / NG=红色 / 阈值=黄色）
□ 直方图显示（QML Chart）
□ 缩放 / 平移 / 复位
□ 多图对比模式（基准图 vs 结果图）
```

#### 2.3 工具实现（Week 13-16）

```
Week 13: 模板匹配 + 灰度匹配 + 边缘检测
Week 14: Blob 检测 + 轮廓分析 + 几何测量
Week 15: 颜色识别 + 图像预处理 + 阈值分割
Week 16: 直线圆检测 + 图像运算 + 图像变换
```

**交付物**：
- 14 个传统视觉工具（全部可用）
- 工具链执行引擎（支持分支）
- 渲染控件（ROI + 结果叠加）

---

### 阶段 3：AI 深度学习引擎（第 17-24 周）

**目标**：构建完整的 AI 训练 + 推理闭环。

#### 3.1 推理引擎架构（Week 17-18）

```
□ ONNX Runtime C++ 集成（统一 CPU/GPU）
□ ToolFactory — 工具实例工厂
□ 模型加载 / 卸载管理（LRU 缓存）
□ 推理预热（warm-up）机制
□ 模型版本管理（.onnx / .svmodel）
□ PyTorch → ONNX 导出流水线
```

#### 3.2 4 个 AI 工具实现（Week 19-22）

```
Week 19-20: CNNPresenceDetect + CNNClassify（推理 + 训练 UI）
  - 图像标注工具（矩形框标注 / 分类标签）
  - 数据集管理（JSON 元数据）
  - PyTorch 训练脚本
  - 模型导出 (.onnx)
  - 推理接口封装

Week 21-22: CNNDetect + CNNCount（推理 + 训练 UI）
```

#### 3.3 训练服务（Week 23-24）

```
□ Python gRPC 服务（PyTorch 训练）
□ C++ → Python RPC 通信（protobuf）
□ 训练进度实时推送（Qt Signal）
□ 断点续训支持
□ 模型评估指标（Precision / Recall / F1）
□ GPU 训练支持（CUDA 可用时自动切换）
```

**交付物**：
- 4 个 AI 工具（训练 + 推理）
- ONNX Runtime 推理引擎
- PyTorch 训练服务
- 标注工具 UI

---

### 阶段 4：相机管理与触发系统（第 25-30 周）

**目标**：支持 GigE Vision 相机全生命周期管理。

#### 4.1 GigE Vision 相机驱动（Week 25-26）

```
□ GenICam C++ 接口（参考 GCBase_MD 库）
□ 相机发现与枚举（GVCP 协议）
□ 相机连接 / 断开
□ 相机参数读写（GenApi 节点树）
□ 图像采集（grab / soft grab）
□ GigE Vision SDK 集成（Vimba / HALCON / 自研）
```

#### 4.2 触发系统（Week 27-28）

```
□ 触发模式：软触发 / IO 触发 / 编码器触发 / 自由运行
□ IO 数字量输入/输出（LINE0-4）
□ 触发时序控制（防抖 / 延时 / 脉冲宽度）
□ 多相机同步触发
□ 相机预览窗口（实时显示）
```

#### 4.3 标定工具（Week 29-30）

```
□ 棋盘格标定（张正友法）— `calibrateCamera()`
□ 手眼标定（Eye-in-Hand / Eye-to-Hand）
□ 标定结果应用（像素 → 物理坐标转换）
□ 畸变校正
```

**交付物**：
- GigE Vision 相机管理（发现 / 连接 / 采图）
- 4 种触发模式
- 标定工具

---

### 阶段 5：通信协议与 IO（第 31-36 周）

**目标**：实现工业级通信输出系统。

#### 5.1 TCP / UDP 通信（Week 31-32）

```
□ TCP 客户端 / 服务器模式
□ UDP 广播 / 单播
□ 协议格式：JSON / 自定义二进制 / 十六进制
□ 连接管理（断线重连 / 心跳保活）
□ CommunicationControlDlg — 通信控制字配置
□ 通信日志记录
```

#### 5.2 串口通信（Week 33-34）

```
□ RS232 / RS485 支持（Boost.Asio）
□ 波特率 / 数据位 / 停止位 / 校验位配置
□ 串口数据收发队列
□ Modbus RTU 协议支持（P2）
```

#### 5.3 IO 与 FTP（Week 35-36）

```
□ IO 数字量输出（LINE0-4，脉冲/电平模式）
□ IO 状态监控（实时显示）
□ FTP 服务器（图像结果推送）
□ 多通道并行输出（通信 + IO + FTP 同时）
```

**交付物**：
- TCP / UDP / 串口 通信模块
- IO 数字量输出
- FTP 结果推送

---

### 阶段 6：运行监控与数据管理（第 37-42 周）

**目标**：运行监控界面 + 历史数据管理。

#### 6.1 运行监控界面（Week 37-38）

```
□ RunMonitorView — 多相机运行状态
□ 统计数据（OK/NG 计数 / 良率 / 产能）
□ 实时折线图（QML Chart）
□ 自定义监控布局（拖拽控件）
□ 运行历史（RunHistory）
```

#### 6.2 历史数据管理（Week 39-40）

```
□ SQLite3 数据库设计
□ 检测结果记录（图像路径 / 判定 / 耗时 / 参数）
□ 历史查询（时间范围 / 判定类型 / 方案名）
□ 数据导出（CSV / Excel）
□ 数据清理策略（自动归档 / 手动清理）
```

#### 6.3 日志系统（Week 41-42）

```
□ 分级日志（DEBUG / INFO / WARN / ERROR）
□ 日志文件滚动（按天 / 按大小）
□ 日志查看器 UI（LvScLogMgrWindow）
□ 远程日志上报（P2）
```

**交付物**：
- 运行监控界面（含统计图表）
- 历史数据库与查询
- 日志系统

---

### 阶段 7：插件系统与扩展（第 43-46 周）

**目标**：实现类似 SCPlugins 的插件热加载机制。

```
□ IPlugin 接口定义
□ PluginManager — DLL 动态加载 / 卸载
□ 插件注册与发现机制
□ 插件沙箱隔离（异常捕获）
□ 插件市场 / 扩展能力（P2）
```

---

### 阶段 8：集成测试与优化（第 47-50 周）

**目标**：全面测试、性能优化、文档完善。

```
□ 集成测试（各模块联调）
□ 性能基准测试（采图延迟 / 推理速度 / UI 响应）
□ 内存泄漏检测（Valgrind / Dr. Memory）
□ 大并发压测
□ 用户文档 / API 文档
□ 部署包制作（NSIS / Inno Setup）
```

---

## 六、文件格式设计

### 6.1 方案文件（.svscheme — JSON）

```json
{
  "version": "1.0",
  "id": "uuid-xxxx",
  "name": "产品A质检方案",
  "created": "2026-05-14T10:00:00Z",
  "modified": "2026-05-14T12:00:00Z",
  "camera": {
    "ip": "192.168.1.64",
    "exposure": 5000,
    "gain": 8.0,
    "triggerMode": "Line0"
  },
  "toolChain": [
    {
      "id": "tool-001",
      "type": "ImagePreprocess",
      "params": { "denoise": true, "morphology": "open" }
    },
    {
      "id": "tool-002",
      "type": "CNNPresenceDetect",
      "params": { "model": "models/defect_v1.onnx", "threshold": 0.85 }
    },
    {
      "id": "tool-003",
      "type": "BranchControl",
      "condition": { "source": "tool-002.ok", "op": "==", "value": false },
      "trueBranch": ["tool-004"],
      "falseBranch": []
    }
  ],
  "output": {
    "tcp": { "host": "192.168.1.10", "port": 5000, "format": "json" },
    "io": { "line0": "pass", "line1": "fail" }
  }
}
```

### 6.2 AI 模型文件（.svmodel — ZIP 压缩包）

```
product_model.svmodel (ZIP)
├── model.onnx              # ONNX 推理模型
├── metadata.json            # 任务类型 / 输入尺寸 / 阈值
├── classes.json             # 分类标签
├── training_info.json       # 训练参数 / 数据集摘要
└── preview/                 # 预览样本图片
```

---

## 七、关键技术难点与应对策略

| 难点 | 风险等级 | 应对策略 |
|------|---------|---------|
| **GigE Vision SDK** 跨厂商兼容性 | 🔴 高 | 使用标准 GenICam 接口，兼容 Vimba / 厂商 SDK；提供插件架构支持多驱动 |
| **AI 训练服务** C++ ↔ Python 通信 | 🟡 中 | 使用 gRPC + Protobuf，明确接口契约；独立训练进程避免主程序崩溃 |
| **实时渲染** 4K 图像 + ROI 绘制性能 | 🟡 中 | GPU 加速渲染（Qt Quick + GPU）；LOD 多分辨率策略 |
| **工具链并行执行** 线程安全 | 🟡 中 | Qt Worker 线程池 + 互斥锁；工具间通过消息队列通信 |
| **ONNX 模型** 多后端切换 | 🟡 中 | 后端自动探测（CUDA → OpenVINO → CPU）；配置优先 |
| **方案文件** 版本兼容 | 🟢 低 | JSON Schema 校验；版本迁移脚本 |
| **插件安全** 第三方 DLL 崩溃 | 🟡 中 | 插件独立进程；SEH 异常捕获；超时机制 |
| **长周期开发** 需求蔓延 | 🔴 高 | MVP 严格聚焦；阶段交付物冻结；变更需评审 |

---

## 八、团队规模建议

| 角色 | MVP 最低人数 | 完整版人数 |
|------|------------|-----------|
| 项目经理 / Tech Lead | 1 | 1 |
| C++ 核心开发（框架 + 算法） | 2 | 3 |
| Qt UI 开发（QML + C++） | 1 | 2 |
| Python AI 工程师（训练框架） | 1 | 2 |
| 测试工程师 | 0.5 | 2 |
| **合计** | **5.5 ≈ 6人** | **≈ 10人** |

---

## 九、开发里程碑

```
里程碑 M1 (Week 8)   ── MVP Shell 交付
                       登录 → 方案管理 → 基础编辑器

里程碑 M2 (Week 16)  ── 视觉工具链完成
                       14 传统工具 + 执行引擎 + 渲染控件

里程碑 M3 (Week 24)  ── AI 引擎完成
                       4 个 AI 工具 + 训练服务 + 标注工具

里程碑 M4 (Week 30)  ── 相机系统完成
                       GigE Vision + 触发 + 标定

里程碑 M5 (Week 36)  ── 通信系统完成
                       TCP / 串口 / IO / FTP

里程碑 M6 (Week 42)  ── 监控系统完成
                       运行监控 + 历史 + 日志

里程碑 M7 (Week 46)  ── 插件系统完成
里程碑 M8 (Week 50)  ── 正式发布
```

---

## 十、开源 / 许可证建议

| 组件 | 许可证 | 说明 |
|------|--------|------|
| SmartVision Core | **GPL v3** | 开源核心框架 |
| 商业 AI 工具 | 专有插件 | 高级模型 / 云端训练 |
| 插件生态 | **Apache 2.0** | 鼓励第三方插件 |
| OpenCV / ONNX Runtime | BSD-3-Clause | 遵循其许可证 |
| Qt6 | GPL v3 / 商业 | 根据需求选择 |

---

*本文档为 Q-DetectVision 软件的初始开发规划，需求细节和技术选型需在架构评审会议中进一步确认。*
