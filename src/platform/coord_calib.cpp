// coord_calib.cpp — 統一座標校正系統
#include "coord_calib.h"
#include "../config/coords.h"
#include <cstring>
#include <cstdio>
#include <string>

// ============================================================
// 靜態初始化：停用 DPI 縮放（Win7/Win10/Win11 通用）
// ============================================================
static struct DpiBootstrap {
    DpiBootstrap() {
        // 停用 DPI 虛擬化，程式自行處理 HiDPI
        // 不使用 SetProcessDpiAwarenessContext（Win7 不支援）
        SetProcessDPIAware();
    }
} s_dpiInit;

// ============================================================
// 標籤表
// ============================================================
static const char* s_labels[] = {
    // 復活
    "歸魂珠復活", "原地復活", "基本復活",
    // 聖門
    "聖門_特派員", "聖門_對話框", "聖門_箭矢", "聖門_符咒", "聖門_消耗品", "聖門_購買確認",
    // 商洞
    "商洞_特派員", "商洞_特派員_FAR",
    // 玄巖
    "玄巖_特派員", "玄巖_對話框", "玄巖_箭矢", "玄巖_符咒", "玄巖_消耗品",
    // 鳳凰
    "鳳凰_特派員", "鳳凰_對話框",
    // 寵物
    "寵物_飼料", "寵物_符咒",
    // 中心點
    "中心點(煉功點)",
    // 攻擊8點
    "攻01", "攻02", "攻03", "攻04", "攻05", "攻06", "攻07", "攻08",
    // 買賣座標
    "商店_開啟", "商店_箭矢分頁", "商店_HP分頁", "商店_MP分頁", "商店_SP分頁",
    "商店_物品格子", "商店_購買確認", "商店_數量輸入", "商店_數量確認",
    "商店_賣物分頁", "商店_賣物格子", "商店_賣物確認", "商店_關閉",
};
static_assert(sizeof(s_labels) / sizeof(s_labels[0]) == (size_t)CalibIndex::COUNT, "labels mismatch");

// ============================================================
// 預設座標查表
// ============================================================
static Coords::Point GetDefault(CalibIndex idx) {
    switch (idx) {
        case CalibIndex::REVIVE_SOUL_PEARL:  return Coords::歸魂珠復活;
        case CalibIndex::REVIVE原地:           return Coords::復活按鈕;
        case CalibIndex::REVIVE_基本:          return Coords::基本復活;
        case CalibIndex::NPC聖門_特派員:       return Coords::NPC聖門特派員詹姆士;
        case CalibIndex::NPC聖門_對話框購買:  return Coords::NPC聖門對話框購買物品;
        case CalibIndex::NPC聖門_箭矢:        return Coords::NPC聖門箭矢;
        case CalibIndex::NPC聖門_符咒:         return Coords::NPC聖門符咒;
        case CalibIndex::NPC聖門_消耗品:       return Coords::NPC聖門消耗品;
        case CalibIndex::NPC聖門_購買確認:     return Coords::NPC聖門箭矢購買確認;
        case CalibIndex::NPC商洞_特派員:       return Coords::NPC商洞特派員詹姆士;
        case CalibIndex::NPC商洞_特派員_FAR:   return Coords::NPC商洞特派員詹姆士;
        case CalibIndex::NPC玄巖_特派員:       return Coords::NPC玄巖特派員詹姆士;
        case CalibIndex::NPC玄巖_對話框購買:  return Coords::NPC玄巖對話框購買物品;
        case CalibIndex::NPC玄巖_箭矢:        return Coords::NPC玄巖箭矢;
        case CalibIndex::NPC玄巖_符咒:         return Coords::NPC玄巖符咒;
        case CalibIndex::NPC玄巖_消耗品:       return Coords::NPC玄巖消耗品;
        case CalibIndex::NPC鳳凰_特派員:       return Coords::NPC鳳凰特派員詹姆士;
        case CalibIndex::NPC鳳凰_對話框購買:  return Coords::NPC鳳凰對話框購買物品;
        case CalibIndex::PET_FOOD:             return Coords::食料;
        case CalibIndex::PET_TALISMAN:         return Coords::寵物卡;
        case CalibIndex::CENTER_POINT:         return Coords::中心點;
        case CalibIndex::SCAN_PT01:            return Coords::點01;
        case CalibIndex::SCAN_PT02:            return Coords::點02;
        case CalibIndex::SCAN_PT03:            return Coords::點03;
        case CalibIndex::SCAN_PT04:            return Coords::點04;
        case CalibIndex::SCAN_PT05:            return Coords::點05;
        case CalibIndex::SCAN_PT06:            return Coords::點06;
        case CalibIndex::SCAN_PT07:            return Coords::點07;
        case CalibIndex::SCAN_PT08:            return Coords::點08;
        // 買賣座標（預設值，可校正）
        case CalibIndex::SHOP_OPEN:            return Coords::Point(500, 450);  // 購買分頁按鈕
        case CalibIndex::SHOP_TAB_ARROW:       return Coords::Point(450, 200);  // 箭矢分頁
        case CalibIndex::SHOP_TAB_HP:           return Coords::Point(500, 200);  // HP水分頁
        case CalibIndex::SHOP_TAB_MP:           return Coords::Point(550, 200);  // MP水分頁
        case CalibIndex::SHOP_TAB_SP:           return Coords::Point(600, 200);  // SP水分頁
        case CalibIndex::SHOP_ITEM_SLOT:       return Coords::Point(500, 350);  // 物品格子（第一格）
        case CalibIndex::SHOP_BUY_CONFIRM:      return Coords::Point(500, 530);  // 購買確認
        case CalibIndex::SHOP_QUANTITY_INPUT:  return Coords::Point(500, 450);  // 數量輸入框
        case CalibIndex::SHOP_QUANTITY_CONFIRM: return Coords::Point(500, 530);  // 數量確認
        case CalibIndex::SHOP_SELL_TAB:        return Coords::Point(550, 200);  // 賣物分頁
        case CalibIndex::SHOP_SELL_SLOT:        return Coords::Point(500, 350);  // 賣物格子（第一格）
        case CalibIndex::SHOP_SELL_CONFIRM:    return Coords::Point(500, 530);  // 賣物確認
        case CalibIndex::SHOP_CLOSE:            return Coords::Point(500, 380);  // 關閉商店（ESC或X按鈕）
        default: return Coords::Point(0, 0);
    }
}

