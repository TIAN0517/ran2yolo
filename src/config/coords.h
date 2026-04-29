#pragma once
// coords.h — 預設座標常數（純資料，無邏輯）
// 座標轉換邏輯統一在 coord_calib.h 的 CoordConv 命名空間
// 此檔案僅存放預設值，不做實際計算

#include <windows.h>

namespace Coords
{
    // ============================================================
    // 基本座標型別
    // 遊戲內座標使用標準化相對座標 0-1000
    // ============================================================
    struct Point {
        int x;   // 0-999 (對應 1000 寬)
        int z;   // 0-999 (對應 1000 高)
        constexpr Point(int px = 0, int pz = 0) : x(px), z(pz) {}
    };

    using ScanPoint = Point;

    // ============================================================
    // 標準化相對座標範圍
    // ============================================================
    static constexpr int GAME_W = 1000;
    static constexpr int GAME_H = 1000;

    // ============================================================
    // 安全邊界（遊戲內座標）
    // ============================================================
    static constexpr Point 左上 = Point(0, 0);
    static constexpr Point 右上 = Point(1000, 0);
    static constexpr Point 左下 = Point(10, 990);
    static constexpr Point 右下 = Point(1000, 990);

    // ============================================================
    // 復活相關（標準化相對座標 0-1000）
    // ============================================================
    static constexpr Point 歸魂珠復活 = Point(570, 410);  // 使用歸魂珠
    static constexpr Point 復活按鈕 = Point(450, 410);    // 復活
    static constexpr Point 基本復活 = Point(510, 410);    // 復活1

    // ============================================================
    // NPC/商店座標（預設值，可由 coord_calib.h 校正）
    // ============================================================
    // 聖門 NPC
    static constexpr Point NPC聖門特派員詹姆士 = Point(500, 500);
    static constexpr Point NPC聖門對話框購買物品 = Point(500, 500);
    static constexpr Point NPC聖門箭矢 = Point(510, 500);
    static constexpr Point NPC聖門符咒 = Point(520, 500);
    static constexpr Point NPC聖門消耗品 = Point(530, 500);
    static constexpr Point NPC聖門箭矢購買確認 = Point(510, 520);

    // 商洞 NPC
    static constexpr Point NPC商洞特派員詹姆士 = Point(500, 500);

    // 玄巖 NPC
    static constexpr Point NPC玄巖特派員詹姆士 = Point(500, 500);
    static constexpr Point NPC玄巖對話框購買物品 = Point(500, 500);
    static constexpr Point NPC玄巖箭矢 = Point(510, 500);
    static constexpr Point NPC玄巖符咒 = Point(520, 500);
    static constexpr Point NPC玄巖消耗品 = Point(530, 500);

    // 鳳凰 NPC
    static constexpr Point NPC鳳凰特派員詹姆士 = Point(500, 500);
    static constexpr Point NPC鳳凰對話框購買物品 = Point(500, 500);

    // ============================================================
    // 寵物餵食（標準化相對座標）
    // ============================================================
    static constexpr Point 寵物卡 = Point(800, 230);
    static constexpr Point 食料 = Point(850, 230);

    // ============================================================
    // 戰鬥圓心 / 固定掃打點（標準化相對座標 0-1000）
    // 版本 1：中心點 (500, 380)，半徑 80
    // ============================================================
    static constexpr Point 中心點 = Point(500, 380);
    // 半徑 80
    static constexpr ScanPoint 點01 = ScanPoint(500, 300);   // 上
    static constexpr ScanPoint 點02 = ScanPoint(557, 337);   // 右上
    static constexpr ScanPoint 點03 = ScanPoint(580, 380);   // 右
    static constexpr ScanPoint 點04 = ScanPoint(557, 423);   // 右下
    static constexpr ScanPoint 點05 = ScanPoint(500, 460);   // 下
    static constexpr ScanPoint 點06 = ScanPoint(443, 423);   // 左下
    static constexpr ScanPoint 點07 = ScanPoint(420, 380);   // 左
    static constexpr ScanPoint 點08 = ScanPoint(443, 337);   // 左上

    static constexpr ScanPoint AttackScanPoints[] = {
        點01, 點02, 點03, 點04,
        點05, 點06, 點07, 點08
    };

    static constexpr int ATTACK_SCAN_COUNT = 8;

    inline const ScanPoint* GetAttackScanPoints() {
        return AttackScanPoints;
    }

    // ============================================================
    // NPC 賣物格子座標（標準化相對座標）
    // ============================================================
    inline Point GetNPCSellItemPos(int slotIndex = 0) {
        static const Point itemSlots[] = {
            Point(780, 200), Point(805, 200), Point(830, 200), Point(855, 200),
            Point(880, 200), Point(905, 200), Point(930, 200), Point(955, 200),
            Point(780, 235), Point(805, 235), Point(830, 235), Point(855, 235),
            Point(880, 235), Point(905, 235), Point(930, 235), Point(955, 235),
            Point(780, 270), Point(805, 270), Point(830, 270), Point(855, 270),
            Point(880, 270), Point(905, 270), Point(930, 270), Point(955, 270),
        };
        if (slotIndex < 0) return Point(0, 0);
        if (slotIndex < 24) return itemSlots[slotIndex];
        return Point(780 + (slotIndex % 8) * 25, 200 + ((slotIndex / 8) % 3) * 35);
    }

    inline Point GetNPCSellConfirmPos() {
        return Point(500, 530);
    }

    // 交易拒絕按鈕（相對座標，需要校正）
    inline Point GetTradeRejectPos() {
        return Point(580, 380);
    }

    // 對話框關閉/取消按鈕
    inline Point GetDialogClosePos() {
        return Point(500, 450);
    }

    // 寵物餵食格子座標
    inline Point GetPetFeedSlot(int slotIndex = 0) {
        // 寵物卡片槽和食料槽的座標
        if (slotIndex == 0) {
            return 寵物卡;  // Point(800, 230)
        } else if (slotIndex == 1) {
            return 食料;    // Point(850, 230)
        }
        return Point(800 + slotIndex * 50, 230);
    }
} // namespace Coords
