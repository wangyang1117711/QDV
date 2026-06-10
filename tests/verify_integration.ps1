# QDV Integration Verification Script
# Usage: powershell -File verify_integration.ps1

$ProjectRoot = (Get-Item (Split-Path -Parent $MyInvocation.MyCommand.Path)).Parent.FullName
$totalPassed = 0
$totalFailed = 0

function Check($name, $condition, $detail) {
    Write-Host "[CHECK] $name ... " -NoNewline
    if ($condition) {
        Write-Host "PASS" -ForegroundColor Green
        $script:totalPassed++
    } else {
        Write-Host "FAIL" -ForegroundColor Red
        if ($detail) { Write-Host "  $detail" -ForegroundColor Red }
        $script:totalFailed++
    }
}

function FileExists($path) { Test-Path $path }
function FileContains($path, $pattern) {
    if (-not (Test-Path $path)) { return $false }
    (Get-Content $path -Raw) -match $pattern
}

Write-Host "`n========================================" -ForegroundColor Cyan
Write-Host "  QDV Integration Verification" -ForegroundColor Cyan
Write-Host "========================================`n" -ForegroundColor Cyan

# --- Build System ---
$cmake = Get-Content "$ProjectRoot\CMakeLists.txt" -Raw
$modules = @("Core", "AI", "UI", "TrainingInference", "Vision", "Communication", "Database", "Plugins")
foreach ($m in $modules) {
    Check "CMake registers src/$m" ($cmake -match "add_subdirectory\(src/$m\)") ""
}
Check "CMake registers tests" ($cmake -match "add_subdirectory\(tests\)") ""
Check "C++ Standard = 17" ($cmake -match 'CMAKE_CXX_STANDARD 17') ""

$svCmake = Get-Content "$ProjectRoot\apps\SmartVision\CMakeLists.txt" -Raw
foreach ($l in $modules) {
    Check "SmartVision links $l" ($svCmake -match $l) ""
}

# --- Security ---
$authCpp = Get-Content "$ProjectRoot\src\Core\AuthService.cpp" -Raw
$loginCpp = Get-Content "$ProjectRoot\src\UI\LoginView.cpp" -Raw
$mainCpp = Get-Content "$ProjectRoot\src\UI\MainWindow.cpp" -Raw

Check "No hardcoded password" ($authCpp -notmatch 'admin123') ""
Check "Token-based remember-me" ($loginCpp -match 'login/token') ""
Check "No Base64 password" ($loginCpp -notmatch 'login/password') ""
Check "PBKDF2 with iterations" ($authCpp -match '100000') ""
Check "Salt generation" ($authCpp -match 'generateSalt') ""
Check "First-run admin setup" ($mainCpp -match 'showFirstRunSetup') ""

# --- Mock Data ---
$allowedFiles = @("AuthService.cpp", "test_main.cpp", "catch2_minimal.hpp")
$foundMock = $false
$cppFiles = Get-ChildItem -Path "$ProjectRoot\src" -Recurse -Include *.cpp
foreach ($file in $cppFiles) {
    $isAllowed = $false
    foreach ($af in $allowedFiles) { if ($file.Name -eq $af) { $isAllowed = $true; break } }
    if (-not $isAllowed) {
        if ((Get-Content $file.FullName -Raw) -match 'QRandomGenerator') {
            $foundMock = $true
            break
        }
    }
}
Check "No QRandomGenerator in main code" (-not $foundMock) "QRandomGenerator found outside AuthService/tests"

# --- Data Flow ---
Check "DetectionStats defined" (FileExists "$ProjectRoot\include\Core\DetectionStats.h") ""
Check "DB integrator defined" (FileExists "$ProjectRoot\include\Database\DatabaseIntegrator.h") ""
$monitorCpp = Get-Content "$ProjectRoot\src\UI\MonitorView.cpp" -Raw
Check "MonitorView uses updateStats" ($monitorCpp -match 'updateStats') ""
Check "MonitorView uses DatabaseIntegrator" ($monitorCpp -match 'DatabaseIntegrator') ""
$centralCpp = Get-Content "$ProjectRoot\src\UI\CentralWindow.cpp" -Raw
Check "CentralWindow bridges detection" ($centralCpp -match 'onDetectionResult') ""

