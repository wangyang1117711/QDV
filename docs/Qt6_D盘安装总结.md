# Q-DetectVision Qt6 D盘安装总结

## 已创建的文件

### D 盘目录结构
```
D:\Qt\
├── Qt6_D盘安装指南.md              # 完整安装指南
├── env_setup.ps1                    # PowerShell 环境配置
├── env_setup.bat                    # 批处理环境配置
├── Install\                          # 下载的安装程序
│   ├── install_config.xml           # Qt安装配置
│   └── qt-unified-windows-x64-online.exe (需下载)
├── Tools\                           # 工具目录
│   └── mingw1120_64\ (安装后有)
├── 6.5.0\                           # Qt 安装目录 (安装后有)
│   └── mingw_64\
├── opencv\                          # OpenCV (安装后有)
└── Examples\                        # 示例项目
    └── HelloQt\
```

### 项目文件 (E:\anchor\Trae\QDV)
```
├── build_D盘.bat                    # 批处理构建脚本
├── build_D盘.ps1                    # PowerShell 构建脚本
└── CMakeLists.txt_D盘更新           # 更新的 CMake 配置
```

## 安装步骤总结

### 第一步: 下载 Qt Online Installer
1. 访问 https://www.qt.io/download-open-source
2. 下载 qt-unified-windows-x64-online.exe
3. 保存到 D:\Qt\Install\

### 第二步: 运行 Qt 安装
1. 双击安装程序
2. 选择自定义安装路径: D:\Qt\6.5.0
3. 选择组件:
   - Qt 6.5.0 → MinGW 11.2.0 64-bit
   - Qt Charts, Qt 5 Compatibility Module
   - Qt Creator
4. 点击安装

### 第三步: 设置环境
```powershell
# PowerShell (以管理员身份运行)
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass
. D:\Qt\env_setup.ps1
```

### 第四步: 编译项目
```powershell
# 运行项目构建脚本
. E:\anchor\Trae\QDV\build_D盘.ps1
```

## 快速入门命令

### 完整命令序列
```powershell
# 1. 临时允许脚本执行
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass

# 2. 设置 Qt 环境
. D:\Qt\env_setup.ps1

# 3. 编译项目
cd E:\anchor\Trae\QDV
. .\build_D盘.ps1
```

## 验证检查清单

- [ ] D:\Qt\ 目录已创建
- [ ] Qt Online Installer 已下载到 D:\Qt\Install
- [ ] Qt6.5.0 已安装到 D:\Qt\6.5.0\mingw_64
- [ ] qmake --version 运行成功
- [ ] HelloQt 示例项目编译运行成功
- [ ] Q-DetectVision 项目编译成功

## 故障排除

### 1. 执行策略被限制
```powershell
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass
```

### 2. Qt 找不到
确保安装路径完全匹配: D:\Qt\6.5.0\mingw_64

### 3. CMake 找不到 MinGW
确保 MinGW 安装在 D:\Qt\Tools\mingw1120_64 或修改路径

### 4. 编译错误
```powershell
# 设置环境后再编译
. D:\Qt\env_setup.ps1
cd build
cmake .. -G "MinGW Makefiles"
cmake --build .
```

## 下一步

1. 按照 D:\Qt\Qt6_D盘安装指南.md 进行安装
2. 下载并安装 Qt6.5.0 到 D:\Qt
3. 配置环境并测试
4. 编译 Q-DetectVision 项目

## 文档

- 详细安装指南: D:\Qt\Qt6_D盘安装指南.md
- 环境设置脚本: D:\Qt\env_setup.ps1
- 项目构建脚本: E:\anchor\Trae\QDV\build_D盘.ps1

---

**注意**: 由于 Qt 安装包较大 (约 4-10GB)，需要手动下载和安装。所有必要的配置文件已准备就绪！