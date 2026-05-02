// ============================================================
// visionentity.cpp
// 視覺實體掃描模組
// 使用統一截圖 + 像素掃描
// ============================================================
#include "visionentity.h"
#include "screenshot_universal.h"
#include <vector>
#include <cmath>
#include <cstring>

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
    int x, y;
    int width;
    int height;
    int hpPct;
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
// 估算 HP 百分比
// ============================================================
static int EstimateHPPercent(const uint8_t* pixels, int w, int h, int barX, int barY, int barWidth) {
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
// 估算怪物優先級
// ============================================================
int EstimateMonsterPriority(const VisualMonster* m, int screenH) {
    if (!m) return 0;
    int yPriority = m->screenY;
    int hpPriority = (100 - m->hpPct) / 10;
    return yPriority + hpPriority;
}

// ============================================================
// 排序怪物列表
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

    // 截圖
    auto cap = ScreenshotUniversal::Capture(hWnd);
    if (!cap.success) return 0;

    int w = cap.width;
    int h = cap.height;
    const uint8_t* pixels = cap.pixels.data();

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

            // 往下計算高度
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

            // 估算 HP 百分比
            int hpPct = EstimateHPPercent(pixels, w, h, x, y, barWidth);

            // 去重
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
        m.screenY = c.y + c.height;
        m.relX = (c.x * 1024) / w;
        m.relY = ((c.y + c.height) * 768) / h;
        m.hpPct = c.hpPct;
        m.width = c.width;
        m.priority = EstimateMonsterPriority(&m, h);

        outMonsters[count++] = m;
    }

    SortVisualMonsters(outMonsters, count);

    return count;
}

// ============================================================
// 讀取視覺玩家狀態
// ============================================================
bool ReadVisualPlayerState(HWND hWnd, VisualPlayerState* outState) {
    if (!hWnd || !outState) return false;

    memset(outState, 0, sizeof(*outState));
    outState->found = false;

    // 截圖
    auto cap = ScreenshotUniversal::Capture(hWnd);
    if (!cap.success) return false;

    int w = cap.width;
    int h = cap.height;
    const uint8_t* pixels = cap.pixels.data();

    // HP/MP/SP 條通常在視窗底部狀態列
    const int statusBarTop = h - 80;
    const int statusBarBottom = h - 20;

    // HP 條計算
    int hpMaxWidth = 0;
    int barStart = -1;

    for (int y = statusBarTop; y < statusBarBottom; y++) {
        for (int x = 0; x < w; x++) {
            int idx = (y * w + x) * 4;
            int r = pixels[idx + 2];
            int g = pixels[idx + 1];
            int b = pixels[idx + 0];

            if (r > HP_R_THRESH && g < HP_G_THRESH && b < HP_B_THRESH) {
                if (barStart < 0) barStart = x;
                int hpWidth = x - barStart + 1;
                if (hpWidth > hpMaxWidth) hpMaxWidth = hpWidth;
            } else {
                barStart = -1;
            }
        }
    }

    const int HP_MAX_WIDTH = 200;
    if (hpMaxWidth > 0 && hpMaxWidth <= HP_MAX_WIDTH) {
        outState->hpPct = (hpMaxWidth * 100) / HP_MAX_WIDTH;
        if (outState->hpPct > 100) outState->hpPct = 100;
        if (outState->hpPct < 0) outState->hpPct = 0;
        outState->found = true;
    }

    // MP/SP (預設)
    outState->mpPct = 100;
    outState->spPct = 100;

    return outState->found;
}
