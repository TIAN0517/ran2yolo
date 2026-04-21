// ============================================================
// 視覺實體掃描模組（Win7~Win11 通用）
// 純像素掃描取代記憶體讀取
// ============================================================
#include "visionentity.h"
#include "screenshot.h"
#include <vector>
#include <cmath>

// ============================================================
// 血條識別參數
// ============================================================
static const int HP_MIN_WIDTH = 3;
static const int HP_MAX_WIDTH = 6;
static const int HP_MIN_HEIGHT = 8;
static const int HP_MAX_HEIGHT = 15;
static const int HP_R_THRESH = 200;
static const int HP_G_THRESH = 80;
static const int HP_B_THRESH = 80;

// ============================================================
// 內部結構：血條候選
// ============================================================
struct HPBarCandidate {
    int x, y;        // 血條頂部中心
    int width;       // 血條寬度
    int height;      // 血條高度
    int hpPct;       // 血量百分比
};

// ============================================================
// 檢查像素是否為 HP 條顏色
// ============================================================
static inline bool IsHPBarColor(const uint8_t* pixel) {
    int r = pixel[2];
    int g = pixel[1];
    int b = pixel[0];
    return (r > HP_R_THRESH && g < HP_G_THRESH && b < HP_B_THRESH);
}

// ============================================================
// 檢查是否為背景/怪物身體顏色（用於排除漂浮 HP 條）
// ============================================================
static inline bool IsMonsterBodyColor(const uint8_t* pixel) {
    int r = pixel[2];
    int g = pixel[1];
    int b = pixel[0];

    // 跳過純黑（輪廓）
    if (r < 30 && g < 30 && b < 30) return false;
    // 跳過純白（高光）
    if (r > 240 && g > 240 && b > 240) return false;
    // 跳過 HP 條本身
    if (r > HP_R_THRESH && g < HP_G_THRESH && b < HP_B_THRESH) return false;

    // 有其他顏色 = 可能是怪物身體
    return (r > 50 || g > 50 || b > 50);
}

// ============================================================
// 估算 HP 百分比（血條剩餘長度 / 血條總長）
// ============================================================
static int EstimateHPPercent(const uint8_t* pixels, int w, int h, int barX, int barY, int barWidth) {
    // 從左往右掃描，找第一個非 HP 顏色的位置
    int filledWidth = 0;
    for (int x = barX - barWidth / 2; x < barX + barWidth / 2; x++) {
        if (x < 0 || x >= w) break;
        int idx = (barY * w + x) * 4;
        if (IsHPBarColor(&pixels[idx])) {
            filledWidth++;
        } else {
            break;
        }
    }

    if (barWidth == 0) return 100;
    int pct = (filledWidth * 100) / barWidth;
    if (pct > 100) pct = 100;
    if (pct < 0) pct = 0;
    return pct;
}

// ============================================================
// 檢查血條下方是否有怪物身體
// ============================================================
static bool HasMonsterBodyBelow(const uint8_t* pixels, int w, int h,
                                 int barX, int barY, int barWidth) {
    int checkTop = barY + 2;
    int checkBottom = barY + 22;
    int checkLeft = barX - barWidth;
    int checkRight = barX + barWidth;

    int bodyPixelCount = 0;
    for (int y = checkTop; y <= checkBottom && y < h; y++) {
        for (int x = checkLeft; x <= checkRight && x < w; x++) {
            if (x < 0 || x >= w) continue;
            int idx = (y * w + x) * 4;
            if (IsMonsterBodyColor(&pixels[idx])) {
                bodyPixelCount++;
                if (bodyPixelCount >= 8) return true;
            }
        }
    }
    return false;
}

// ============================================================
// 估算怪物優先級（y 越大越近優先）
// ============================================================
int EstimateMonsterPriority(const VisualMonster* m, int screenH) {
    if (!m) return 0;
    // 螢幕下方（y 較大）= 離玩家越近 = 優先攻擊
    int yPriority = m->screenY;
    // 血量越低越優先（預判擊殺）
    int hpPriority = (100 - m->hpPct) / 10;
    return yPriority + hpPriority;
}

