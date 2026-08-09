# Blob 与轮廓分析算子手册

> **最后更新**：2026-07-16  
> **对应版本**：operators.json v2.7.0  
> **算子数量**：5 个

---

## BlobDetect（斑块检测）

### 核心含义
斑块检测算子基于 OpenCV `SimpleBlobDetector` 实现自动化的斑点/斑块检测，通过面积和圆度等特征对图像中的连通斑块进行筛选。该算子能够一次性输出所有满足条件的斑块信息，适用于目标数量较多且形态相近的场景。

与手动阈值 + 连通域分析流程相比，BlobDetect 内置了完整的斑块过滤机制，使用更简洁。

### 原理简述
算子内部依次执行：图像二值化 → 连通域提取 → 斑块中心计算 → 特征过滤。过滤条件包括：

- 面积过滤：仅保留面积在 `[minArea, maxArea]` 范围内的斑块。
- 圆度过滤：圆度定义为 `4π·A / P²`（A 为面积，P 为周长），完美圆的圆度为 1.0。仅保留圆度在 `[minCircularity, maxCircularity]` 范围内的斑块。

最终输出每个满足条件斑块的中心坐标、面积、圆度等信息。

### 适用场景
- 场景1：圆形零件（如螺钉、铆钉、钢珠）的自动计数与定位。
- 场景2：细胞或颗粒的检测与统计。
- 场景3：标定点阵列中圆点的快速提取。

### 参数详解
| 参数名 | 中文名 | 类型 | 默认值 | 范围 | 单位 | 说明 |
|--------|--------|------|--------|------|------|------|
| minArea | 最小面积 | Double | 10 | 0 ~ 1000000 | px² | 斑块最小面积（像素²），步长 10 |
| maxArea | 最大面积 | Double | 50000 | 0 ~ 1000000 | px² | 斑块最大面积（像素²），步长 100 |
| minCircularity | 最小圆度 | Double | 0 | 0 ~ 1 | - | 圆度下限（1.0=完美圆），步长 0.05 |
| maxCircularity | 最大圆度 | Double | 1 | 0 ~ 1 | - | 圆度上限，步长 0.05 |

### 输入输出
- **输入**：单通道灰度图像（Mat）。建议输入对比度较高的图像。
- **输出**：
  - `blobs`（Blob[]）：斑块列表，类型为 Blob 数组，描述为"检测到的斑块"。

### 使用示例
检测圆形标定点：
```json
{
  "type": "BlobDetect",
  "params": {
    "minArea": 50,
    "maxArea": 5000,
    "minCircularity": 0.7,
    "maxCircularity": 1.0
  }
}
```
步骤说明：设置面积范围 50~5000 像素² 过滤过小噪点和过大区域，圆度下限 0.7 确保只保留接近圆形的标定点。

### 注意事项
- 面积和圆度范围需配合设置，若 `minArea > maxArea` 或 `minCircularity > maxCircularity` 将无法检测到任何斑块。
- 输入图像光照不均时斑块检测效果会下降，建议先做预处理。
- 圆度计算依赖轮廓提取，对噪声敏感，可先做高斯模糊。

