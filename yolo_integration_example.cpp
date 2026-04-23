// ============================================================
// YOLO 整合範例
// 這個檔案展示如何將 YoloDetector 整合到現有程式碼
// ============================================================
//
// 使用方式:
//   1. 複製此檔案的關鍵部分到 visionentity.cpp
//   2. 或直接 include 此檔案進行測試
//
// ============================================================

#include "yolo_detector.h"
#include "visionentity.h"
#include "screenshot.h"
#include <vector>

// ============================================================
// YOLO 模式開關 (可通過設定檔控制)
// ============================================================
#ifndef USE_YOLO_DETECTION
#define USE_YOLO_DETECTION 1  // 設為 0 完全停用 YOLO
#endif

// ============================================================
// 全域 YOLO 偵測器 (單例模式)
// ============================================================
class YoloScanner {
public:
    static YoloScanner& Instance() {
        static YoloScanner scanner;
        return scanner;
    }

    bool Init(const wchar_t* modelPath) {
        if (m_inited) return true;
        m_inited = m_detector.Init(modelPath);
        if (m_inited) {
            printf("[YoloScanner] Initialized: %ws\n", modelPath);
        }
        return m_inited;
    }

    void Destroy() {
        m_detector.Destroy();
        m_inited = false;
    }

    bool IsReady() const { return m_inited; }

    // 主要掃描函式
    int ScanMonsters(HWND hWnd, VisualMonster* outMonsters, int maxMonsters,
                     int gameW = 1024, int gameH = 768) {
#if USE_YOLO_DETECTION
        // 嘗試 YOLO 偵測
        if (m_inited) {
            std::vector<uint8_t> pixels;
            if (CaptureGameWindow(hWnd, pixels, gameW, gameH)) {
                std::vector<YoloBox> boxes;
                int n = m_detector.Detect(pixels.data(), gameW, gameH, boxes);
                if (n > 0) {
                    int converted = ConvertYoloToVisualMonsters(
                        &m_detector, boxes,
                        gameW, gameH, gameW, gameH,
                        outMonsters, maxMonsters
                    );
                    if (converted > 0) {
                        printf("[YoloScanner] Detected %d monsters via YOLO\n", converted);
                        return converted;
                    }
                }
            }
        }
#endif
        // 後備: 像素掃描
        return ScanVisualMonsters(hWnd, outMonsters, maxMonsters);
    }

    YoloDetector& GetDetector() { return m_detector; }

private:
    YoloScanner() : m_inited(false) {}
    ~YoloScanner() { Destroy(); }

    YoloDetector m_detector;
    bool m_inited;
};

// ============================================================
// 便利函式: 使用預設設定初始化 YOLO
// ============================================================
inline bool InitYoloScanner(const wchar_t* modelPath = L"models\\best.onnx") {
    return YoloScanner::Instance().Init(modelPath);
}

// ============================================================
// 便利函式: 掃描怪物 (整合版)
// ============================================================
inline int ScanMonstersWithYolo(HWND hWnd, VisualMonster* outMonsters, int maxMonsters) {
    return YoloScanner::Instance().ScanMonsters(hWnd, outMonsters, maxMonsters);
}

// ============================================================
// 使用範例
// ============================================================
void ExampleUsage() {
    HWND hWnd = FindWindow(NULL, L"亂2 online");
    if (!hWnd) return;

    // 初始化 (在程式啟動時執行一次)
    if (!YoloScanner::Instance().Init(L"models\\best.onnx")) {
        printf("[Error] Failed to init YOLO scanner\n");
        // 程式仍然可以運作，只是使用像素掃描
    }

    // 主迴圈中
    VisualMonster monsters[32];
    int count = ScanMonstersWithYolo(hWnd, monsters, 32);

    printf("Found %d monsters:\n", count);
    for (int i = 0; i < count; i++) {
        printf("  [%d] x=%d, y=%d, hp=%d%%, priority=%d\n",
               i, monsters[i].relX, monsters[i].relY,
               monsters[i].hpPct, monsters[i].priority);
    }
}

// ============================================================
// 高效能模式: 減少記憶體配置
// ============================================================
class EfficientYoloScanner {
public:
    EfficientYoloScanner() : m_inited(false), m_maxMonsters(32) {
        // 預先配置緩衝區
        m_monsters.resize(m_maxMonsters);
    }

    bool Init(const wchar_t* modelPath) {
        m_inited = m_detector.Init(modelPath);
        return m_inited;
    }

    // 單一介面: 只需要 HWND
    int Scan(HWND hWnd) {
        return ScanMonstersWithYolo(hWnd, m_monsters.data(), m_maxMonsters);
    }

    const VisualMonster* GetMonsters() const { return m_monsters.data(); }
    int GetCount() const { return (int)m_monsters.size(); }

    YoloDetector& GetDetector() { return m_detector; }

private:
    YoloDetector m_detector;
    bool m_inited;
    int m_maxMonsters;
    std::vector<VisualMonster> m_monsters;
};

// ============================================================
// 速度優先模式: 較低解析度
// ============================================================
class FastYoloScanner : public YoloScanner {
public:
    FastYoloScanner() {
        // 設定較低的信心度閾值
        m_detector.SetConfidenceThreshold(0.3f);
        m_detector.SetNMSThreshold(0.5f);
    }

    // 使用較小的模型輸入尺寸 (需要在模型支援的情況下)
    // 注意: YoloDetector 預設使用模型原始尺寸
};

// ============================================================
// 整合到現有 visionentity.cpp 的範例程式碼
// ============================================================
/*
// 在 visionentity.cpp 頂部加入:
#define USE_YOLO_DETECTION 1
#if USE_YOLO_DETECTION
#include "yolo_detector.h"
#endif

// 在檔案全域變數區域加入:
#if USE_YOLO_DETECTION
static YoloDetector s_yolo;
static bool s_yoloInited = false;
#endif

// 修改 ScanVisualMonsters 函式:
int ScanVisualMonsters(HWND hWnd, VisualMonster* outMonsters, int maxMonsters) {
#if USE_YOLO_DETECTION
    // 嘗試 YOLO (延遲初始化)
    if (!s_yoloInited) {
        s_yoloInited = s_yolo.Init(L"models\\best.onnx");
    }

    if (s_yoloInited) {
        std::vector<uint8_t> pixels;
        if (CaptureGameWindow(hWnd, pixels, 1024, 768)) {
            std::vector<YoloBox> boxes;
            int n = s_yolo.Detect(pixels.data(), 1024, 768, boxes);
            if (n > 0) {
                int converted = ConvertYoloToVisualMonsters(
                    &s_yolo, boxes,
                    1024, 768, 1024, 768,
                    outMonsters, maxMonsters
                );
                if (converted > 0) {
                    return converted;
                }
            }
        }
    }
#endif

    // 後備: 原始像素掃描
    // ... 現有程式碼 ...
}
*/

// ============================================================
// 結束
// ============================================================
