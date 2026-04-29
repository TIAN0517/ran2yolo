#pragma once
// coord_calib.h — 統一座標校正系統
// 所有座標以 0-1000 相對值儲存，點擊時自動轉換為螢幕物理座標
// 統一使用 GetClientRect + ClientToScreen，不受視窗邊框/標題列影響
// 這是專案中唯一的座標轉換實作（禁止重複）

#include <windows.h>

// ============================================================
// 校正項目列舉
// ============================================================
enum class CalibIndex {
    NONE = -1,

    // 復活
    REVIVE_SOUL_PEARL,
    REVIVE原地,
    REVIVE_基本,

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

    // 中心點
    CENTER_POINT,

    // 攻擊掃描8點
    SCAN_PT01, SCAN_PT02, SCAN_PT03, SCAN_PT04,
    SCAN_PT05, SCAN_PT06, SCAN_PT07, SCAN_PT08,

    // ===== 買賣補給座標 =====
    // 商店介面
    SHOP_OPEN,           // 點擊開啟商店（購買分頁）
    SHOP_TAB_ARROW,      // 箭矢分頁
    SHOP_TAB_HP,         // HP水分頁
    SHOP_TAB_MP,         // MP水分頁
    SHOP_TAB_SP,         // SP水分頁
    SHOP_ITEM_SLOT,     // 物品格子（第一格，點選要買的物品）
    SHOP_BUY_CONFIRM,    // 購買確認按鈕
    SHOP_QUANTITY_INPUT, // 數量輸入框
    SHOP_QUANTITY_CONFIRM, // 數量確認
    SHOP_SELL_TAB,       // 賣物分頁
    SHOP_SELL_SLOT,      // 賣物格子
    SHOP_SELL_CONFIRM,   // 賣物確認
    SHOP_CLOSE,          // 關閉商店

    COUNT
};

// ============================================================
// 統一座標系統
// ============================================================
struct ClientRect {
    int x = 0, y = 0, w = 0, h = 0;  // 客戶區尺寸（不含邊框/標題）
    bool valid = false;
};

// ============================================================
// 統一座標轉換 API（靜態工具函式）
// 所有座標轉換必須透過此命名空間，不允許在其他地方重複實作
// ============================================================
namespace CoordConv {
    // 取得客戶區尺寸（僅使用 GetClientRect，不計入邊框/標題）
    inline bool GetClientRect(HWND hWnd, int* outW, int* outH) {
        if (!hWnd || !IsWindow(hWnd)) {
            if (outW) *outW = 0;
            if (outH) *outH = 0;
            return false;
        }
        RECT rc;
        if (!::GetClientRect(hWnd, &rc)) {
            if (outW) *outW = 0;
            if (outH) *outH = 0;
            return false;
        }
        if (outW) *outW = rc.right - rc.left;
        if (outH) *outH = rc.bottom - rc.top;
        return rc.right > 0 && rc.bottom > 0;
    }

    // 相對座標 (0-1000) → 客戶區像素
    inline void RelToClient(int relX, int relY, int* outX, int* outY, int clientW, int clientH) {
        if (outX) *outX = relX * clientW / 1000;
        if (outY) *outY = relY * clientH / 1000;
    }

    // 相對座標 (0-1000) → 螢幕絕對座標
    inline bool RelToScreen(HWND hWnd, int relX, int relY, int* outScreenX, int* outScreenY) {
        if (!hWnd || !IsWindow(hWnd)) return false;
        int clientW, clientH;
        if (!GetClientRect(hWnd, &clientW, &clientH)) return false;

        int cx = relX * clientW / 1000;
        int cy = relY * clientH / 1000;

        POINT pt = { cx, cy };
        if (!ClientToScreen(hWnd, &pt)) return false;

        if (outScreenX) *outScreenX = pt.x;
        if (outScreenY) *outScreenY = pt.y;
        return true;
    }

