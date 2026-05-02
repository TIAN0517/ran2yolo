#pragma once
// coords.h — 1024x768 相對座標（視窗內座標）

#include <windows.h>

namespace Coords
{
    struct Point {
        int x;
        int z;
        constexpr Point(int px = 0, int pz = 0) : x(px), z(pz) {}
    };

    using ScanPoint = Point;

    static constexpr int GAME_W = 1024;  // 統一使用 1024x768
    static constexpr int GAME_H = 768;

    // ============================================================
    // 復活相關
    // ============================================================
    static constexpr int 復活_SX = 450;
    static constexpr int 復活_SZ = 410;
    static constexpr int 使用歸魂珠_SX = 570;
    static constexpr int 使用歸魂珠_SZ = 410;
    static constexpr int 復活1_SX = 510;
    static constexpr int 復活1_SZ = 410;

    // 復活座標
    static constexpr Point 歸魂珠復活 = Point(570, 410);
    static constexpr Point 復活按鈕 = Point(450, 410);
    static constexpr Point 基本復活 = Point(510, 410);

    // ============================================================
    // 戰鬥圓心
    // ============================================================
    static constexpr int 中心點_SX = 510;
    static constexpr int 中心點_SZ = 380;
    static constexpr Point 中心點 = Point(510, 380);

    // ============================================================
    // 練功點（8個等半徑小圓，中心 510,380，半徑約 60px）
    // ============================================================
    static constexpr ScanPoint 點01 = ScanPoint(570, 380);  // 右
    static constexpr ScanPoint 點02 = ScanPoint(552, 422);  // 右下
    static constexpr ScanPoint 點03 = ScanPoint(510, 440);  // 下
    static constexpr ScanPoint 點04 = ScanPoint(468, 422);  // 左下
    static constexpr ScanPoint 點05 = ScanPoint(450, 380);  // 左
    static constexpr ScanPoint 點06 = ScanPoint(468, 338);  // 左上
    static constexpr ScanPoint 點07 = ScanPoint(510, 320);  // 上
    static constexpr ScanPoint 點08 = ScanPoint(552, 338);  // 右上

    // 向後相容性別名
    static constexpr ScanPoint OFFSET_點01 = 點01;
    static constexpr ScanPoint OFFSET_點02 = 點02;
    static constexpr ScanPoint OFFSET_點03 = 點03;
    static constexpr ScanPoint OFFSET_點04 = 點04;
    static constexpr ScanPoint OFFSET_點05 = 點05;
    static constexpr ScanPoint OFFSET_點06 = 點06;
    static constexpr ScanPoint OFFSET_點07 = 點07;
    static constexpr ScanPoint OFFSET_點08 = 點08;

    static constexpr ScanPoint AttackScanPointOffsets[] = {
        點01, 點02, 點03, 點04, 點05, 點06, 點07, 點08
    };

    static constexpr int ATTACK_SCAN_COUNT = 8;
    static constexpr int POINTS_PER_CIRCLE = 8;
    static constexpr int MS_PER_POINT = 100;
    static constexpr int MS_PER_CIRCLE = 800;

    inline ScanPoint GetAttackPoint(int index) {
        if (index < 0 || index >= ATTACK_SCAN_COUNT) return 點01;
        return AttackScanPointOffsets[index];
    }

    inline int GetCircleIndex(int pointIndex) {
        if (pointIndex < 0 || pointIndex >= ATTACK_SCAN_COUNT) return 0;
        return pointIndex / POINTS_PER_CIRCLE;
    }

    inline int GetPointInCircle(int pointIndex) {
        if (pointIndex < 0 || pointIndex >= ATTACK_SCAN_COUNT) return 0;
        return pointIndex % POINTS_PER_CIRCLE;
    }

    inline ScanPoint GetAttackPoint(int index, const Point& center) {
        if (index < 0 || index >= ATTACK_SCAN_COUNT) {
            return ScanPoint(center.x, center.z);
        }
        const ScanPoint& offset = AttackScanPointOffsets[index];
        return ScanPoint(center.x + (offset.x - 中心點.x),
                         center.z + (offset.z - 中心點.z));
    }

    inline void GetAllAttackPoints(ScanPoint points[]) {
        for (int i = 0; i < ATTACK_SCAN_COUNT; i++) {
            points[i] = AttackScanPointOffsets[i];
        }
    }

    inline const ScanPoint* GetAttackScanPoints() {
        return AttackScanPointOffsets;
    }

    // ============================================================
    // NPC/商店座標（預設值）
    // ============================================================
    static constexpr Point NPC聖門特派員詹姆士 = Point(500, 500);
    static constexpr Point NPC聖門對話框購買物品 = Point(500, 500);
    static constexpr Point NPC聖門箭矢 = Point(510, 500);
    static constexpr Point NPC聖門符咒 = Point(520, 500);
    static constexpr Point NPC聖門消耗品 = Point(530, 500);
    static constexpr Point NPC聖門箭矢購買確認 = Point(510, 520);

    static constexpr Point NPC商洞特派員詹姆士 = Point(500, 500);
    static constexpr Point NPC玄巖特派員詹姆士 = Point(500, 500);
    static constexpr Point NPC玄巖對話框購買物品 = Point(500, 500);
    static constexpr Point NPC玄巖箭矢 = Point(510, 500);
    static constexpr Point NPC玄巖符咒 = Point(520, 500);
    static constexpr Point NPC玄巖消耗品 = Point(530, 500);

    static constexpr Point NPC鳳凰特派員詹姆士 = Point(500, 500);
    static constexpr Point NPC鳳凰對話框購買物品 = Point(500, 500);

    // ============================================================
    // 寵物餵食
    // ============================================================
    static constexpr Point 寵物卡 = Point(800, 230);
    static constexpr Point 食料 = Point(850, 230);

    // ============================================================
    // NPC 賣物格子座標
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

    inline Point GetTradeRejectPos() {
        return Point(580, 380);
    }

    inline Point GetDialogClosePos() {
        return Point(500, 450);
    }

    inline Point GetPetFeedSlot(int slotIndex = 0) {
        if (slotIndex == 0) return 寵物卡;
        else if (slotIndex == 1) return 食料;
        return Point(800 + slotIndex * 50, 230);
    }

    // ============================================================
    // 安全邊界
    // ============================================================
    static constexpr Point 左上 = Point(0, 0);
    static constexpr Point 右上 = Point(1024, 0);
    static constexpr Point 左下 = Point(10, 758);
    static constexpr Point 右下 = Point(1024, 758);
} // namespace Coords
