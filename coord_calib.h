#pragma once
// coord_calib.h — F7 即按即換校正系統
// 所有座標以 0-1000 相對值儲存，ToAbsolute() 時乘以實際視窗大小

#include <windows.h>
#include <string>

// ============================================================
// 校正項目列舉（對應 coords.h 中需要可調整的定點）
// ============================================================
enum class CalibIndex {
    NONE = -1,

    // 復活
    REVIVE_SOUL_PEARL,   // 歸魂珠復活
    REVIVE原地,           // 原地復活
    REVIVE_基本,          // 基本復活

    // 聖門 NPC
    NPC聖門_特派員,
    NPC聖門_對話框購買,
    NPC聖門_箭矢,
    NPC聖門_符咒,
    NPC聖門_消耗品,
    NPC聖門_購買確認,

    // 商洞 NPC
    NPC商洞_特派員,
    NPC商洞_特派員_FAR,

    // 玄巖 NPC
    NPC玄巖_特派員,
    NPC玄巖_對話框購買,
    NPC玄巖_箭矢,
    NPC玄巖_符咒,
    NPC玄巖_消耗品,

    // 鳳凰 NPC
    NPC鳳凰_特派員,
    NPC鳳凰_對話框購買,

    // 寵物
    PET_FOOD,
    PET_TALISMAN,

    // 中心點（煉功點圓心）
    CENTER_POINT,

    // 攻擊掃描8點（圓形攻擊定點，可個別微調）
    SCAN_PT01,
    SCAN_PT02,
    SCAN_PT03,
    SCAN_PT04,
    SCAN_PT05,
    SCAN_PT06,
    SCAN_PT07,
    SCAN_PT08,

    COUNT
};

// ============================================================
// 校正資料結構
// ============================================================
struct CalibEntry {
    const char* label;       // 顯示名稱
    CalibIndex index;        // 列舉值
    int* outX;               // 輸出相對 X（0-1000）
    int* outZ;               // 輸出相對 Z（0-1000）
};

// ============================================================
// 校正器（單例，全域存取）
// ============================================================
class CoordCalibrator {
public:
    static CoordCalibrator& Instance();

    // 是否在校正模式
    bool IsActive() const { return m_active; }
    void SetActive(bool v);

    // 當前選中的項目
    CalibIndex GetSelected() const { return m_selected; }
    void SetSelected(CalibIndex idx);

    // 設定遊戲視窗 HWND（用於座標轉換）
    void SetGameHwnd(HWND h) { m_gameHwnd = h; }
    HWND GetGameHwnd() const { return m_gameHwnd; }

    // 收到滑鼠點擊（在校正模式下）
    // 傳入絕對螢幕座標，會自動轉為相對座標並寫入
    void OnScreenClick(int screenX, int screenY);

    // 查詢/寫入特定項目（供 Bot 使用）
    // 傳回校正後的相對座標，若未校正則用預設值
    int GetX(CalibIndex idx) const;
    int GetZ(CalibIndex idx) const;
    bool IsCalibrated(CalibIndex idx) const;  // 是否設定過校正值
    void Set(CalibIndex idx, int rx, int rz);

    // 持久化
    void SaveToIni(const char* path) const;
    void LoadFromIni(const char* path);

    // 取消校正（還原為預設）
    void Reset(CalibIndex idx);
    void ResetAll();

    const char* GetLabel(CalibIndex idx) const;

private:
    CoordCalibrator();
    ~CoordCalibrator() {}

    // 不可拷貝
    CoordCalibrator(const CoordCalibrator&) = delete;
    CoordCalibrator& operator=(const CoordCalibrator&) = delete;

    // 螢幕座標 → 相對座標（0-1000）
    int ScreenToRelX(int sx) const;
    int ScreenToRelY(int sy) const;

    bool m_active = false;
    CalibIndex m_selected = CalibIndex::NONE;
    HWND m_gameHwnd = NULL;

    // 校正值（未校正專案 = -1，表示使用預設）
    int m_overridesX[(int)CalibIndex::COUNT];
    int m_overridesZ[(int)CalibIndex::COUNT];
};
