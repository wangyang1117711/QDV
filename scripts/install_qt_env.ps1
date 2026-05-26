<#
.SYNOPSIS
Q-DetectVision Qt6 开发环境自动化安装脚本

.DESCRIPTION
自动检测并安装Qt6开发环境所需的所有组件，包括：
- Visual Studio Build Tools 2022
- Qt6.5.0 MSVC 2022 64-bit
- OpenCV 4.8.0
- vcpkg 包管理器
- 必要的环境变量配置

.NOTES
此脚本需要管理员权限运行
#>

param(
    [string]$QtVersion = "6.5.0",
    [string]$OpenCVVersion = "4.8.0",
    [string]$InstallRoot = "C:\Qt",
    [string]$VcpkgRoot = "C:\vcpkg"
)

$ErrorActionPreference = "Stop"

function Test-Admin {
    $currentPrincipal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
    return $currentPrincipal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Write-Status {
    param(
        [string]$Message,
        [string]$Type = "INFO"
    )
    $color = switch($Type) {
        "OK" { [ConsoleColor]::Green }
        "ERROR" { [ConsoleColor]::Red }
        "WARN" { [ConsoleColor]::Yellow }
        default { [ConsoleColor]::Cyan }
    }
    Write-Host "[$Type] $Message" -ForegroundColor $color
}

function Test-PathExists {
    param([string]$Path)
    return Test-Path $Path
}

function Install-VSBuildTools {
    Write-Status "检查 Visual Studio Build Tools..."

    $vsPath = "C:\Program Files\Microsoft Visual Studio\2022\BuildTools"
    if (Test-PathExists $vsPath) {
        Write-Status "Visual Studio Build Tools 已安装" "OK"
        return $true
    }

    Write-Status "正在下载 Visual Studio Build Tools..." "WARN"
    $installerPath = "$env:TEMP\vs_buildtools.exe"
    
    try {
        Invoke-WebRequest -Uri "https://aka.ms/vs/17/release/vs_buildtools.exe" -OutFile $installerPath -UseBasicParsing
        Write-Status "安装文件下载完成" "OK"

        Write-Status "正在安装 Visual Studio Build Tools..."
        Start-Process -FilePath $installerPath -ArgumentList "--add Microsoft.VisualStudio.Workload.NativeDesktop --quiet --norestart" -Wait -NoNewWindow
        Write-Status "Visual Studio Build Tools 安装完成" "OK"
        return $true
    } catch {
        Write-Status "Visual Studio Build Tools 安装失败: $_" "ERROR"
        return $false
    } finally {
        if (Test-PathExists $installerPath) {
            Remove-Item $installerPath -Force
        }
    }
}

function Install-Qt {
    Write-Status "检查 Qt6..."

    $qtPath = "$InstallRoot\$QtVersion\msvc2022_64"
    if (Test-PathExists $qtPath) {
        Write-Status "Qt6 $QtVersion 已安装" "OK"
        return $true
    }

    Write-Status "Qt6 $QtVersion 未安装，请手动安装" "WARN"
    Write-Status "下载地址: https://www.qt.io/download-open-source" "INFO"
    Write-Status "请安装组件: Qt $QtVersion -> MSVC 2022 64-bit" "INFO"
    return $false
}

function Install-Vcpkg {
    Write-Status "检查 vcpkg..."

    if (Test-PathExists $VcpkgRoot) {
        Write-Status "vcpkg 已安装" "OK"
        return $true
    }

    Write-Status "正在安装 vcpkg..."
    try {
        git clone https://github.com/microsoft/vcpkg.git $VcpkgRoot
        & "$VcpkgRoot\bootstrap-vcpkg.bat"
        Write-Status "vcpkg 安装完成" "OK"
        return $true
    } catch {
        Write-Status "vcpkg 安装失败: $_" "ERROR"
        return $false
    }
}

function Install-OpenCV {
    Write-Status "检查 OpenCV..."

    $opencvDll = "$VcpkgRoot\installed\x64-windows\bin\opencv_world$($OpenCVVersion.Replace('.', '')).dll"
    if (Test-PathExists $opencvDll) {
        Write-Status "OpenCV $OpenCVVersion 已安装" "OK"
        return $true
    }

    Write-Status "正在通过 vcpkg 安装 OpenCV..."
    try {
        & "$VcpkgRoot\vcpkg.exe" install opencv4:x64-windows --triplet x64-windows
        Write-Status "OpenCV 安装完成" "OK"
        return $true
    } catch {
        Write-Status "OpenCV 安装失败: $_" "ERROR"
        return $false
    }
}

function Install-Dependencies {
    Write-Status "安装其他依赖..."

    try {
        & "$VcpkgRoot\vcpkg.exe" install nlohmann-json:x64-windows spdlog:x64-windows catch2:x64-windows --triplet x64-windows
        & "$VcpkgRoot\vcpkg.exe" integrate install
        Write-Status "依赖安装完成" "OK"
        return $true
    } catch {
        Write-Status "依赖安装失败: $_" "ERROR"
        return $false
    }
}

function Configure-Environment {
    Write-Status "配置环境变量..."

    $qtPath = "$InstallRoot\$QtVersion\msvc2022_64"
    
    # 设置用户环境变量
    [Environment]::SetEnvironmentVariable("QTDIR", $qtPath, "User")
    [Environment]::SetEnvironmentVariable("OPENCV_DIR", "$VcpkgRoot\installed\x64-windows", "User")
    [Environment]::SetEnvironmentVariable("VCPKG_ROOT", $VcpkgRoot, "User")

    # 更新 PATH
    $currentPath = [Environment]::GetEnvironmentVariable("PATH", "User")
    $pathsToAdd = @(
        "$qtPath\bin",
        "$VcpkgRoot\installed\x64-windows\bin",
        "$VcpkgRoot"
    )

    foreach ($path in $pathsToAdd) {
        if (-not $currentPath.Contains($path)) {
            $currentPath = "$path;$currentPath"
        }
    }
    [Environment]::SetEnvironmentVariable("PATH", $currentPath, "User")

    Write-Status "环境变量配置完成" "OK"
    return $true
}

function Test-Environment {
    Write-Status "验证开发环境..."

    $success = $true

    # 检查 Qt
    try {
        $qmake = & qmake --version 2>&1
        if ($LASTEXITCODE -eq 0) {
            Write-Status "Qt 版本: $($qmake[0])" "OK"
        } else {
            Write-Status "Qt 未正确配置" "ERROR"
            $success = $false
        }
    } catch {
        Write-Status "Qt 未找到" "ERROR"
        $success = $false
    }

    # 检查 CMake
    try {
        $cmake = & cmake --version
        Write-Status "CMake 版本: $($cmake[0])" "OK"
    } catch {
        Write-Status "CMake 未找到" "ERROR"
        $success = $false
    }

    return $success
}

function Compile-Project {
    Write-Status "编译 Q-DetectVision 项目..."

    $projectRoot = "E:\anchor\Trae\QDV"
    $buildDir = "$projectRoot\build"

    if (-not (Test-PathExists $projectRoot)) {
        Write-Status "项目目录不存在: $projectRoot" "ERROR"
        return $false
    }

    try {
        # 创建构建目录
        if (-not (Test-PathExists $buildDir)) {
            New-Item -ItemType Directory -Path $buildDir | Out-Null
        }

        # 配置 CMake
        Write-Status "配置 CMake..."
        Push-Location $buildDir
        & cmake .. -G "Visual Studio 17 2022" -A x64 `
            -DCMAKE_TOOLCHAIN_FILE="$VcpkgRoot\scripts\buildsystems\vcpkg.cmake" `
            -DCMAKE_PREFIX_PATH="$InstallRoot\$QtVersion\msvc2022_64"
        
        if ($LASTEXITCODE -ne 0) {
            Write-Status "CMake 配置失败" "ERROR"
            Pop-Location
            return $false
        }

        # 编译项目
        Write-Status "编译项目..."
        & cmake --build . --config Release -j 8

        if ($LASTEXITCODE -ne 0) {
            Write-Status "编译失败" "ERROR"
            Pop-Location
            return $false
        }

        Write-Status "编译成功" "OK"
        Pop-Location
        return $true
    } catch {
        Write-Status "编译异常: $_" "ERROR"
        Pop-Location
        return $false
    }
}

function Test-Application {
    Write-Status "测试应用程序..."

    $appPath = "E:\anchor\Trae\QDV\build\bin\Release\Q-DetectVision.exe"
    
    if (-not (Test-PathExists $appPath)) {
        Write-Status "应用程序不存在: $appPath" "ERROR"
        return $false
    }

    Write-Status "应用程序路径: $appPath" "OK"
    Write-Status "运行时测试: 需要手动启动验证" "INFO"
    return $true
}

# 主程序
Write-Host "`n==============================================" -ForegroundColor Cyan
Write-Host "    Q-DetectVision Qt6 开发环境安装脚本" -ForegroundColor Cyan
Write-Host "==============================================`n" -ForegroundColor Cyan

# 检查管理员权限
if (-not (Test-Admin)) {
    Write-Status "需要管理员权限运行此脚本" "ERROR"
    exit 1
}

# 安装步骤
$steps = @(
    @{ Name = "Visual Studio Build Tools"; Func = "Install-VSBuildTools" },
    @{ Name = "Qt6"; Func = "Install-Qt" },
    @{ Name = "vcpkg"; Func = "Install-Vcpkg" },
    @{ Name = "OpenCV"; Func = "Install-OpenCV" },
    @{ Name = "其他依赖"; Func = "Install-Dependencies" },
    @{ Name = "环境变量配置"; Func = "Configure-Environment" },
    @{ Name = "环境验证"; Func = "Test-Environment" },
    @{ Name = "项目编译"; Func = "Compile-Project" },
    @{ Name = "应用测试"; Func = "Test-Application" }
)

$failedSteps = @()

foreach ($step in $steps) {
    Write-Host "`n--- $($step.Name) ---" -ForegroundColor Yellow
    $result = & $step.Func
    if (-not $result) {
        $failedSteps += $step.Name
    }
}

# 总结
Write-Host "`n==============================================" -ForegroundColor Cyan
Write-Host "              安装结果总结" -ForegroundColor Cyan
Write-Host "==============================================" -ForegroundColor Cyan

if ($failedSteps.Count -eq 0) {
    Write-Status "所有步骤完成成功！" "OK"
    Write-Host "`n下一步操作:" -ForegroundColor Yellow
    Write-Host "1. 重启终端或运行: refreshenv"
    Write-Host "2. 进入项目目录: cd E:\anchor\Trae\QDV"
    Write-Host "3. 运行应用: .\build\bin\Release\Q-DetectVision.exe"
} else {
    Write-Status "以下步骤失败:" "ERROR"
    foreach ($step in $failedSteps) {
        Write-Host "  - $step" -ForegroundColor Red
    }
    Write-Host "`n请手动完成失败的步骤后再继续" -ForegroundColor Yellow
    exit 1
}