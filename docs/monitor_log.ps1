# JyTrainer Log Monitor
# 用法：在 PowerShell 中執行
# .\docs\monitor_log.ps1

param(
    [string]$LogFile = "RanBot_Trainer.log",
    [int]$Tail = 50,
    [switch]$Watch,
    [switch]$FsmOnly,
    [switch]$ErrorOnly
)

$ErrorActionPreference = "Continue"

function Write-FilteredLog {
    param([string]$Content, [string]$Filter)

    $lines = $Content -split "`n"
    foreach ($line in $lines) {
        if ($line -match $Filter) {
            # 著色輸出
            if ($line -match "ERROR|❌|FAIL") {
                Write-Host $line -ForegroundColor Red
            } elseif ($line -match "WARN|⚠️|超時") {
                Write-Host $line -ForegroundColor Yellow
            } elseif ($line -match "SUCCESS|✅|完成") {
                Write-Host $line -ForegroundColor Green
            } elseif ($line -match "狀態機|StateMachine|->") {
                Write-Host $line -ForegroundColor Cyan
            } elseif ($line -match "Watchdog") {
                Write-Host $line -ForegroundColor Magenta
            } else {
                Write-Host $line -ForegroundColor Gray
            }
        }
    }
}

# 設定過濾器
$filter = ".*"
if ($ErrorOnly) {
    $filter = "ERROR|❌|FAIL|IM_ASSERT|Watchdog"
} elseif ($FsmOnly) {
    $filter = "狀態機|StateMachine|Watchdog|STOP|RECOVERY|DEAD|HUNTING|RETURNING"
}

Write-Host "========================================" -ForegroundColor White
Write-Host " JyTrainer Log Monitor" -ForegroundColor Cyan
Write-Host " File: $LogFile" -ForegroundColor Gray
Write-Host " Filter: $filter" -ForegroundColor Gray
Write-Host "========================================" -ForegroundColor White
Write-Host ""

if (-not (Test-Path $LogFile)) {
    Write-Host "[ERROR] Log file not found: $LogFile" -ForegroundColor Red
    Write-Host "Please run from the same directory as JyTrainer.exe" -ForegroundColor Yellow
    exit 1
}

if ($Watch) {
    Write-Host "[INFO] Watching log file (Ctrl+C to stop)..." -ForegroundColor Green
    Write-Host ""

    $lastSize = (Get-Item $LogFile).Length

    while ($true) {
        Start-Sleep -Milliseconds 500

        $currentSize = (Get-Item $LogFile).Length

        if ($currentSize -gt $lastSize) {
            $bytes = Get-Content $LogFile -Raw -Encoding Byte
            $stream = [System.IO.MemoryStream]::new($bytes, $lastSize, $currentSize - $lastSize)
            $reader = [System.IO.StreamReader]::new($stream)

            $newContent = $reader.ReadToEnd()
            $reader.Close()
            $stream.Close()

            if ($newContent -match $filter) {
                Write-FilteredLog -Content $newContent -Filter $filter
            }

            $lastSize = $currentSize
        }
    }
} else {
    # 一次性輸出
    $content = Get-Content $LogFile -Tail $Tail -Encoding UTF8
    $content | ForEach-Object {
        if ($_ -match $filter) {
            if ($_ -match "ERROR|❌|FAIL") {
                Write-Host $_ -ForegroundColor Red
            } elseif ($_ -match "WARN|⚠️|超時") {
                Write-Host $_ -ForegroundColor Yellow
            } elseif ($_ -match "SUCCESS|✅|完成") {
                Write-Host $_ -ForegroundColor Green
            } elseif ($_ -match "狀態機|StateMachine|->") {
                Write-Host $_ -ForegroundColor Cyan
            } elseif ($_ -match "Watchdog") {
                Write-Host $_ -ForegroundColor Magenta
            } else {
                Write-Host $_ -ForegroundColor Gray
            }
        }
    }
}