// ============================================================
// 實作
// ============================================================
CoordCalibrator& CoordCalibrator::Instance() {
    static CoordCalibrator inst;
    return inst;
}

CoordCalibrator::CoordCalibrator() {
    memset(m_overX, 0xFF, sizeof(m_overX));
    memset(m_overZ, 0xFF, sizeof(m_overZ));
}

void CoordCalibrator::SetActive(bool v) {
    m_active = v;
    if (!v) m_selected = CalibIndex::NONE;
}

void CoordCalibrator::SetSelected(CalibIndex idx) {
    m_selected = idx;
    if (idx != CalibIndex::NONE) m_active = true;
}

// ============================================================
// 統一座標 API
// ============================================================

// 僅使用 GetClientRect，取得客戶區尺寸（不含邊框/標題）
ClientRect CoordCalibrator::GetClientRect() const {
    ClientRect rc;
    if (!m_hwnd || !IsWindow(m_hwnd)) return rc;
    RECT r;
    if (!::GetClientRect(m_hwnd, &r)) return rc;
    rc.w = r.right - r.left;
    rc.h = r.bottom - r.top;
    if (rc.w > 0 && rc.h > 0) rc.valid = true;
    return rc;
}

// 相對座標 (0-1000) → 客戶區像素
bool CoordCalibrator::RelToClient(int relX, int relY, int* outX, int* outY, const ClientRect& rc) {
    if (!rc.valid || !outX || !outY) return false;
    *outX = relX * rc.w / 1000;
    *outY = relY * rc.h / 1000;
    return true;
}

// 相對座標 (0-1000) → 螢幕絕對座標（含 ClientToScreen）
bool CoordCalibrator::RelToScreen(int relX, int relY, int* outX, int* outY, HWND hwnd) {
    if (!outX || !outY || !hwnd) return false;
    RECT rc;
    if (!::GetClientRect(hwnd, &rc)) return false;
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0) return false;
    int cx = relX * w / 1000;
    int cy = relY * h / 1000;
    POINT pt = { cx, cy };
    if (!::ClientToScreen(hwnd, &pt)) return false;
    *outX = pt.x;
    *outY = pt.y;
    return true;
}

// CalibIndex → 螢幕座標（完整轉換鏈）
bool CoordCalibrator::ToScreen(CalibIndex idx, int* outX, int* outY) const {
    if (idx == CalibIndex::NONE || !m_hwnd) return false;
    return RelToScreen(GetX(idx), GetZ(idx), outX, outY, m_hwnd);
}

// 螢幕座標 → 相對座標（用於校正模式）
int CoordCalibrator::ScreenToRelX(int sx, const ClientRect& rc) {
    if (!rc.valid) return sx;
    return sx * 1000 / rc.w;
}

int CoordCalibrator::ScreenToRelY(int sy, const ClientRect& rc) {
    if (!rc.valid) return sy;
    return sy * 1000 / rc.h;
}