// ============================================================
// 對怪物列表按優先級排序（選擇排序）
// ============================================================
void SortVisualMonsters(VisualMonster* monsters, int count) {
    if (!monsters || count <= 1) return;

    for (int i = 0; i < count - 1; i++) {
        int maxIdx = i;
        int maxPriority = EstimateMonsterPriority(&monsters[i], 768);

        for (int j = i + 1; j < count; j++) {
            int pri = EstimateMonsterPriority(&monsters[j], 768);
            if (pri > maxPriority) {
                maxPriority = pri;
                maxIdx = j;
            }
        }

        if (maxIdx != i) {
            VisualMonster tmp = monsters[i];
            monsters[i] = monsters[maxIdx];
            monsters[maxIdx] = tmp;
        }
    }
}

// ============================================================
// 掃描畫面所有怪物血條
// ============================================================
int ScanVisualMonsters(HWND hWnd, VisualMonster* outMonsters, int maxMonsters) {
    if (!hWnd || !outMonsters || maxMonsters <= 0) return 0;

    RECT rc;
    if (!GetClientRect(hWnd, &rc)) return 0;
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0) return 0;

    // 截圖
    std::vector<uint8_t> pixels;
    if (!CaptureGameWindow(hWnd, pixels, w, h)) return 0;

    std::vector<HPBarCandidate> candidates;
    candidates.reserve(64);

    // 只掃描視窗上半部分（怪物血條通常在頭頂）
    const int scanTop = 50;
    const int scanBottom = h / 2 + 50;

    // 滑動視窗掃描
    for (int y = scanTop; y < scanBottom - HP_MAX_HEIGHT; y += 2) {
        for (int x = 10; x < w - 10; x += 2) {
            int idx = (y * w + x) * 4;
            if (!IsHPBarColor(&pixels[idx])) continue;

            // 找到 HP 條候選，往下計算高度
            int barHeight = 0;
            for (int dy = 0; dy < HP_MAX_HEIGHT && y + dy < h; dy++) {
                int checkIdx = ((y + dy) * w + x) * 4;
                if (IsHPBarColor(&pixels[checkIdx])) {
                    barHeight++;
                } else {
                    break;
                }
            }

            if (barHeight < HP_MIN_HEIGHT || barHeight > HP_MAX_HEIGHT) continue;

            // 計算血條寬度
            int barWidth = 1;
            for (int dx = 1; dx < HP_MAX_WIDTH && x + dx < w; dx++) {
                int checkIdx = (y * w + (x + dx)) * 4;
                if (IsHPBarColor(&pixels[checkIdx])) {
                    barWidth++;
                } else {
                    break;
                }
            }

            if (barWidth < HP_MIN_WIDTH || barWidth > HP_MAX_WIDTH) continue;

            // 檢查血條下方是否有怪物身體（排除漂浮 HP 條）
            if (!HasMonsterBodyBelow(pixels.data(), w, h, x, y + barHeight, barWidth)) continue;

            // 估算 HP 百分比
            int hpPct = EstimateHPPercent(pixels.data(), w, h, x, y, barWidth);

            // 添加候選（去重：與已有候選距離太近則跳過）
            bool isDuplicate = false;
            for (const auto& c : candidates) {
                int dx = abs(x - c.x);
                int dy = abs(y - c.y);
                if (dx < 10 && dy < 10) {
                    isDuplicate = true;
                    break;
                }
            }

            if (!isDuplicate) {
                HPBarCandidate cand = {x, y, barWidth, barHeight, hpPct};
                candidates.push_back(cand);
            }
        }
    }

    // 轉換為 VisualMonster
    int count = 0;
    for (size_t i = 0; i < candidates.size() && count < maxMonsters; i++) {
        const auto& c = candidates[i];

        VisualMonster m;
        m.screenX = c.x;
        m.screenY = c.y + c.height;  // 點擊血條下方
        m.relX = (c.x * 1024) / w;
        m.relY = ((c.y + c.height) * 768) / h;
        m.hpPct = c.hpPct;
        m.width = c.width;
        m.priority = EstimateMonsterPriority(&m, h);

        outMonsters[count++] = m;
    }

    // 按優先級排序
    SortVisualMonsters(outMonsters, count);

    return count;
}

