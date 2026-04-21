#pragma once

// 建議此檔儲存為 UTF-8 with BOM
// 座標系統：統一使用 1024x768 作為遊戲內基準座標
// Win11/Win7 差異由用戶校準系統自動覆寫

#include <windows.h>

namespace Coords
{
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
    // 執行時 Windows 版本檢測（Win7/Win11 自動切換）
    // ============================================================
    inline bool IsWin11() {
        static bool s_isWin11 = []() {
            OSVERSIONINFOW osvi = {};
            osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFOW);

            HMODULE hNtDll = GetModuleHandleW(L"ntdll.dll");
            if (hNtDll) {
                typedef LONG(WINAPI* RtlGetVersionPtr)(OSVERSIONINFOW*);
                RtlGetVersionPtr rtlGetVersion = reinterpret_cast<RtlGetVersionPtr>(
                    GetProcAddress(hNtDll, "RtlGetVersion"));
                if (rtlGetVersion && rtlGetVersion(&osvi) == 0) {
                    // Win11: dwMajorVersion=10 且 dwBuildNumber>=22000
                    if (osvi.dwMajorVersion == 10 && osvi.dwBuildNumber >= 22000) {
                        return true;
                    }
                    return false;
                }
            }
            // Fallback: 用 GetVersionEx（可能被 SDK 模擬，但精確度較低）
            #pragma warning(push)
            #pragma warning(disable: 4996)
            OSVERSIONINFOW osviFallback = {};
            osviFallback.dwOSVersionInfoSize = sizeof(OSVERSIONINFOW);
            if (GetVersionExW(&osviFallback)) {
                if (osviFallback.dwMajorVersion == 10 && osviFallback.dwBuildNumber >= 22000) {
                    return true;
                }
            }
            #pragma warning(pop)
            return false;
        }();
        return s_isWin11;
    }

    inline bool IsWin7() {
        return !IsWin11();
    }

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
    // 復活相關（遊戲內絕對座標 1024x768）
    // 預設值：Win11/Win7 通用
    // 如有差異，由用戶校準系統覆寫
    // ============================================================

    // ---------- Win11 預設 ----------
    static constexpr Point 復活按鈕_WIN11 = Point(454, 408);
    static constexpr Point 歸魂珠復活_WIN11 = Point(572, 407);
    static constexpr Point 基本復活_WIN11 = Point(513, 407);

    // ---------- Win7 預設 ----------
    static constexpr Point 歸魂珠復活_WIN7 = Point(580, 400);
    static constexpr Point 復活按鈕_WIN7 = Point(520, 400);
    static constexpr Point 基本復活_WIN7 = Point(520, 400);

    inline Point 復活按鈕() { return IsWin11() ? 復活按鈕_WIN11 : 復活按鈕_WIN7; }
    inline Point 歸魂珠復活() { return IsWin11() ? 歸魂珠復活_WIN11 : 歸魂珠復活_WIN7; }
    inline Point 基本復活() { return IsWin11() ? 基本復活_WIN11 : 基本復活_WIN7; }

    // ============================================================
    // 聖門 NPC
    // ============================================================
    static constexpr Point NPC聖門特派員詹姆士 = Point(577, 140);
    static constexpr Point NPC聖門對話框購買物品 = Point(573, 398);

    // ---------- Win11 ----------
    static constexpr Point NPC聖門箭矢_WIN11 = Point(549, 251);
    static constexpr Point NPC聖門符咒_WIN11 = Point(627, 251);
    static constexpr Point NPC聖門消耗品_WIN11 = Point(699, 253);
    static constexpr Point NPC聖門消耗品數量框_WIN11 = Point(412, 417);
    static constexpr Point NPC聖門箭矢購買確認_WIN11 = Point(530, 416);
    static constexpr Point NPC聖門消耗品購買確認_WIN11 = Point(530, 418);

    // ---------- Win7 ----------
    static constexpr Point NPC聖門箭矢_WIN7 = Point(723, 175);
    static constexpr Point NPC聖門符咒_WIN7 = Point(563, 244);
    static constexpr Point NPC聖門消耗品_WIN7 = Point(640, 243);
    static constexpr Point NPC聖門消耗品數量框_WIN7 = Point(712, 247);
    static constexpr Point NPC聖門箭矢購買確認_WIN7 = Point(585, 389);
    static constexpr Point NPC聖門消耗品購買確認_WIN7 = Point(530, 414);

    inline Point NPC聖門箭矢() { return IsWin11() ? NPC聖門箭矢_WIN11 : NPC聖門箭矢_WIN7; }
    inline Point NPC聖門符咒() { return IsWin11() ? NPC聖門符咒_WIN11 : NPC聖門符咒_WIN7; }
    inline Point NPC聖門消耗品() { return IsWin11() ? NPC聖門消耗品_WIN11 : NPC聖門消耗品_WIN7; }
    inline Point NPC聖門消耗品數量框() { return IsWin11() ? NPC聖門消耗品數量框_WIN11 : NPC聖門消耗品數量框_WIN7; }
    inline Point NPC聖門箭矢購買確認() { return IsWin11() ? NPC聖門箭矢購買確認_WIN11 : NPC聖門箭矢購買確認_WIN7; }
    inline Point NPC聖門消耗品購買確認() { return IsWin11() ? NPC聖門消耗品購買確認_WIN11 : NPC聖門消耗品購買確認_WIN7; }

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
    // 商洞 NPC
    // ============================================================
    static constexpr Point NPC商洞特派員詹姆士_WIN11 = Point(612, 288);
    static constexpr Point NPC商洞特派員詹姆士_FAR_WIN11 = Point(835, 109);
    static constexpr Point NPC商洞特派員詹姆士_WIN7 = Point(630, 295);
    static constexpr Point NPC商洞特派員詹姆士_FAR_WIN7 = Point(848, 172);

    inline Point NPC商洞特派員詹姆士() { return IsWin11() ? NPC商洞特派員詹姆士_WIN11 : NPC商洞特派員詹姆士_WIN7; }
    inline Point NPC商洞特派員詹姆士_FAR() { return IsWin11() ? NPC商洞特派員詹姆士_FAR_WIN11 : NPC商洞特派員詹姆士_FAR_WIN7; }

    // ============================================================
    // 玄巖 NPC
    // ============================================================
    static constexpr Point NPC玄巖特派員詹姆士_WIN11 = Point(540, 200);
    static constexpr Point NPC玄巖特派員詹姆士_WIN7 = Point(585, 207);
    static constexpr Point NPC玄巖對話框購買物品_WIN11 = Point(576, 396);
    static constexpr Point NPC玄巖對話框購買物品_WIN7 = Point(587, 396);
    static constexpr Point NPC玄巖箭矢_WIN11 = Point(554, 252);
    static constexpr Point NPC玄巖符咒_WIN11 = Point(630, 252);
    static constexpr Point NPC玄巖消耗品_WIN11 = Point(701, 252);
    static constexpr Point NPC玄巖箭矢_WIN7 = Point(565, 252);
    static constexpr Point NPC玄巖符咒_WIN7 = Point(641, 252);
    static constexpr Point NPC玄巖消耗品_WIN7 = Point(712, 252);

    inline Point NPC玄巖特派員詹姆士() { return IsWin11() ? NPC玄巖特派員詹姆士_WIN11 : NPC玄巖特派員詹姆士_WIN7; }
    inline Point NPC玄巖對話框購買物品() { return IsWin11() ? NPC玄巖對話框購買物品_WIN11 : NPC玄巖對話框購買物品_WIN7; }
    inline Point NPC玄巖箭矢() { return IsWin11() ? NPC玄巖箭矢_WIN11 : NPC玄巖箭矢_WIN7; }
    inline Point NPC玄巖符咒() { return IsWin11() ? NPC玄巖符咒_WIN11 : NPC玄巖符咒_WIN7; }
    inline Point NPC玄巖消耗品() { return IsWin11() ? NPC玄巖消耗品_WIN11 : NPC玄巖消耗品_WIN7; }

    // ============================================================
    // 鳳凰 NPC
    // ============================================================
    static constexpr Point NPC鳳凰特派員詹姆士_WIN11 = Point(516, 316);
    static constexpr Point NPC鳳凰特派員詹姆士_WIN7 = Point(568, 247);
    static constexpr Point NPC鳳凰對話框購買物品_WIN11 = Point(575, 396);
    static constexpr Point NPC鳳凰對話框購買物品_WIN7 = Point(586, 396);

    inline Point NPC鳳凰特派員詹姆士() { return IsWin11() ? NPC鳳凰特派員詹姆士_WIN11 : NPC鳳凰特派員詹姆士_WIN7; }
    inline Point NPC鳳凰對話框購買物品() { return IsWin11() ? NPC鳳凰對話框購買物品_WIN11 : NPC鳳凰對話框購買物品_WIN7; }

    // ============================================================
    // 賣物資按鈕
    // ============================================================
    static constexpr Point NPC賣物資_WIN11 = Point(300, 350);
    static constexpr Point NPC賣物資_WIN7 = Point(305, 355);
    static constexpr Point NPC賣物資選擇是按鈕_WIN11 = Point(300, 400);
    static constexpr Point NPC賣物資選擇是按鈕_WIN7 = Point(305, 405);

    inline Point GetNPCSellItemPos() { return IsWin11() ? NPC賣物資_WIN11 : NPC賣物資_WIN7; }
    inline Point GetNPCSellConfirmPos() { return IsWin11() ? NPC賣物資選擇是按鈕_WIN11 : NPC賣物資選擇是按鈕_WIN7; }

    // ============================================================
    // 寵物餵食
    // ============================================================
    static constexpr Point 寵物卡_WIN11 = Point(774, 190);
    static constexpr Point 飼料_WIN11 = Point(810, 188);
    static constexpr Point 寵物卡_WIN7 = Point(775, 180);
    static constexpr Point 飼料_WIN7 = Point(820, 180);

    inline Point 寵物卡() { return IsWin11() ? 寵物卡_WIN11 : 寵物卡_WIN7; }
    inline Point 飼料() { return IsWin11() ? 飼料_WIN11 : 飼料_WIN7; }

    // ============================================================
    // 戰鬥圓心 / 固定掃打點（Win11）
    // ============================================================
    static constexpr Point 中心點_WIN11 = Point(500, 370);
    static constexpr ScanPoint 點01_WIN11 = ScanPoint(472, 345);
    static constexpr ScanPoint 點02_WIN11 = ScanPoint(446, 359);
    static constexpr ScanPoint 點03_WIN11 = ScanPoint(462, 423);
    static constexpr ScanPoint 點04_WIN11 = ScanPoint(526, 432);
    static constexpr ScanPoint 點05_WIN11 = ScanPoint(556, 394);
    static constexpr ScanPoint 點06_WIN11 = ScanPoint(565, 381);
    static constexpr ScanPoint 點07_WIN11 = ScanPoint(549, 315);
    static constexpr ScanPoint 點08_WIN11 = ScanPoint(506, 325);

    // Win7（用戶實測）
    static constexpr Point 中心點_WIN7 = Point(520, 390);
    static constexpr ScanPoint 點01_WIN7 = ScanPoint(492, 365);
    static constexpr ScanPoint 點02_WIN7 = ScanPoint(466, 379);
    static constexpr ScanPoint 點03_WIN7 = ScanPoint(482, 443);
    static constexpr ScanPoint 點04_WIN7 = ScanPoint(546, 452);
    static constexpr ScanPoint 點05_WIN7 = ScanPoint(576, 414);
    static constexpr ScanPoint 點06_WIN7 = ScanPoint(585, 401);
    static constexpr ScanPoint 點07_WIN7 = ScanPoint(569, 335);
    static constexpr ScanPoint 點08_WIN7 = ScanPoint(526, 345);

    inline Point 中心點() { return IsWin11() ? 中心點_WIN11 : 中心點_WIN7; }
    inline ScanPoint 點01() { return IsWin11() ? 點01_WIN11 : 點01_WIN7; }
    inline ScanPoint 點02() { return IsWin11() ? 點02_WIN11 : 點02_WIN7; }
    inline ScanPoint 點03() { return IsWin11() ? 點03_WIN11 : 點03_WIN7; }
    inline ScanPoint 點04() { return IsWin11() ? 點04_WIN11 : 點04_WIN7; }
    inline ScanPoint 點05() { return IsWin11() ? 點05_WIN11 : 點05_WIN7; }
    inline ScanPoint 點06() { return IsWin11() ? 點06_WIN11 : 點06_WIN7; }
    inline ScanPoint 點07() { return IsWin11() ? 點07_WIN11 : 點07_WIN7; }
    inline ScanPoint 點08() { return IsWin11() ? 點08_WIN11 : 點08_WIN7; }

    // 攻擊掃描點
    static constexpr ScanPoint AttackScanPoints_WIN11[] =
    {
        點01_WIN11, 點02_WIN11, 點03_WIN11, 點04_WIN11,
        點05_WIN11, 點06_WIN11, 點07_WIN11, 點08_WIN11
    };

    static constexpr ScanPoint AttackScanPoints_WIN7[] =
    {
        點01_WIN7, 點02_WIN7, 點03_WIN7, 點04_WIN7,
        點05_WIN7, 點06_WIN7, 點07_WIN7, 點08_WIN7
    };

    static constexpr int ATTACK_SCAN_COUNT = 8;

    inline const ScanPoint* GetAttackScanPoints() {
        return IsWin11() ? AttackScanPoints_WIN11 : AttackScanPoints_WIN7;
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
}
