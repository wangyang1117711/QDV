# Q-DetectVision Test Suite

param(
    [string]$ReportPath = "E:\anchor\Trae\QDV\test_report_$(Get-Date -Format 'yyyyMMdd').txt"
)

$results = @()

function Test-Item {
    param($Name, $Test)
    try {
        & $Test
        $results += [PSCustomObject]@{Name=$Name; Status='PASS'; Details=''}
        Write-Host "PASS: $Name" -ForegroundColor Green
    } catch {
        $results += [PSCustomObject]@{Name=$Name; Status='FAIL'; Details=$_.Exception.Message}
        Write-Host "FAIL: $Name - $_" -ForegroundColor Red
    }
}

Write-Host "`n=== Q-DetectVision Function Tests ===`n"

# Test 1: Main executable exists
Test-Item 'Main executable exists' {
    if (-not (Test-Path 'E:\anchor\Trae\QDV\build_qt\bin\QDetectVision.exe')) {
        throw 'Executable not found'
    }
}

# Test 2: Qt DLLs
Test-Item 'Qt6Core.dll' {
    if (-not (Test-Path 'E:\anchor\Trae\QDV\build_qt\bin\Qt6Core.dll')) {
        throw 'Missing Qt6Core.dll'
    }
}

Test-Item 'Qt6Gui.dll' {
    if (-not (Test-Path 'E:\anchor\Trae\QDV\build_qt\bin\Qt6Gui.dll')) {
        throw 'Missing Qt6Gui.dll'
    }
}

Test-Item 'Qt6Widgets.dll' {
    if (-not (Test-Path 'E:\anchor\Trae\QDV\build_qt\bin\Qt6Widgets.dll')) {
        throw 'Missing Qt6Widgets.dll'
    }
}

# Test 3: Platform plugin
Test-Item 'Platform plugin qwindows.dll' {
    if (-not (Test-Path 'E:\anchor\Trae\QDV\build_qt\bin\platforms\qwindows.dll')) {
        throw 'Missing qwindows.dll'
    }
}

# Test 4: Application launch
Test-Item 'Application launch test' {
    $proc = Start-Process 'E:\anchor\Trae\QDV\build_qt\bin\QDetectVision.exe' -Wait -PassThru -NoNewWindow
    if ($proc.ExitCode -ne 0) {
        throw "Exit code: $($proc.ExitCode)"
    }
}

# Test 5: Project structure
Test-Item 'Core source files' {
    if (-not (Test-Path 'E:\anchor\Trae\QDV\src\Core')) {
        throw 'Core directory missing'
    }
}

Test-Item 'UI source files' {
    if (-not (Test-Path 'E:\anchor\Trae\QDV\src\UI')) {
        throw 'UI directory missing'
    }
}

# Summary
$pass = ($results | Where-Object {$_.Status -eq 'PASS'}).Count
$fail = ($results | Where-Object {$_.Status -eq 'FAIL'}).Count
$total = $results.Count

Write-Host "`n=== Test Summary ==="
Write-Host "Total: $total"
Write-Host "Passed: $pass" -ForegroundColor Green
Write-Host "Failed: $fail" -ForegroundColor Red
Write-Host "Pass Rate: $([math]::Round(($pass/$total)*100,1))%"

$results | Format-Table Name, Status, Details -AutoSize

# Generate report
$report = @"
Q-DetectVision Test Report
=========================
Date: $(Get-Date)
Environment: Windows + Qt6.11.1 + MinGW

Summary:
- Total Tests: $total
- Passed: $pass
- Failed: $fail
- Pass Rate: $([math]::Round(($pass/$total)*100,1))%

Details:
"@

foreach ($r in $results) {
    $report += "`n$($r.Status): $($r.Name)"
    if ($r.Details) {
        $report += " - $($r.Details)"
    }
}

$report | Out-File $ReportPath -Encoding UTF8
Write-Host "`nReport saved to: $ReportPath"