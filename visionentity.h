#pragma once
// ============================================================
// 視覺實體掃描模組（Win7~Win11 通用）
// 純像素掃描 + YOLO 物件偵測
// ============================================================
#include <windows.h>

// YOLO types (forward declare if USE_YOLO_DETECTION)
// ============================================================
#ifdef USE_YOLO_DETECTION
struct YoloBox;  // Forward declaration
#include "yolo_detector.h"
#endif

// ============================================================
// 視覺怪物結構
// ============================================================
struct VisualMonster {
    int screenX = 0;      // 螢幕座標 X
    int screenY = 0;      // 螢幕座標 Y
    int relX = 0;        // 遊戲相對座標 X (0-1023)
    int relY = 0;         // 遊戲相對座標 Y (0-767)
    int hpPct = 100;     // 血量百分比 0-100
    int priority = 0;    // 優先級（值越大越優先）
    int width = 0;       // 血條寬度
};

// ============================================================
// 視覺讀取的玩家狀態（取代記憶體讀值）
// ============================================================
struct VisualPlayerState {
    int hpPct = 100;       // HP 百分比 0-100
    int mpPct = 100;       // MP 百分比 0-100
    int spPct = 100;       // SP 百分比 0-100
    int mapId = 0;         // 地圖 ID（從視覺辨識得到）
    int screenX = 0;       // 玩家畫面座標
    int screenY = 0;
    int gold = 0;          // 金幣（像素計數估算）
    bool found = false;     // 是否成功讀到
};

// ============================================================
// YOLO 偵測器控制 (USE_YOLO_DETECTION 必須定義)
// ============================================================
#ifdef USE_YOLO_DETECTION

// 初始化 YOLO 偵測器
// modelPath: ONNX 模型路徑
// 返回: 是否成功
bool InitYoloDetector(const char* modelPath);
bool InitYoloDetectorW(const wchar_t* modelPath);

// 檢查 YOLO 是否就緒
bool IsYoloReady();

// 釋放 YOLO 資源
void DestroyYoloDetector();

// 設定 YOLO 閾值
void SetYoloThresholds(float confidence, float nms_threshold);

// 獲取最後推論時間 (毫秒)
float GetYoloInferenceTime();

// YOLO 掃描怪物 (直接調用)
int ScanYoloMonsters(HWND hWnd, VisualMonster* outMonsters, int maxMonsters,
                    int gameW, int gameH);

#endif // USE_YOLO_DETECTION

// ============================================================
// 函式宣告
// ============================================================

// 掃描畫面所有怪物血條（整合 YOLO + 像素掃描）
// 如果定義了 USE_YOLO_DETECTION 且 YOLO 可用，優先使用 YOLO
// 返回找到的怪物數量
int ScanVisualMonsters(HWND hWnd, VisualMonster* outMonsters, int maxMonsters);

// 讀取視覺玩家狀態（HP/MP/SP 百分比）
bool ReadVisualPlayerState(HWND hWnd, VisualPlayerState* outState);

// 估算怪物優先級（y 越大越近優先）
int EstimateMonsterPriority(const VisualMonster* m, int screenH);

// 對怪物列表按優先級排序
void SortVisualMonsters(VisualMonster* monsters, int count);
