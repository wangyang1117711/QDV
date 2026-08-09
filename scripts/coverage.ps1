<#
.SYNOPSIS
    QDetectVision 代码覆盖率脚本（MinGW + gcov）

.DESCRIPTION
    在 build_coverage 目录中以 -DENABLE_COVERAGE=ON 配置并构建项目，
    运行全部测试，随后使用 gcov 收集覆盖率数据。
    若检测到 lcov/genhtml 则生成 HTML 报告；否则输出文本摘要。

.NOTES
    使用方法：
        powershell -ExecutionPolicy Bypass -File scripts\coverage.ps1
    可选参数：
        -CleanBuild    构建前清空 build_coverage 目录
        -KeepGcda      不在运行测试前删除旧的 .gcda 文件
#>

param(
    [switch]$CleanBuild,
    [switch]$KeepGcda
)

$ErrorActionPreference = "Stop"

# ============================================================
# 路径配置（与项目环境保持一致）
# ============================================================
$ProjectRoot   = "E:\anchor\Trae\QDV"
$BuildDir      = Join-Path $ProjectRoot "build_coverage"
$TestExe       = Join-Path $BuildDir "bin\QDV_tests.exe"

$QtBin         = "D:\Qt_new\6.11.1\mingw_64\bin"
$MinGWBin      = "D:\Qt_new\Tools\mingw1310_64\bin"
$CMakeBin      = "D:\Qt_new\Tools\CMake_64\bin"
$OpenCVBin     = "D:\opencv\build_mingw\bin"
$PythonDir     = "D:\Program Files\Python314"

$GcovExe       = Join-Path $MinGWBin "gcov.exe"
$CoverageHtmlDir = Join-Path $BuildDir "coverage_html"
$CoverageTxt    = Join-Path $BuildDir "coverage_report.txt"

# ============================================================
# 辅助函数
# ============================================================
function Write-Step {
    param([string]$Message)
    Write-Host ""
    Write-Host "========================================" -ForegroundColor Cyan
    Write-Host "  $Message" -ForegroundColor Cyan
    Write-Host "========================================" -ForegroundColor Cyan
}

function Write-Ok   { param([string]$Msg) Write-Host "[OK]   $Msg" -ForegroundColor Green }
function Write-Info { param([string]$Msg) Write-Host "[INFO] $Msg" -ForegroundColor White }
function Write-Warn { param([string]$Msg) Write-Host "[WARN] $Msg" -ForegroundColor Yellow }
function Die        { param([string]$Msg)
    Write-Host "[ERROR] $Msg" -ForegroundColor Red
    exit 1
}

# ============================================================
# 0. 环境准备
# ============================================================
Write-Step "QDetectVision 代码覆盖率脚本（gcov）"
Write-Info "项目根目录: $ProjectRoot"
Write-Info "构建目录:   $BuildDir"

if (-not (Test-Path $ProjectRoot)) { Die "项目根目录不存在: $ProjectRoot" }

# 构建并注入 PATH：Qt / MinGW / OpenCV / Python / CMake
$env:PATH = "$QtBin;$MinGWBin;$OpenCVBin;$PythonDir;$CMakeBin;" + $env:PATH

# Qt 测试在无显示器环境下使用 minimal 平台插件，避免弹窗
$env:QT_QPA_PLATFORM = "minimal"

Write-Info "QT_QPA_PLATFORM = $env:QT_QPA_PLATFORM"

# 工具自检
foreach ($tool in @(
    @{ Name = "gcc";   Path = (Join-Path $MinGWBin "gcc.exe") },
    @{ Name = "g++";   Path = (Join-Path $MinGWBin "g++.exe") },
    @{ Name = "cmake"; Path = (Join-Path $CMakeBin "cmake.exe") },
    @{ Name = "gcov";  Path = $GcovExe }
)) {
    if (-not (Test-Path $tool.Path)) { Die "$($tool.Name) 未找到: $($tool.Path)" }
}
Write-Ok "gcc / g++ / cmake / gcov 均就绪"

# ============================================================
# 1. 准备构建目录
# ============================================================
Write-Step "[1/5] 准备构建目录 build_coverage"

if ($CleanBuild -and (Test-Path $BuildDir)) {
    Write-Info "清空旧的 build_coverage 目录..."
    Remove-Item -Recurse -Force $BuildDir
}
New-Item -ItemType Directory -Path $BuildDir -Force | Out-Null
Write-Ok "构建目录就绪: $BuildDir"

# ============================================================
# 2. CMake 配置（启用覆盖率）
# ============================================================
Write-Step "[2/5] CMake 配置（-DENABLE_COVERAGE=ON）"

