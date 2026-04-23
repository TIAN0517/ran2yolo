#pragma once

// 建議此檔儲存為 UTF-8 with BOM
// 座標系統：統一使用 1024x768 作為遊戲內基準座標
// 純 Win7 版本，無平台檢測

#include <windows.h>

// ============================================================
// IsWin11: Check Windows version (inline to avoid extern issues)
// ============================================================
inline bool CoordsIsWin11() {
    OSVERSIONINFOW osvi = {};
    osvi.dwOSVersionInfoSize = sizeof(osvi);
    #pragma warning(push)
    #pragma warning(disable:4996)
    if (GetVersionExW(&osvi)) {
        return osvi.dwMajorVersion >= 10;
    }
    #pragma warning(pop)
    return false;
}

namespace Coords
{
    inline bool IsWin11() { return CoordsIsWin11(); }

    // ============================================================
    // 基本座標型別
    // 遊戲內座標直接使用 1024x768 範圍（無需轉換）
    // ============================================================
    struct Point
    {
        int x;   // 0-1023 (對應 1024 寬)
        int z;   // 0-767 (對應 768 高)
        constexpr Point(int px = 0, int pz = 0) : x(px), z(pz) {}
    };

    using ScanPoint = Point;

    // ============================================================
    // 1024x768 遊戲 client 基準解析度
    // ============================================================
    static constexpr int GAME_W = 1024;
    static constexpr int GAME_H = 768;

    // ============================================================
    // 座標轉換：相對座標 → 實際像素
    // 根據 HWND 客戶區大小動態調整
    // ============================================================
    inline Point ToAbsolute(Point rel, HWND hWnd) {
        if (!hWnd || !IsWindow(hWnd)) return rel;
        RECT rc;
        if (!GetClientRect(hWnd, &rc)) return rel;
        int w = rc.right;
        int h = rc.bottom;
        if (w <= 0 || h <= 0) return rel;
        int ax = (rel.x * w) / GAME_W;
        int az = (rel.z * h) / GAME_H;
        return Point(ax, az);
    }

    // 將 1024x768 基準座標轉為 HWND 內的實際像素座標
    inline bool ToClientCoords(HWND hWnd, Point gamePt, POINT* outScreen) {
        if (!hWnd || !outScreen || !IsWindow(hWnd)) return false;
        RECT rc;
        if (!GetClientRect(hWnd, &rc)) return false;
        int w = rc.right;
        int h = rc.bottom;
        if (w <= 0 || h <= 0) return false;

        int cx = (gamePt.x * w) / GAME_W;
        int cy = (gamePt.z * h) / GAME_H;

        // Clamp to client area
        if (cx < 0) cx = 0;
        if (cx >= w) cx = w - 1;
        if (cy < 0) cy = 0;
        if (cy >= h) cy = h - 1;

        POINT pt = { cx, cy };
        if (!ClientToScreen(hWnd, &pt)) return false;
        *outScreen = pt;
        return true;
    }

    // ============================================================
    // 安全邊界（遊戲內座標）
    // ============================================================
    static constexpr Point 左上 = Point(0, 0);
    static constexpr Point 右上 = Point(1000, 0);
    static constexpr Point 左下 = Point(10, 740);
    static constexpr Point 右下 = Point(1000, 740);

    // ============================================================
    // 復活相關（Win7 實測座標）
    // ============================================================
    static constexpr Point 歸魂珠復活 = Point(580, 400);
    static constexpr Point 復活按鈕 = Point(520, 400);
    static constexpr Point 基本復活 = Point(520, 400);

    // ============================================================
    // 聖門 NPC（Win7 實測座標）
    // ============================================================
    static constexpr Point NPC聖門特派員詹姆士 = Point(577, 140);
    static constexpr Point NPC聖門對話框購買物品 = Point(573, 398);
    static constexpr Point NPC聖門箭矢 = Point(723, 175);
    static constexpr Point NPC聖門符咒 = Point(563, 244);
    static constexpr Point NPC聖門消耗品 = Point(640, 243);
    static constexpr Point NPC聖門消耗品數量框 = Point(712, 247);
    static constexpr Point NPC聖門箭矢購買確認 = Point(585, 389);
    static constexpr Point NPC聖門消耗品購買確認 = Point(530, 414);

    // 商店格子
    static constexpr Point 聖門商店左上第一格 = Point(534, 285);
    static constexpr Point 聖門商店右上第一格 = Point(714, 284);
    static constexpr Point 聖門商店左下第一格 = Point(532, 537);
    static constexpr Point 聖門商店右下第一格 = Point(715, 539);

    static constexpr Point 聖門背包左上角第一格 = Point(773, 191);
    static constexpr Point 聖門背包右上角第一格 = Point(952, 189);
    static constexpr Point 聖門背包左下角第一格 = Point(773, 512);
    static constexpr Point 聖門背包右下角第一格 = Point(950, 513);