    // 螢幕座標 → 相對座標 (0-1000)
    inline bool ScreenToRel(HWND hWnd, int screenX, int screenY, int* outRelX, int* outRelY) {
        if (!hWnd || !IsWindow(hWnd)) return false;
        POINT pt = { screenX, screenY };
        if (!ScreenToClient(hWnd, &pt)) return false;

        int clientW, clientH;
        if (!GetClientRect(hWnd, &clientW, &clientH)) return false;

        if (outRelX) *outRelX = pt.x * 1000 / clientW;
        if (outRelY) *outRelY = pt.y * 1000 / clientH;
        return true;
    }

    // 客戶區像素 → 螢幕座標
    inline bool ClientToScreenPt(HWND hWnd, int clientX, int clientY, int* outScreenX, int* outScreenY) {
        if (!hWnd || !IsWindow(hWnd)) return false;
        POINT pt = { clientX, clientY };
        if (!::ClientToScreen(hWnd, &pt)) return false;
        if (outScreenX) *outScreenX = pt.x;
        if (outScreenY) *outScreenY = pt.y;
        return true;
    }

    // 螢幕座標 → 客戶區像素
    inline bool ScreenToClientPt(HWND hWnd, int screenX, int screenY, int* outClientX, int* outClientY) {
        if (!hWnd || !IsWindow(hWnd)) return false;
        POINT pt = { screenX, screenY };
        if (!::ScreenToClient(hWnd, &pt)) return false;
        if (outClientX) *outClientX = pt.x;
        if (outClientY) *outClientY = pt.y;
        return true;
    }
} // namespace CoordConv

// ============================================================
// 校正器（單例）
// ============================================================
class CoordCalibrator {
public:
    static CoordCalibrator& Instance();

    // 設定遊戲視窗
    void SetGameHwnd(HWND h) { m_hwnd = h; }
    HWND GetGameHwnd() const { return m_hwnd; }

    // 校正模式
    bool IsActive() const { return m_active; }
    void SetActive(bool v);
    CalibIndex GetSelected() const { return m_selected; }
    void SetSelected(CalibIndex idx);

    // 收到螢幕點擊 → 轉相對座標儲存
    void OnScreenClick(int screenX, int screenY);

    // 查詢（返回 0-1000 相對座標）
    int GetX(CalibIndex idx) const;
    int GetZ(CalibIndex idx) const;
    bool IsCalibrated(CalibIndex idx) const;
    void Set(CalibIndex idx, int rx, int rz);

    // ========== 統一座標轉換（核心 API）==========

    // 取得客戶區尺寸
    ClientRect GetClientRect() const;

    // 相對座標 (0-1000) → 客戶區像素
    static bool RelToClient(int relX, int relY, int* outX, int* outY, const ClientRect& rc);

    // 相對座標 (0-1000) → 螢幕絕對座標（含 ClientToScreen）
    static bool RelToScreen(int relX, int relY, int* outX, int* outY, HWND hwnd);

    // CalibIndex → 螢幕座標（完整轉換鏈）
    bool ToScreen(CalibIndex idx, int* outScreenX, int* outScreenY) const;

    // ========== 輔助函式 ==========

    // 螢幕座標 → 相對座標（用於校正模式）
    static int ScreenToRelX(int sx, const ClientRect& rc);
    static int ScreenToRelY(int sy, const ClientRect& rc);

    // 持久化
    void Save() const;
    void Load();
    void SaveToIni(const char* path) const;
    void LoadFromIni(const char* path);

    void Reset(CalibIndex idx);
    void ResetAll();
    const char* GetLabel(CalibIndex idx) const;

private:
    CoordCalibrator();
    CoordCalibrator(const CoordCalibrator&) = delete;
    CoordCalibrator& operator=(const CoordCalibrator&) = delete;

    bool m_active = false;
    CalibIndex m_selected = CalibIndex::NONE;
    HWND m_hwnd = NULL;

    // 校正值（-1 = 未校正，使用預設）
    int m_overX[(int)CalibIndex::COUNT];
    int m_overZ[(int)CalibIndex::COUNT];
};
