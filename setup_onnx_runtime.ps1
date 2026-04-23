# ============================================================
# ONNX Runtime v1.12.1 x86 下載腳本
# 支援 Windows 7 x86
# ============================================================
# 使用方式:
#   powershell -ExecutionPolicy Bypass -File setup_onnx_runtime.ps1
# ============================================================

$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

$VERSION = "1.12.1"
$ARCH = "win-x86"
$DOWNLOAD_URL = "https://github.com/microsoft/onnxruntime/releases/download/v${VERSION}/onnxruntime-${VERSION}-${ARCH}.zip"
$ZIP_NAME = "onnxruntime-${VERSION}-${ARCH}.zip"
$SCRIPT_DIR = Split-Path -Parent $MyInvocation.MyCommand.Path
$PROJECT_DIR = $SCRIPT_DIR
$ONNX_DIR = Join-Path $PROJECT_DIR "onnxruntime"

Write-Host "========================================" -ForegroundColor Cyan
Write-Host " ONNX Runtime ${VERSION} x86 Setup" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan

# 建立目錄
if (-not (Test-Path $ONNX_DIR)) {
    New-Item -ItemType Directory -Path $ONNX_DIR | Out-Null
}
$ONNX_INCLUDE = Join-Path $ONNX_DIR "include"
$ONNX_LIB = Join-Path $ONNX_DIR "lib"

if (-not (Test-Path $ONNX_INCLUDE)) {
    New-Item -ItemType Directory -Path $ONNX_INCLUDE | Out-Null
}
if (-not (Test-Path $ONNX_LIB)) {
    New-Item -ItemType Directory -Path $ONNX_LIB | Out-Null
}

# 檢查是否已存在
$DLL_PATH = Join-Path $ONNX_DIR "onnxruntime.dll"
if (Test-Path $DLL_PATH) {
    Write-Host "[OK] ONNX Runtime DLL 已存在: $DLL_PATH" -ForegroundColor Green
    Write-Host "跳過下載。如需重新下載，請先刪除 onnxruntime 目錄。"
    exit 0
}

Write-Host ""
Write-Host "下載 URL: $DOWNLOAD_URL" -ForegroundColor Yellow
Write-Host "目標目錄: $ONNX_DIR" -ForegroundColor Yellow
Write-Host ""

# 下載
Write-Host "正在下載 ONNX Runtime..." -ForegroundColor Cyan
try {
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    Invoke-WebRequest -Uri $DOWNLOAD_URL -OutFile $ZIP_NAME -UseBasicParsing -TimeoutSec 300
    Write-Host "[OK] 下載完成" -ForegroundColor Green
} catch {
    Write-Host "[ERROR] 下載失敗: $_" -ForegroundColor Red
    Write-Host "請手動下載並解壓到 $ONNX_DIR" -ForegroundColor Yellow
    Write-Host "URL: $DOWNLOAD_URL" -ForegroundColor Yellow
    exit 1
}

# 解壓
Write-Host "正在解壓..." -ForegroundColor Cyan
try {
    Expand-Archive -Path $ZIP_NAME -DestinationPath $ONNX_DIR -Force

    # 移動檔案
    $EXTRACTED_DIR = Join-Path $ONNX_DIR "onnxruntime-${VERSION}-${ARCH}"
    if (Test-Path $EXTRACTED_DIR) {
        # 移動 include 和 lib
        $src_include = Join-Path $EXTRACTED_DIR "include"
        $src_lib = Join-Path $EXTRACTED_DIR "lib"
        $src_dll = Join-Path $EXTRACTED_DIR "onnxruntime.dll"

        if (Test-Path $src_include) {
            Copy-Item -Path "$src_include\*" -Destination $ONNX_INCLUDE -Recurse -Force
        }
        if (Test-Path $src_lib) {
            Copy-Item -Path "$src_lib\*" -Destination $ONNX_LIB -Recurse -Force
        }
        if (Test-Path $src_dll) {
            Copy-Item -Path $src_dll -Destination $ONNX_DIR -Force
        }

        # 刪除臨時目錄
        Remove-Item -Path $EXTRACTED_DIR -Recurse -Force
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
$header = Join-Path $ONNX_INCLUDE "onnxruntime_c_api.h"
$lib = Join-Path $ONNX_LIB "onnxruntime.lib"
$DLL = Join-Path $ONNX_DIR "onnxruntime.dll"

$allOk = $true
if (Test-Path $header) {
    Write-Host "[OK] Header: $header" -ForegroundColor Green
} else {
    Write-Host "[ERROR] Header not found: $header" -ForegroundColor Red
    $allOk = $false
}

if (Test-Path $lib) {
    Write-Host "[OK] Library: $lib" -ForegroundColor Green
} else {
    Write-Host "[ERROR] Library not found: $lib" -ForegroundColor Red
    $allOk = $false
}

if (Test-Path $DLL) {
    $dllInfo = Get-Item $DLL
    Write-Host "[OK] DLL: $DLL (Size: $([math]::Round($dllInfo.Length/1MB, 2)) MB)" -ForegroundColor Green
} else {
    Write-Host "[ERROR] DLL not found: $DLL" -ForegroundColor Red
    $allOk = $false
}

Write-Host ""
if ($allOk) {
    Write-Host "========================================" -ForegroundColor Green
    Write-Host " ONNX Runtime 安裝完成!" -ForegroundColor Green
    Write-Host "========================================" -ForegroundColor Green
    Write-Host ""
    Write-Host "下一步:" -ForegroundColor Yellow
    Write-Host "  1. 在 Visual Studio 中開啟 JyTrainer.sln"
    Write-Host "  2. 確保 onnxruntime\include 在 Include 目錄中"
    Write-Host "  3. 確保 onnxruntime\lib 在 Library 目錄中"
    Write-Host "  4. 重新編譯專案"
    Write-Host ""
    Write-Host "模型路徑: models/best.onnx" -ForegroundColor Cyan
} else {
    Write-Host "[ERROR] 安裝驗證失敗" -ForegroundColor Red
    exit 1
}