# --- Inference Engine ---
$engineCpp = Get-Content "$ProjectRoot\src\AI\InferenceEngine.cpp" -Raw
Check "Uses cv::dnn::readNetFromONNX" ($engineCpp -match 'cv::dnn::readNetFromONNX') ""
Check "Uses m_net.forward()" ($engineCpp -match 'm_net\.forward') ""
Check "Has preprocess pipeline" ($engineCpp -match 'preprocess') ""
Check "Has postprocess pipeline" ($engineCpp -match 'postprocess') ""
Check "Has warmUp method" ($engineCpp -match 'warmUp') ""
$mgrCpp = Get-Content "$ProjectRoot\src\AI\ModelManager.cpp" -Raw
Check "Has LRU eviction" ($mgrCpp -match 'evictLRU') ""

# --- Logger Performance ---
$loggerH = Get-Content "$ProjectRoot\include\Core\Logger.h" -Raw
Check "Logger has buffer queue" ($loggerH -match 'm_buffer') ""
Check "Logger has flush threshold" ($loggerH -match 'BUFFER_FLUSH_SIZE') ""
Check "Logger has file rotation" ($loggerH -match 'rotateIfNeeded') ""
Check "Logger has old log cleanup" ($loggerH -match 'cleanupOldLogs') ""
Check "Logger has flush timer" ($loggerH -match 'm_flushTimer') ""

# --- Architecture ---
$coreCmake = Get-Content "$ProjectRoot\src\Core\CMakeLists.txt" -Raw
Check "Core no longer links Widgets" ($coreCmake -notmatch 'Qt6::Widgets') ""

$schemeH = Get-Content "$ProjectRoot\include\Core\Scheme.h" -Raw
Check "Scheme uses unique_ptr" ($schemeH -match 'unique_ptr') ""

# --- Test Framework ---
Check "Test CMakeLists exists" (FileExists "$ProjectRoot\tests\CMakeLists.txt") ""
Check "Test main exists" (FileExists "$ProjectRoot\tests\test_main.cpp") ""
$testFiles = @(
    "Core/test_scheme.cpp", "Core/test_auth.cpp", "Core/test_branch_node.cpp",
    "Vision/test_tools.cpp",
    "Communication/test_tcp.cpp", "Communication/test_serial.cpp",
    "Database/test_database.cpp",
    "AI/test_inference.cpp"
)
foreach ($tf in $testFiles) {
    Check "Test file: $tf" (FileExists "$ProjectRoot\tests\$tf") ""
}

# --- Vision Tools ---
Check "ToolChainVerifier exists" (FileExists "$ProjectRoot\include\Vision\ToolChainVerifier.h") ""
Check "ToolChainVerifier impl" (FileExists "$ProjectRoot\src\Vision\ToolChainVerifier.cpp") ""

# --- Communication ---
Check "CommVerifier exists" (FileExists "$ProjectRoot\include\Communication\CommunicationVerifier.h") ""
Check "CommVerifier impl" (FileExists "$ProjectRoot\src\Communication\CommunicationVerifier.cpp") ""

# --- Plugins ---
Check "TestPlugin exists" (FileExists "$ProjectRoot\include\Plugins\TestPlugin.h") ""
Check "PluginVerifier exists" (FileExists "$ProjectRoot\include\Plugins\PluginVerifier.h") ""

# --- Summary ---
$total = $totalPassed + $totalFailed
Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  TOTAL: $total | PASSED: $totalPassed | FAILED: $totalFailed" -ForegroundColor $(if ($totalFailed -eq 0) { "Green" } else { "Yellow" })
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

if ($totalFailed -gt 0) {
    Write-Host "Some checks FAILED. Review details above." -ForegroundColor Red
    exit 1
} else {
    Write-Host "All integration checks PASSED!" -ForegroundColor Green
    exit 0
}