    // ============================================================
    // 商洞 NPC（Win7 實測座標）
    // ============================================================
    static constexpr Point NPC商洞特派員詹姆士 = Point(630, 295);
    static constexpr Point NPC商洞特派員詹姆士_FAR = Point(848, 172);

    // ============================================================
    // 玄巖 NPC（Win7 實測座標）
    // ============================================================
    static constexpr Point NPC玄巖特派員詹姆士 = Point(585, 207);
    static constexpr Point NPC玄巖對話框購買物品 = Point(587, 396);
    static constexpr Point NPC玄巖箭矢 = Point(565, 252);
    static constexpr Point NPC玄巖符咒 = Point(641, 252);
    static constexpr Point NPC玄巖消耗品 = Point(712, 252);

    // ============================================================
    // 鳳凰 NPC（Win7 實測座標）
    // ============================================================
    static constexpr Point NPC鳳凰特派員詹姆士 = Point(568, 247);
    static constexpr Point NPC鳳凰對話框購買物品 = Point(586, 396);

    // ============================================================
    // 賣物資按鈕（Win7 實測座標）
    // ============================================================
    static constexpr Point NPC賣物資 = Point(305, 355);
    static constexpr Point NPC賣物資選擇是按鈕 = Point(305, 405);

    // ============================================================
    // 寵物餵食（Win7 實測座標）
    // ============================================================
    static constexpr Point 寵物卡 = Point(775, 180);
    static constexpr Point 飼料 = Point(820, 180);

    // ============================================================
    // 戰鬥圓心 / 固定掃打點（Win7 實測座標）
    // ============================================================
    static constexpr Point 中心點 = Point(520, 390);
    static constexpr ScanPoint 點01 = ScanPoint(492, 365);
    static constexpr ScanPoint 點02 = ScanPoint(466, 379);
    static constexpr ScanPoint 點03 = ScanPoint(482, 443);
    static constexpr ScanPoint 點04 = ScanPoint(546, 452);
    static constexpr ScanPoint 點05 = ScanPoint(576, 414);
    static constexpr ScanPoint 點06 = ScanPoint(585, 401);
    static constexpr ScanPoint 點07 = ScanPoint(569, 335);
    static constexpr ScanPoint 點08 = ScanPoint(526, 345);

    // 攻擊掃描點
    static constexpr ScanPoint AttackScanPoints[] =
    {
        點01, 點02, 點03, 點04,
        點05, 點06, 點07, 點08
    };

    static constexpr int ATTACK_SCAN_COUNT = 8;

    inline const ScanPoint* GetAttackScanPoints() {
        return AttackScanPoints;
    }

    // ============================================================
    // 小工具
    // ============================================================
    inline int ClampX(int x) {
        return (x < 0) ? 0 : ((x >= GAME_W) ? (GAME_W - 1) : x);
    }

    inline int ClampZ(int z) {
        return (z < 0) ? 0 : ((z >= GAME_H) ? (GAME_H - 1) : z);
    }

    inline Point Clamp(const Point& p) {
        return Point(ClampX(p.x), ClampZ(p.z));
    }

    inline bool IsInsideGameArea(const Point& p) {
        return p.x >= 0 && p.x < GAME_W &&
               p.z >= 0 && p.z < GAME_H;
    }

    // 調試：取得 HWND 客戶區大小（用於診斷）
    inline bool GetGameWindowSize(HWND hWnd, int* outW, int* outH) {
        if (!hWnd || !IsWindow(hWnd)) return false;
        RECT rc;
        if (!GetClientRect(hWnd, &rc)) return false;
        if (outW) *outW = rc.right;
        if (outH) *outH = rc.bottom;
        return true;
    }

    // 賣物品相關座標
    inline Point GetNPCSellItemPos(int slotIndex = 0) {
        // 背包物品位置（從左到右、從上到下）
        static const Point itemSlots[] = {
            Point(773, 191), Point(797, 191), Point(821, 191), Point(845, 191), Point(869, 191), Point(893, 191), Point(917, 191), Point(941, 191),
            Point(773, 215), Point(797, 215), Point(821, 215), Point(845, 215), Point(869, 215), Point(893, 215), Point(917, 215), Point(941, 215),
            Point(773, 239), Point(797, 239), Point(821, 239), Point(845, 239), Point(869, 239), Point(893, 239), Point(917, 239), Point(941, 239),
        };
        if (slotIndex < 0 || slotIndex >= 24) return Point(0, 0);
        return itemSlots[slotIndex];
    }

    inline Point GetNPCSellConfirmPos() {
        return Point(520, 414);
    }
}