### 关联算子
- [Threshold](05_图像分割.md#threshold阈值分割)：可先二值化提升斑块检测稳定性。
- [Connection](#connection连通域分析)：当不需要圆度过滤时可用连通域分析替代。
- [AreaCenter](#areacenter面积中心点)：对检测到的斑块计算精确重心。

---

## Connection（连通域分析）

### 核心含义
连通域分析算子将二值图像分割为多个独立的连通域，为每个连通区域分配唯一 ID。该算子是 Blob 分析的基础步骤，用于将阈值分割后的前景分解为可单独处理的目标单元。

支持 4 连通和 8 连通两种邻域定义，用户可根据目标形态选择合适的连通方式。

### 原理简述
算子调用 OpenCV `cv::connectedComponents` 或 `cv::connectedComponentsWithStats` 函数，对二值图像进行连通域标记。算法扫描图像中每个前景像素，根据邻域关系（4 连通：上下左右；8 连通：上下左右及对角线）将相邻像素归为同一连通域。

- 4 连通：仅考虑上下左右 4 个邻域像素，对细长水平/垂直目标友好。
- 8 连通：考虑 8 个邻域像素（含对角线），对斜向连接的目标更合适。

### 适用场景
- 场景1：二值化后分离多个独立目标，为后续逐个分析做准备。
- 场景2：目标计数，统计图像中满足条件的连通区域数量。
- 场景3：提取各连通域的外接矩形、面积等统计信息。

### 参数详解
| 参数名 | 中文名 | 类型 | 默认值 | 范围 | 单位 | 说明 |
|--------|--------|------|--------|------|------|------|
| connectivity | 连通方式 | Enum | 8 | 4 / 8 | - | 4 连通或 8 连通 |

### 输入输出
- **输入**：二值图像（单通道，像素值为 0 或 255）。非二值图像需先阈值化。
- **输出**：
  - `regions`（Region[]）：连通域，类型为 Region 数组，描述为"连通域分割结果"。

### 使用示例
分离二值图中的多个目标：
```json
{
  "type": "Connection",
  "params": {
    "connectivity": 8
  }
}
```
步骤说明：输入经 Threshold 二值化后的图像，使用 8 连通分离所有相邻目标，输出每个连通域的区域信息。

### 注意事项
- 输入必须是二值图像，若传入灰度图像可能产生异常结果。
- 8 连通可能将斜向接触的噪点误连为目标，对噪声敏感的场景建议用 4 连通。
- 连通域数量过多时会消耗较多内存，可先用形态学开运算去除小噪点。

### 关联算子
- [Threshold](05_图像分割.md#threshold阈值分割)：连通域分析的前置步骤，生成二值图。
- [SelectShape](#selectshape形状选择)：对连通域按形状特征进一步筛选。
- [AreaCenter](#areacenter面积中心点)：计算各连通域的面积与重心。
- [ContourAnalyze](#contouranalyze轮廓分析)：提取连通域的轮廓用于形状分析。

---

## ContourAnalyze（轮廓分析）

### 核心含义
轮廓分析算子查找图像中的轮廓并按面积进行过滤，内置 OTSU 自适应阈值二值化，能够自动确定最佳分割阈值。该算子提取的轮廓可用于形状匹配、尺寸测量、缺陷检测等后续分析。

与 Connection 不同，ContourAnalyze 关注的是目标的边界轮廓而非填充区域，更适合需要边界信息的场景。

### 原理简述
算子首先对输入灰度图应用 OTSU 自动阈值二值化（`cv::threshold` 配 `THRESH_OTSU` 标志），然后调用 `cv::findContours` 提取所有外轮廓。对每条轮廓计算面积，若 `filterByArea` 为 true，则仅保留面积在 `[minArea, maxArea]` 范围内的轮廓。

OTSU 算法通过最大化类间方差自动确定最优阈值，无需用户手动指定阈值，适用于双峰直方图（前景与背景灰度分别集中）的图像。

### 适用场景
- 场景1：目标边界提取，用于尺寸测量或形状分析。
- 场景2：缺陷检测，提取缺陷轮廓用于分类。
- 场景3：OCR 前的文字区域轮廓提取。

### 参数详解
| 参数名 | 中文名 | 类型 | 默认值 | 范围 | 单位 | 说明 |
|--------|--------|------|--------|------|------|------|
| minArea | 最小面积 | Double | 100 | 0 ~ 1000000 | px² | 轮廓最小面积，步长 50 |
| maxArea | 最大面积 | Double | 100000 | 0 ~ 1000000 | px² | 轮廓最大面积，步长 100 |
| filterByArea | 启用面积过滤 | Bool | true | - | - | 勾选后只保留 minArea~maxArea 之间的轮廓 |

### 输入输出
- **输入**：单通道灰度图像（Mat）。OTSU 二值化要求图像直方图呈双峰分布效果最佳。
- **输出**：
  - `contours`（Contour[]）：轮廓，类型为 Contour 数组，描述为"提取的轮廓列表"。

### 使用示例
提取工件轮廓用于尺寸测量：
```json
{
  "type": "ContourAnalyze",
  "params": {
    "minArea": 500,
    "maxArea": 50000,
    "filterByArea": true
  }
}
```
步骤说明：OTSU 自动二值化后提取轮廓，面积过滤范围 500~50000 像素² 去除噪点和过大的背景区域，保留工件轮廓。

### 注意事项
- OTSU 自动阈值假设图像直方图呈双峰分布，若前景与背景灰度重叠严重，二值化效果会下降。
- `filterByArea` 为 false 时将返回所有轮廓，包括噪点，建议保持开启。
- 轮廓提取模式为外轮廓（RETR_EXTERNAL），不提取内孔轮廓。

### 关联算子
- [Threshold](05_图像分割.md#threshold阈值分割)：当 OTSU 效果不佳时可先手动二值化。
- [SelectShape](#selectshape形状选择)：对提取的轮廓按形状特征筛选。
- [LineCircleDetect](07_特征提取与识别.md#linecircledetect线圆检测)：对轮廓进行直线/圆拟合。
- [GeometryMeasure](09_几何测量.md#geometrymeasure几何测量)：基于轮廓进行几何量测量。

---

## SelectShape（形状选择）

### 核心含义
形状选择算子（类似 HALCON `select_shape`）按形状特征从区域中筛选满足条件的目标。支持面积、宽度、高度、长宽比、矩形度、圆形度、紧凑度等 7 种形状特征，能够灵活地根据几何属性过滤目标。

该算子通常接在 Connection 或 ContourAnalyze 之后，用于从多个区域中筛选出特定形状的目标。

### 原理简述
算子首先对输入灰度图以 `threshold` 进行二值化，然后提取所有连通域，对每个连通域计算指定的形状特征值，仅保留特征值落在 `[minValue, maxValue]` 范围内的区域。

各特征的计算方式：
- `area`：区域像素总数。
- `width` / `height`：外接矩形的宽度 / 高度。
- `ratio`：长宽比 = max(width, height) / min(width, height)。
- `rectangularity`：矩形度 = area / (width × height)。
- `circularity`：圆形度 = 4π·area / perimeter²。
- `compactness`：紧凑度 = perimeter² / (4π·area)。

### 适用场景
- 场景1：从多个连通域中筛选特定面积范围的目标。
- 场景2：按长宽比筛选细长形零件（如螺钉）。
- 场景3：按圆形度筛选圆形目标，排除非圆形噪点。

### 参数详解
| 参数名 | 中文名 | 类型 | 默认值 | 范围 | 单位 | 说明 |
|--------|--------|------|--------|------|------|------|
| threshold | 二值化阈值 | Double | 128.0 | 0 ~ 255 | - | 灰度二值化阈值，步长 1 |
| featureType | 特征类型 | Enum | area | area / width / height / ratio / rectangularity / circularity / compactness | - | 选择依据的形状特征 |
| minValue | 最小值 | Double | 100 | 0 ~ 10000000 | - | 特征最小值（面积/宽高单位为像素，比率为 0~20，矩形度/圆形度/紧凑度为 0~1），步长 10 |
| maxValue | 最大值 | Double | 1000000 | 0 ~ 10000000 | - | 特征最大值，步长 10 |

### 输入输出
- **输入**：单通道灰度图像（Mat）。算子内部会进行二值化。
- **输出**：
  - `regions`（Region[]）：筛选区域，类型为 Region 数组，描述为"形状筛选后的区域"。

### 使用示例
按圆形度筛选圆形目标：
```json
{
  "type": "SelectShape",
  "params": {
    "threshold": 128,
    "featureType": "circularity",
    "minValue": 0.7,
    "maxValue": 1.0
  }
}
```
步骤说明：二值化阈值 128，按圆形度筛选，保留圆度 0.7~1.0 的区域，适用于圆形零件提取。

按面积筛选目标：
```json
{
  "type": "SelectShape",
  "params": {
    "threshold": 100,
    "featureType": "area",
    "minValue": 1000,
    "maxValue": 50000
  }
}
```

### 注意事项
- `minValue` 和 `maxValue` 的合理范围随 `featureType` 变化：面积/宽高为像素值（0~数百万），长宽比为 0~20，矩形度/圆形度/紧凑度为 0~1。设置时需匹配特征类型。
- `threshold` 二值化阈值直接影响连通域提取质量，光照不均时建议先用 [DynThreshold](05_图像分割.md#dynthreshold动态阈值)。
- 当 `featureType` 为 `compactness` 时，值越小越接近圆形（紧凑度与圆形度互为倒数关系）。

### 关联算子
- [Connection](#connection连通域分析)：SelectShape 内部已含连通域提取，也可接在 Connection 之后使用。
- [ContourAnalyze](#contouranalyze轮廓分析)：提取轮廓后可配合 SelectShape 筛选。
- [BlobDetect](#blobdetect斑块检测)：BlobDetect 内置圆度过滤，SelectShape 提供更丰富的形状特征。
- [AreaCenter](#areacenter面积中心点)：筛选后计算区域重心用于定位。

---

## AreaCenter（面积中心点）

### 核心含义
面积中心点算子计算前景区域的面积与重心坐标，支持三种重心计算模式：整体区域重心、最大连通域重心、图像矩重心。该算子是目标定位的基础工具，输出目标的位置和尺寸信息。

重心坐标常用于引导机械臂抓取、目标跟踪、对位对齐等场景。

### 原理简述
算子首先以 `threshold` 对输入灰度图二值化得到前景 mask，然后根据 `mode` 选择重心计算方式：

- `region`：计算所有前景像素的整体重心，`cx = Σx / N`，`cy = Σy / N`（N 为前景像素总数）。
- `largest`：先提取所有连通域，选取面积最大的连通域，再计算其重心。
- `moments`：基于图像矩（`cv::moments`）计算重心，利用一阶矩 `m10/m00` 和 `m01/m00` 得到重心坐标，对灰度加权敏感。

面积 `area` 为前景像素总数（`mode=largest` 时为最大连通域面积）。

### 适用场景
- 场景1：目标定位，输出重心坐标引导机械臂抓取。
- 场景2：目标尺寸检测，通过面积判断工件是否合格。
- 场景3：对位对齐，计算目标重心与参考点的偏差。

### 参数详解
| 参数名 | 中文名 | 类型 | 默认值 | 范围 | 单位 | 说明 |
|--------|--------|------|--------|------|------|------|
| threshold | 二值化阈值 | Double | 128.0 | 0 ~ 255 | - | 灰度二值化阈值，范围 [0, 255]，步长 1 |
| mode | 模式 | Enum | region | region / largest / moments | - | region=所有前景整体重心，largest=最大连通域重心，moments=图像矩重心 |

### 输入输出
- **输入**：单通道灰度图像（Mat）。二值化阈值需根据目标与背景的灰度差合理设置。
- **输出**：
  - `center`（Point）：中心点，类型为 Point，描述为"区域重心坐标 (x, y)"。
  - `area`（double）：面积，类型为 double，描述为"前景像素总数"。

### 使用示例
计算最大连通域的重心用于机械臂定位：
```json
{
  "type": "AreaCenter",
  "params": {
    "threshold": 128,
    "mode": "largest"
  }
}
```
步骤说明：二值化阈值 128，`mode=largest` 选取最大连通域计算重心，避免多个目标干扰定位结果。

使用图像矩计算灰度加权重心：
```json
{
  "type": "AreaCenter",
  "params": {
    "threshold": 100,
    "mode": "moments"
  }
}
```

### 注意事项
- `mode=region` 时若图像中有多个目标，重心会落在所有目标的几何中心，可能不在任何目标上。
- `mode=largest` 依赖连通域提取，若目标与背景连通会导致面积计算错误。
- `mode=moments` 对灰度分布敏感，适用于灰度均匀目标的精确定位。

### 关联算子
- [Threshold](05_图像分割.md#threshold阈值分割)：AreaCenter 内部已含二值化，也可先用 Threshold 预处理。
- [Connection](#connection连通域分析)：`mode=largest` 内部使用连通域分析。
- [SelectShape](#selectshape形状选择)：先按形状筛选区域再计算重心。
- [DistancePp](09_几何测量.md#distancepp点到点距离)：计算重心与参考点的距离。
- [GeometryMeasure](09_几何测量.md#geometrymeasure几何测量)：基于重心进行几何测量。