// ============================================================
// 讀取視覺玩家狀態（HP/MP/SP 百分比）
// ============================================================
bool ReadVisualPlayerState(HWND hWnd, VisualPlayerState* outState) {
    if (!hWnd || !outState) return false;

    memset(outState, 0, sizeof(*outState));
    outState->found = false;

    RECT rc;
    if (!GetClientRect(hWnd, &rc)) return false;
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0) return false;

    // 截圖
    std::vector<uint8_t> pixels;
    if (!CaptureGameWindow(hWnd, pixels, w, h)) return false;

    // HP/MP/SP 條通常在視窗底部狀態列
    // 根據解析度調整搜尋區域
    const int statusBarTop = h - 80;
    const int statusBarBottom = h - 20;

    // 計算 HP 條百分比
    int hpMaxWidth = 0;
    int hpCurrentWidth = 0;
    int barStart = -1;

    for (int y = statusBarTop; y < statusBarBottom; y++) {
        for (int x = 0; x < w; x++) {
            int idx = (y * w + x) * 4;
            int r = pixels[idx + 2];
            int g = pixels[idx + 1];
            int b = pixels[idx + 0];

            // HP 條：紅色
            if (r > HP_R_THRESH && g < HP_G_THRESH && b < HP_B_THRESH) {
                if (barStart < 0) barStart = x;
                hpCurrentWidth = x - barStart + 1;
                if (hpCurrentWidth > hpMaxWidth) hpMaxWidth = hpCurrentWidth;
            } else {
                barStart = -1;
            }
        }
    }

    // HP 條：紅色 (R > 200 且 G < 80 且 B < 80)
    if (hpMaxWidth > 0) {
        outState->hpPct = (hpMaxWidth * 100) / HP_MAX_WIDTH;
        if (outState->hpPct > 100) outState->hpPct = 100;
        if (outState->hpPct < 0) outState->hpPct = 0;
        outState->found = true;  // 只有實際讀到 HP 條才標 found
    }
    // 若 hpMaxWidth == 0，表示畫面中找不到 HP 條，不設 found=true

    // MP/SP 條識別（簡化版）
    // MP 條：藍色 (B > 200, R < 80, G < 80)
    int mpMaxWidth = 0;
    barStart = -1;
    for (int y = statusBarTop; y < statusBarBottom; y++) {
        for (int x = 0; x < w; x++) {
            int idx = (y * w + x) * 4;
            int r = pixels[idx + 2];
            int g = pixels[idx + 1];
            int b = pixels[idx + 0];

            if (b > 200 && r < 80 && g < 80) {
                if (barStart < 0) barStart = x;
                int mpWidth = x - barStart + 1;
                if (mpWidth > mpMaxWidth) mpMaxWidth = mpWidth;
            } else {
                barStart = -1;
            }
        }
    }
    if (mpMaxWidth > 0) {
        outState->mpPct = (mpMaxWidth * 100) / HP_MAX_WIDTH;
        if (outState->mpPct > 100) outState->mpPct = 100;
    }

    // SP 條：綠色 (G > 200, R < 80, B < 80)
    int spMaxWidth = 0;
    barStart = -1;
    for (int y = statusBarTop; y < statusBarBottom; y++) {
        for (int x = 0; x < w; x++) {
            int idx = (y * w + x) * 4;
            int r = pixels[idx + 2];
            int g = pixels[idx + 1];
            int b = pixels[idx + 0];

            if (g > 200 && r < 80 && b < 80) {
                if (barStart < 0) barStart = x;
                int spWidth = x - barStart + 1;
                if (spWidth > spMaxWidth) spMaxWidth = spWidth;
            } else {
                barStart = -1;
            }
        }
    }
    if (spMaxWidth > 0) {
        outState->spPct = (spMaxWidth * 100) / HP_MAX_WIDTH;
        if (outState->spPct > 100) outState->spPct = 100;
    }

    // found=true 表示成功讀到 HP 條；沒讀到則 found=false，回傳 false
    return outState->found;
}
