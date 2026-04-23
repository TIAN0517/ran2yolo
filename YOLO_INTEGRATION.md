# YOLO 整合指南 (JyTrainer_Win11)

本指南說明 YOLO 物件偵測如何整合到 JyTrainer。

## 已實作功能 ✓

### 1. YoloDetector 類別
- **yolo_detector.h/cpp** - ONNX Runtime C API 封裝
  - 模型載入
  - 影像前處理 (BGRA → NCHW)
  - YOLOv8 輸出解析
  - NMS 後處理
  - 轉換為 VisualMonster

### 2. visionentity.cpp 整合
- YOLO 偵測器單例模式
- `ScanVisualMonsters()` 自動優先使用 YOLO
- YOLO 失敗時自動降級到像素掃描

### 3. bot_logic.cpp 整合
- 程式啟動時初始化 YOLO 偵測器
- 程式關閉時釋放資源
- 推論時間日誌
- YOLO 模式開關與像素模式互斥

---

## 檔案結構

```
JyTrainer_Win11/
├── yolo_detector.h        # YoloDetector 類別 (5KB)
├── yolo_detector.cpp      # ONNX Runtime 封裝 (22KB)
├── onnx_config.h          # ONNX Runtime 配置
├── visionentity.h         # 更新: 宣告 YOLO 函式
├── visionentity.cpp       # 更新: YOLO 整合
├── bot_logic.h            # 更新: YOLO 設定存取
├── bot_logic.cpp          # 更新: YOLO 初始化/整合
├── setup_onnx_runtime.ps1 # ONNX Runtime 安裝腳本
└── JyTrainer.vcxproj     # 更新: USE_YOLO_DETECTION
```

---

## 安裝步驟

### 1. 下載 ONNX Runtime v1.12.1 x86

```powershell
cd ~/Desktop/BossJy/JyTrainer_Win11
powershell -ExecutionPolicy Bypass -File setup_onnx_runtime.ps1
```

或手動下載:
- URL: https://github.com/microsoft/onnxruntime/releases/download/v1.12.1/onnxruntime-1.12.1-win-x86.zip
- 解壓到 `onnxruntime/`

### 2. 確認目錄結構

```
onnxruntime/
├── include/
│   └── onnxruntime_c_api.h
├── lib/
│   └── onnxruntime.lib
└── onnxruntime.dll
```

### 3. 確認模型存在

```
models/best.onnx
```

### 4. 編譯

```batch
cd ~/Desktop/BossJy/JyTrainer_Win11
_build.bat
```

---

## 使用方式

### 自動模式（預設）

當 `use_yolo_mode=true` 或 `use_visual_mode=true` 時：
- `ScanVisualMonsters()` 自動優先使用 YOLO
- YOLO 失敗時自動降級到像素掃描

### 手動模式

```cpp
#include "visionentity.h"

// 檢查 YOLO 是否就緒
if (IsYoloReady()) {
    // YOLO 可用
}

// 設定閾值
SetYoloThresholds(0.5f, 0.45f);

// 掃描怪物
VisualMonster monsters[32];
int count = ScanVisualMonsters(hWnd, monsters, 32);
```

---

## 設定檔

YOLO 設定保存在 `yolo_config.ini`:

```ini
[YOLO]
Enabled=1
Confidence=0.50
NMSThreshold=45
```

### GUI 設定

| 設定 | 說明 | 預設值 |
|------|------|--------|
| use_yolo_mode | 啟用 YOLO 模式 | false |
| yolo_confidence | 信心度閾值 (0.0~1.0) | 0.50 |
| yolo_nms_threshold | NMS 閾值 (0~100) | 45 |

---

## 日誌輸出

執行程式時觀察日誌:

```
[YOLO] 偵測器已就緒 (conf=0.50, nms=0.45)
[YOLO] 推論時間: 15.32 ms, 偵測: 3 個目標
[YOLO] 🎯 鎖怪成功 (520, 400) hp=85%
```

---

## 效能

| 模型 | 輸入尺寸 | 推論時間 (Core i5) |
|------|---------|-------------------|
| YOLOv8n | 640x640 | ~15-20 ms |
| YOLOv8n | 416x416 | ~8-12 ms |

---

## 故障排除

### 1. ONNX Runtime DLL 未找到
```
[YoloDetector] Failed to load DLL: onnxruntime.dll
```
**解決**: 執行 `setup_onnx_runtime.ps1` 或手動複製 DLL

### 2. 模型載入失敗
```
[YoloDetector] OrtCreateSessionFromFile failed: ...
```
**解決**: 確認 `models/best.onnx` 存在且路徑正確

### 3. YOLO 推論時間過長
```
[YOLO] 推論時間: 150.00 ms
```
**解決**: 使用較小的模型或降低輸入尺寸

---

## 參考資料

- [ONNX Runtime v1.12.1 Release](https://github.com/microsoft/onnxruntime/releases/tag/v1.12.1)
- [ONNX Runtime C API Documentation](https://onnxruntime.ai/docs/api/c/)
- [Ultralytics YOLOv8](https://github.com/ultralytics/ultralytics)