Push-Location $BuildDir
try {
    & cmake $ProjectRoot -G "MinGW Makefiles" `
        -DCMAKE_BUILD_TYPE=Debug `
        -DCMAKE_C_COMPILER=(Join-Path $MinGWBin "gcc.exe") `
        -DCMAKE_CXX_COMPILER=(Join-Path $MinGWBin "g++.exe") `
        -DENABLE_COVERAGE=ON `
        2>&1 | ForEach-Object { Write-Host $_ }
    if ($LASTEXITCODE -ne 0) { Die "CMake 配置失败（退出码 $LASTEXITCODE）" }
} finally {
    Pop-Location
}
Write-Ok "CMake 配置完成"

# ============================================================
# 3. 构建项目
# ============================================================
Write-Step "[3/5] 构建项目（含 --coverage）"

& cmake --build $BuildDir -j 2>&1 | ForEach-Object { Write-Host $_ }
if ($LASTEXITCODE -ne 0) { Die "构建失败（退出码 $LASTEXITCODE）" }
Write-Ok "构建完成"

if (-not (Test-Path $TestExe)) { Die "测试可执行文件未生成: $TestExe" }

# ============================================================
# 4. 运行测试（生成 .gcda 覆盖率数据）
# ============================================================
Write-Step "[4/5] 运行测试（生成 .gcda 运行期数据）"

# 运行前清理旧的 .gcda 文件，避免历史数据污染本次覆盖率统计
if (-not $KeepGcda) {
    Write-Info "清理旧 .gcda 文件..."
    Get-ChildItem -Path $BuildDir -Recurse -Filter "*.gcda" -File -ErrorAction SilentlyContinue |
        Remove-Item -Force -ErrorAction SilentlyContinue
}

# 清理可能的旧测试产物，避免测试失败
$env:GCOV_PREFIX = $BuildDir

Write-Info "执行: $TestExe"
& $TestExe 2>&1 | ForEach-Object { Write-Host $_ }
$testExit = $LASTEXITCODE
if ($testExit -ne 0) {
    Write-Warn "测试退出码非 0（$testExit），覆盖率数据可能不完整"
} else {
    Write-Ok "全部测试通过"
}

# 统计生成的 .gcda 文件数量
$gcdaFiles = Get-ChildItem -Path $BuildDir -Recurse -Filter "*.gcda" -File -ErrorAction SilentlyContinue
Write-Info "生成 .gcda 文件数: $($gcdaFiles.Count)"

if ($gcdaFiles.Count -eq 0) {
    Die "未生成任何 .gcda 文件，覆盖率分析无法继续。请确认编译期已启用 --coverage。"
}

# ============================================================
# 5. 收集覆盖率数据并生成报告
# ============================================================
Write-Step "[5/5] 收集覆盖率数据（gcov）"

# 检测 lcov / genhtml（Windows MinGW 默认不带，多数情况会走文本摘要分支）
$lcov    = Get-Command lcov -ErrorAction SilentlyContinue
$genhtml = Get-Command genhtml -ErrorAction SilentlyContinue

if ($lcov -and $genhtml) {
    # ---------- 分支 A：lcov + genhtml 生成 HTML 报告 ----------
    Write-Info "检测到 lcov + genhtml，生成 HTML 报告..."

    $lcovTraceFile = Join-Path $BuildDir "coverage.info"

    # 收集覆盖率数据，排除第三方与系统头文件
    & lcov --capture `
        --directory "$BuildDir" `
        --output-file "$lcovTraceFile" `
        --rc lcov_branch_coverage=1 `
        2>&1 | ForEach-Object { Write-Host $_ }

    if ($LASTEXITCODE -ne 0) { Die "lcov 采集失败（退出码 $LASTEXITCODE）" }

    # 过滤掉 third_party / Qt / OpenCV / 测试代码本身
    & lcov --remove "$lcovTraceFile" `
        "*/third_party/*" `
        "*/build_coverage/*" `
        "*/tests/*" `
        "D:/Qt_new/*" `
        "D:/opencv/*" `
        "D:/Program Files/Python314/*" `
        --output-file "$lcovTraceFile" `
        2>&1 | ForEach-Object { Write-Host $_ }

    New-Item -ItemType Directory -Path $CoverageHtmlDir -Force | Out-Null
    & genhtml "$lcovTraceFile" `
        --output-directory "$CoverageHtmlDir" `
        --branch-coverage `
        --title "QDetectVision Coverage" `
        2>&1 | ForEach-Object { Write-Host $_ }

    if ($LASTEXITCODE -ne 0) { Die "genhtml 生成 HTML 报告失败（退出码 $LASTEXITCODE）" }

    Write-Ok "HTML 覆盖率报告已生成: $CoverageHtmlDir\index.html"
    Write-Info "用浏览器打开: file:///$($CoverageHtmlDir.Replace('\','/'))/index.html"

} else {
    # ---------- 分支 B：仅 gcov 文本摘要（Windows 常见情况）----------
    if (-not $lcov)    { Write-Warn "未检测到 lcov，跳过 lcov 流程" }
    if (-not $genhtml) { Write-Warn "未检测到 genhtml，跳过 HTML 生成" }

    Write-Info "使用 gcov 生成逐文件覆盖率摘要（.gcov 文本）..."

    # 对所有 .gcno（每个对应一个源文件）运行 gcov
    $gcnoFiles = Get-ChildItem -Path $BuildDir -Recurse -Filter "*.gcno" -File -ErrorAction SilentlyContinue
    Write-Info "待分析 .gcno 文件数: $($gcnoFiles.Count)"

    # gcov 需要在对象文件所在目录运行，并指向 .gcno
    $processedDirs = @{}
    foreach ($gcno in $gcnoFiles) {
        $objDir = $gcno.DirectoryName
        if ($processedDirs.ContainsKey($objDir)) { continue }
        $processedDirs[$objDir] = $true

        # 仅处理项目源码目录，排除第三方与 autogen 中间产物
        if ($objDir -match "third_party|autogen|_autogen") { continue }

        Push-Location $objDir
        try {
            # 对该目录下所有 .gcno 运行 gcov，-f 强制逐函数，-u 处理无条件分支
            & $GcovExe -f -u *.gcno 2>&1 | ForEach-Object { Write-Verbose $_ }
        } finally {
            Pop-Location
        }
    }

    # 汇总文本报告：从各 .gcov 文件提取行覆盖率摘要
    $summaryLines = @()
    $summaryLines += "QDetectVision 代码覆盖率摘要（gcov 文本模式）"
    $summaryLines += "生成时间: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')"
    $summaryLines += "测试退出码: $testExit"
    $summaryLines += "================================================"
    $summaryLines += ""

    $totalFiles = 0
    $gcovFiles = Get-ChildItem -Path $BuildDir -Recurse -Filter "*.gcov" -File -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -notmatch "third_party|autogen|_autogen" }

    foreach ($gcovFile in $gcovFiles) {
        $totalFiles++
        $lines = Get-Content $gcovFile -ErrorAction SilentlyContinue
        # gcov 文件首行格式: File 'xxx.cpp'
        $fileLine = ($lines | Where-Object { $_ -match "^File:" } | Select-Object -First 1)
        # 末尾摘要行格式: Lines:85.7% of 7
        $lineRate = ($lines | Where-Object { $_ -match "^Lines:" } | Select-Object -First 1)
        $branchRate = ($lines | Where-Object { $_ -match "^Branches:" } | Select-Object -First 1)
        $summaryLines += $fileLine
        $summaryLines += "  $lineRate"
        if ($branchRate) { $summaryLines += "  $branchRate" }
        $summaryLines += ""
    }

    $summaryLines += "================================================"
    $summaryLines += "共分析源文件数: $totalFiles"
    $summaryLines += "提示: 完整逐行报告见各目录下的 .gcov 文件"

    $summaryLines | Out-File -FilePath $CoverageTxt -Encoding utf8
    Write-Ok "文本覆盖率摘要已生成: $CoverageTxt"

    Write-Host ""
    Write-Host "---------- 摘要预览 ----------" -ForegroundColor Yellow
    Get-Content $CoverageTxt | Select-Object -First 60 | ForEach-Object { Write-Host $_ }

    Write-Host ""
    Write-Info "提升建议：安装 lcov + genhtml 可获得 HTML 可视化报告。"
    Write-Info "  MSYS2 安装方式:  pacman -S mingw-w64-x86_64-lcov"
}

# ============================================================
# 完成
# ============================================================
Write-Step "覆盖率流程完成"
Write-Info "构建目录:   $BuildDir"
Write-Info "测试可执行: $TestExe"
if (Test-Path $CoverageTxt)    { Write-Info "文本报告:   $CoverageTxt" }
if (Test-Path $CoverageHtmlDir) { Write-Info "HTML 报告:  $CoverageHtmlDir\index.html" }
Write-Host ""
Write-Host "提示: 覆盖率产物（.gcda/.gcno/.gcov/build_coverage）已在 .gitignore 中忽略。" -ForegroundColor DarkGray