// ============================================================
// 校正模式：收到螢幕點擊
// ============================================================
void CoordCalibrator::OnScreenClick(int screenX, int screenY) {
    if (!m_active || m_selected == CalibIndex::NONE) return;
    if (!m_hwnd || !IsWindow(m_hwnd)) return;

    // Screen → Client
    POINT pt = { screenX, screenY };
    if (!ScreenToClient(m_hwnd, &pt)) return;

    // Client → Rel (使用統一是 ClientRect)
    ClientRect rc = GetClientRect();
    int relX = ScreenToRelX(pt.x, rc);
    int relZ = ScreenToRelY(pt.y, rc);

    Set(m_selected, relX, relZ);

    // 自動跳到下一個未校正項目
    int cur = (int)m_selected;
    int next = cur + 1;
    while (next < (int)CalibIndex::COUNT) {
        if (m_overX[next] == -1) break;
        next++;
    }
    if (next >= (int)CalibIndex::COUNT) next = 0;
    m_selected = (CalibIndex)next;
}

// ============================================================
// 查詢
// ============================================================
int CoordCalibrator::GetX(CalibIndex idx) const {
    int i = (int)idx;
    if (i < 0 || i >= (int)CalibIndex::COUNT) return 0;
    if (m_overX[i] != -1) return m_overX[i];
    return GetDefault(idx).x;
}

int CoordCalibrator::GetZ(CalibIndex idx) const {
    int i = (int)idx;
    if (i < 0 || i >= (int)CalibIndex::COUNT) return 0;
    if (m_overZ[i] != -1) return m_overZ[i];
    return GetDefault(idx).z;
}

bool CoordCalibrator::IsCalibrated(CalibIndex idx) const {
    int i = (int)idx;
    if (i < 0 || i >= (int)CalibIndex::COUNT) return false;
    return m_overX[i] != -1 && m_overZ[i] != -1;
}

void CoordCalibrator::Set(CalibIndex idx, int rx, int rz) {
    int i = (int)idx;
    if (i < 0 || i >= (int)CalibIndex::COUNT) return;
    m_overX[i] = rx;
    m_overZ[i] = rz;
}

void CoordCalibrator::Reset(CalibIndex idx) {
    int i = (int)idx;
    if (i < 0 || i >= (int)CalibIndex::COUNT) return;
    m_overX[i] = -1;
    m_overZ[i] = -1;
}

void CoordCalibrator::ResetAll() {
    memset(m_overX, 0xFF, sizeof(m_overX));
    memset(m_overZ, 0xFF, sizeof(m_overZ));
}

const char* CoordCalibrator::GetLabel(CalibIndex idx) const {
    int i = (int)idx;
    if (i == (int)CalibIndex::NONE) return "【未選擇】";
    if (i < 0 || i >= (int)CalibIndex::COUNT) return "???";
    return s_labels[i];
}

// ============================================================
// 持久化
// ============================================================
static void BuildIniPath(char* out, size_t size) {
    if (!out || size == 0) return;
    out[0] = '\0';
    GetModuleFileNameA(NULL, out, (DWORD)size);
    char* slash = strrchr(out, '\\');
    if (slash) strcpy_s(slash + 1, size - (slash + 1 - out), "coord_calib.ini");
    else strcpy_s(out, size, "coord_calib.ini");
}

void CoordCalibrator::Save() const {
    char path[MAX_PATH]{};
    BuildIniPath(path, sizeof(path));
    SaveToIni(path);
}

void CoordCalibrator::Load() {
    char path[MAX_PATH]{};
    BuildIniPath(path, sizeof(path));
    LoadFromIni(path);
}

void CoordCalibrator::SaveToIni(const char* path) const {
    if (!path) return;
    for (int i = 0; i < (int)CalibIndex::COUNT; i++) {
        char key[32];
        snprintf(key, sizeof(key), "x_%d", i);
        WritePrivateProfileStringA("Calib", key,
            m_overX[i] == -1 ? "-1" : std::to_string(m_overX[i]).c_str(), path);
        snprintf(key, sizeof(key), "z_%d", i);
        WritePrivateProfileStringA("Calib", key,
            m_overZ[i] == -1 ? "-1" : std::to_string(m_overZ[i]).c_str(), path);
    }
}

void CoordCalibrator::LoadFromIni(const char* path) {
    if (!path) return;
    for (int i = 0; i < (int)CalibIndex::COUNT; i++) {
        char key[32];
        snprintf(key, sizeof(key), "x_%d", i);
        m_overX[i] = GetPrivateProfileIntA("Calib", key, -1, path);
        snprintf(key, sizeof(key), "z_%d", i);
        m_overZ[i] = GetPrivateProfileIntA("Calib", key, -1, path);
    }
}
