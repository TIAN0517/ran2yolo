# ============================================================
# 智能自動標注腳本 - 增強版
# 使用多策略提高標注準確度
# ============================================================
import cv2
import numpy as np
from pathlib import Path

IMAGES_DIR = Path("training_data/images")
LABELS_DIR = Path("training_data/labels")
LABELS_DIR.mkdir(parents=True, exist_ok=True)

def find_hp_bars(img):
    """找到所有 HP 血條"""
    h, w = img.shape[:2]
    bars = []

    for y in range(int(h * 0.05), int(h * 0.8), 2):
        for x in range(int(w * 0.05), int(w * 0.95), 2):
            b, g, r = img[y, x]

            # HP 血條特徵：紅色
            if r > 150 and g < 100 and b < 100:
                # 計算血條寬度
                bar_w = 0
                for dx in range(40):
                    if x + dx >= w:
                        break
                    nb, ng, nr = img[y, x + dx]
                    if nr > 150 and ng < 100 and nb < 100:
                        bar_w += 1
                    else:
                        break

                # 計算血條高度
                bar_h = 0
                for dy in range(20):
                    if y + dy >= h:
                        break
                    nb, ng, nr = img[y + dy, x]
                    if nr > 150 and ng < 100 and nb < 100:
                        bar_h += 1
                    else:
                        break

                if 5 <= bar_w <= 50 and 3 <= bar_h <= 20:
                    bars.append((x, y, bar_w, bar_h, r, g, b))

    # 去重（合併重疊的血條）
    merged = []
    for bar in bars:
        bx, by, bw, bh, br, bg, bb = bar
        is_dup = False
        for mx, my, mw, mh, mr, mg, mb in merged:
            if abs(bx - mx) < 20 and abs(by - my) < 10:
                is_dup = True
                break
        if not is_dup:
            merged.append(bar)

    return merged

def estimate_monster_box(bar_x, bar_y, bar_w, bar_h, img_w, img_h):
    """根據血條估算怪物邊界框"""

    # 血條通常在怪物頭頂上方
    # 怪物高度通常是血條的 8-15 倍
    # 怪物寬度通常是血條的 5-10 倍

    # 估算怪物中心位置（血條下方）
    monster_center_x = bar_x + bar_w // 2
    monster_center_y = bar_y + bar_h + bar_w * 6  # 血條下方約 6 倍血條寬

    # 估算怪物大小
    monster_w = bar_w * 6
    monster_h = bar_h * 10

    # 確保邊界合理
    x1 = max(0, monster_center_x - monster_w // 2)
    y1 = max(0, monster_center_y - monster_h // 2)
    x2 = min(img_w, monster_center_x + monster_w // 2)
    y2 = min(img_h, monster_center_y + monster_h // 2)

    # 轉換為 YOLO 格式
    cx = (x1 + x2) / 2 / img_w
    cy = (y1 + y2) / 2 / img_h
    nw = (x2 - x1) / img_w
    nh = (y2 - y1) / img_h

    # 確保值在有效範圍
    cx = max(0.001, min(0.999, cx))
    cy = max(0.001, min(0.999, cy))
    nw = max(0.01, min(0.5, nw))
    nh = max(0.01, min(0.5, nh))

    return (cx, cy, nw, nh)

def label_image(img_path):
    """標注單張圖片"""
    img = cv2.imread(str(img_path))
    if img is None:
        return []

    h, w = img.shape[:2]
    bars = find_hp_bars(img)

    annotations = []
    for bar in bars:
        bx, by, bw, bh, br, bg, bb = bar
        cx, cy, nw, nh = estimate_monster_box(bx, by, bw, bh, w, h)

        # 去重
        is_dup = False
        for acx, acy, anw, anh in annotations:
            if abs(acx - cx) < 0.08 and abs(acy - cy) < 0.08:
                is_dup = True
                break
        if not is_dup:
            annotations.append((cx, cy, nw, nh))

    return annotations

def main():
    print("=" * 60)
    print("  智能自動標注 - 增強版")
    print("=" * 60)

    # 只處理 frame_ 圖片
    frame_files = sorted(IMAGES_DIR.glob("frame_*.jpg"))

    print(f"找到 {len(frame_files)} 張圖片")

    total_ann = 0
    empty_count = 0

    for i, img_path in enumerate(frame_files):
        annotations = label_image(img_path)

        label_path = LABELS_DIR / (img_path.stem + ".txt")
        with open(label_path, 'w') as f:
            for cx, cy, nw, nh in annotations:
                f.write(f"0 {cx:.6f} {cy:.6f} {nw:.6f} {nh:.6f}\n")

        total_ann += len(annotations)
        if len(annotations) == 0:
            empty_count += 1

        if (i + 1) % 100 == 0:
            print(f"  進度: {i + 1}/{len(frame_files)} | 標注: {total_ann}")

    print()
    print("=" * 60)
    print(f"完成！")
    print(f"總標注數: {total_ann}")
    print(f"空標注圖片: {empty_count}")
    print("=" * 60)

if __name__ == "__main__":
    main()
