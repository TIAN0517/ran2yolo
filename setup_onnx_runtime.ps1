# ============================================================
# ONNX Runtime v1.12.1 下載腳本 (x86 + x64)
# 支援 Windows 7 x86 / Windows 11 x64
# ============================================================
param(
    [string]$Arch = "x86"  # x86 或 x64
)

$ErrorActionPreference = "Stop"
$VERSION = "1.12.1"
$DOWNLOAD_URL = "https://github.com/microsoft/onnxruntime/releases/download/v${VERSION}/onnxruntime-${VERSION}-win-${Arch}.zip"
$ZIP_NAME = "onnxruntime-${VERSION}-win-${Arch}.zip"
$SCRIPT_DIR = Split-Path -Parent $MyInvocation.MyCommand.Path
$PROJECT_DIR = $SCRIPT_DIR

Write-Host "========================================" -ForegroundColor Cyan
Write-Host " ONNX Runtime ${VERSION} ${Arch} Setup" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan

$ONNX_DIR = Join-Path $PROJECT_DIR "onnxruntime-${Arch}"
Write-Host "目標目錄: $ONNX_DIR" -ForegroundColor Yellow

# 建立目錄
if (-not (Test-Path $ONNX_DIR)) {
    New-Item -ItemType Directory -Path $ONNX_DIR -Force | Out-Null
}

# DLL 目標路徑
$TARGET_DLL = Join-Path $PROJECT_DIR "onnxruntime.dll"

# 檢查是否已存在
if (Test-Path $TARGET_DLL) {
    $existing = Get-Item $TARGET_DLL
    Write-Host "[INFO] ONNX Runtime DLL 已存在: $TARGET_DLL" -ForegroundColor Green
    Write-Host "       大小: $([math]::Round($existing.Length/1MB, 2)) MB"
    Write-Host ""
    Write-Host "如果要使用 ${Arch} 版本，請刪除現有 DLL 並重新執行此腳本" -ForegroundColor Yellow
    Write-Host "或手動複製 ${ONNX_DIR}\onnxruntime.dll 到 $TARGET_DLL" -ForegroundColor Yellow
    exit 0
}

Write-Host ""
Write-Host "下載 URL: $DOWNLOAD_URL" -ForegroundColor Yellow
Write-Host ""

# 下載
Write-Host "正在下載 ONNX Runtime ${Arch}..." -ForegroundColor Cyan
try {
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    Invoke-WebRequest -Uri $DOWNLOAD_URL -OutFile $ZIP_NAME -UseBasicParsing -TimeoutSec 300
    Write-Host "[OK] 下載完成" -ForegroundColor Green
} catch {
    Write-Host "[ERROR] 下載失敗: $_" -ForegroundColor Red
    Write-Host ""
    Write-Host "請手動下載:" -ForegroundColor Yellow
    Write-Host "  x86: https://github.com/microsoft/onnxruntime/releases/download/v${VERSION}/onnxruntime-${VERSION}-win-x86.zip" -ForegroundColor Yellow
    Write-Host "  x64: https://github.com/microsoft/onnxruntime/releases/download/v${VERSION}/onnxruntime-${VERSION}-win-x64.zip" -ForegroundColor Yellow
    exit 1
}

# 解壓
Write-Host "正在解壓..." -ForegroundColor Cyan
try {
    Expand-Archive -Path $ZIP_NAME -DestinationPath $ONNX_DIR -Force

    # 移動檔案
    $EXTRACTED_DIR = Join-Path $ONNX_DIR "onnxruntime-${VERSION}-win-${Arch}"
    if (Test-Path $EXTRACTED_DIR) {
        $src_dll = Join-Path $EXTRACTED_DIR "onnxruntime.dll"
        $src_dll2 = Join-Path $EXTRACTED_DIR "onnxruntime_providers_shared.dll"

        if (Test-Path $src_dll) {
            Copy-Item -Path $src_dll -Destination $PROJECT_DIR -Force
        }
        if (Test-Path $src_dll2) {
            Copy-Item -Path $src_dll2 -Destination $PROJECT_DIR -Force
        }
    }

    # 刪除 ZIP
    Remove-Item -Path $ZIP_NAME -Force

    Write-Host "[OK] 解壓完成" -ForegroundColor Green
} catch {
    Write-Host "[ERROR] 解壓失敗: $_" -ForegroundColor Red
    exit 1
}

# 驗證
Write-Host ""
Write-Host "驗證安裝..." -ForegroundColor Cyan
if (Test-Path $TARGET_DLL) {
    $dllInfo = Get-Item $TARGET_DLL
    $archStr = "x86"
    if ($dllInfo.Length -gt 12000000) { $archStr = "x64" }
    Write-Host "[OK] ONNX Runtime DLL: $TARGET_DLL" -ForegroundColor Green
    Write-Host "     大小: $([math]::Round($dllInfo.Length/1MB, 2)) MB ($archStr)" -ForegroundColor Green
} else {
    Write-Host "[ERROR] DLL 未找到" -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "========================================" -ForegroundColor Green
Write-Host " ONNX Runtime ${Arch} 安裝完成!" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Green
