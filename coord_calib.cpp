// coord_calib.cpp — F7 即按即換校正系統實作
#include "coord_calib.h"
#include "coords.h"
#include <cstring>

// ============================================================
// 靜態對照表：CalibIndex → coords.h 存取路徑
// ============================================================
static const char* s_labels[] = {
    // 復活
    "歸魂珠復活",
    "原地復活",
    "基本復活",

    // 聖門
    "聖門_特派員",
    "聖門_對話框",
    "聖門_箭矢",
    "聖門_符咒",
    "聖門_消耗品",
    "聖門_購買確認",

    // 商洞
    "商洞_特派員",
    "商洞_特派員_FAR",

    // 玄巖
    "玄巖_特派員",
    "玄巖_對話框",
    "玄巖_箭矢",
    "玄巖_符咒",
    "玄巖_消耗品",

    // 鳳凰
    "鳳凰_特派員",
    "鳳凰_對話框",

    // 寵物
    "寵物_飼料",
    "寵物_符咒",

    // 中心點
    "中心點(煉功點)",

    // 攻擊掃描8點
    "攻01",
    "攻02",
    "攻03",
    "攻04",
    "攻05",
    "攻06",
    "攻07",
    "攻08",
};

static_assert(sizeof(s_labels) / sizeof(s_labels[0]) == (size_t)CalibIndex::COUNT,
    "s_labels count must match CalibIndex::COUNT");

// ============================================================
// 對應 coords.h 的 getter（Win7/Win11 自動切換）
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
        case CalibIndex::NPC商洞_特派員_FAR:   return Coords::NPC商洞特派員詹姆士_FAR;

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

        default: return Coords::Point(0, 0);
    }
}

// ============================================================
// CoordCalibrator 實作
// ============================================================
CoordCalibrator& CoordCalibrator::Instance() {
    static CoordCalibrator inst;
    return inst;
}

CoordCalibrator::CoordCalibrator() {
    memset(m_overridesX, 0xFF, sizeof(m_overridesX));  // -1 = 未校正
    memset(m_overridesZ, 0xFF, sizeof(m_overridesZ));
}

void CoordCalibrator::SetActive(bool v) {
    m_active = v;
    if (!v) m_selected = CalibIndex::NONE;
}

void CoordCalibrator::SetSelected(CalibIndex idx) {
    m_selected = idx;
    if (idx != CalibIndex::NONE) m_active = true;
}

int CoordCalibrator::ScreenToRelX(int sx) const {
    if (!m_gameHwnd || !IsWindow(m_gameHwnd)) return sx;
    RECT rc;
    if (!GetClientRect(m_gameHwnd, &rc)) return sx;
    int w = rc.right;
    if (w <= 0) return sx;
    // 遊戲內座標 0-1023
    return (int)((int64_t)sx * 1024 / w);
}

int CoordCalibrator::ScreenToRelY(int sy) const {
    if (!m_gameHwnd || !IsWindow(m_gameHwnd)) return sy;
    RECT rc;
    if (!GetClientRect(m_gameHwnd, &rc)) return sy;
    int h = rc.bottom;
    if (h <= 0) return sy;
    // 遊戲內座標 0-767
    return (int)((int64_t)sy * 768 / h);
}

void CoordCalibrator::OnScreenClick(int screenX, int screenY) {
    if (!m_active || m_selected == CalibIndex::NONE) return;

    // 轉為客戶區座標
    if (!m_gameHwnd || !IsWindow(m_gameHwnd)) return;
    POINT pt = { screenX, screenY };
    if (!ScreenToClient(m_gameHwnd, &pt)) return;

    int rx = ScreenToRelX(pt.x);
    int rz = ScreenToRelY(pt.y);
    Set(m_selected, rx, rz);

    // 自動跳到下一項
    int cur = (int)m_selected;
    int next = cur + 1;
    while (next < (int)CalibIndex::COUNT) {
        if (m_overridesX[next] == -1) break;  // 找下一個未校正的
        next++;
    }
    if (next >= (int)CalibIndex::COUNT) next = 0;
    m_selected = (CalibIndex)next;
}

int CoordCalibrator::GetX(CalibIndex idx) const {
    int i = (int)idx;
    if (i < 0 || i >= (int)CalibIndex::COUNT) return 0;
    if (m_overridesX[i] != -1) return m_overridesX[i];
    return GetDefault(idx).x;
}

int CoordCalibrator::GetZ(CalibIndex idx) const {
    int i = (int)idx;
    if (i < 0 || i >= (int)CalibIndex::COUNT) return 0;
    if (m_overridesZ[i] != -1) return m_overridesZ[i];
    return GetDefault(idx).z;
}

bool CoordCalibrator::IsCalibrated(CalibIndex idx) const {
    int i = (int)idx;
    if (i < 0 || i >= (int)CalibIndex::COUNT) return false;
    return m_overridesX[i] != -1 && m_overridesZ[i] != -1;
}

void CoordCalibrator::Set(CalibIndex idx, int rx, int rz) {
    int i = (int)idx;
    if (i < 0 || i >= (int)CalibIndex::COUNT) return;
    m_overridesX[i] = rx;
    m_overridesZ[i] = rz;
}

void CoordCalibrator::Reset(CalibIndex idx) {
    int i = (int)idx;
    if (i < 0 || i >= (int)CalibIndex::COUNT) return;
    m_overridesX[i] = -1;
    m_overridesZ[i] = -1;
}

void CoordCalibrator::ResetAll() {
    memset(m_overridesX, 0xFF, sizeof(m_overridesX));
    memset(m_overridesZ, 0xFF, sizeof(m_overridesZ));
}

const char* CoordCalibrator::GetLabel(CalibIndex idx) const {
    int i = (int)idx;
    // NONE = -1，其他從 0 開始
    if (i == (int)CalibIndex::NONE) return "【未選擇】";
    if (i < 0 || i >= (int)CalibIndex::COUNT) return "???";
    return s_labels[i];
}

void CoordCalibrator::SaveToIni(const char* path) const {
    if (!path) return;
    for (int i = 0; i < (int)CalibIndex::COUNT; i++) {
        char key[64];
        snprintf(key, sizeof(key), "x_%d", i);
        WritePrivateProfileStringA("Calib", key,
            m_overridesX[i] == -1 ? "-1" : std::to_string(m_overridesX[i]).c_str(), path);
        snprintf(key, sizeof(key), "z_%d", i);
        WritePrivateProfileStringA("Calib", key,
            m_overridesZ[i] == -1 ? "-1" : std::to_string(m_overridesZ[i]).c_str(), path);
    }
}

void CoordCalibrator::LoadFromIni(const char* path) {
    if (!path) return;
    for (int i = 0; i < (int)CalibIndex::COUNT; i++) {
        char key[64];
        snprintf(key, sizeof(key), "x_%d", i);
        m_overridesX[i] = GetPrivateProfileIntA("Calib", key, -1, path);
        snprintf(key, sizeof(key), "z_%d", i);
        m_overridesZ[i] = GetPrivateProfileIntA("Calib", key, -1, path);
    }
}
