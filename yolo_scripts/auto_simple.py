# ============================================================
# 自動標注腳本 - 修復版
# ============================================================
import cv2
import numpy as np
from pathlib import Path

IMAGES_DIR = Path("training_data/images")
LABELS_DIR = Path("training_data/labels")
LABELS_DIR.mkdir(parents=True, exist_ok=True)

def label_monster(img_path):
    img = cv2.imread(str(img_path))
    if img is None:
        return []
    h, w = img.shape[:2]
    result = []

    # 放寬 HP 血條檢測
    for y in range(int(h*0.05), int(h*0.7), 3):
        for x in range(int(w*0.05), int(w*0.95), 3):
            b, g, r = img[y, x]
            # HP 血條：紅色（放寬條件）
            if r > 150 and g < 100 and b < 100:
                # 計算血條寬度
                bar_w = 1
                for dx in range(1, 30):
                    if x+dx < w:
                        p = img[y, x+dx]
                        if p[2] > 150 and p[1] < 100:
                            bar_w += 1
                        else:
                            break
                # 血條寬度在 3-50 之間
                if 3 <= bar_w <= 50:
                    # 估算怪物區域
                    mx, my = x + bar_w//2, y
                    mw, mh = bar_w * 5, bar_w * 7
                    x1, y1 = max(0, mx-mw//2), max(0, my-10)
                    x2, y2 = min(w, mx+mw//2), min(h, my+mh)

                    if x2-x1 > 15 and y2-y1 > 15:
                        cx = (x1+x2)/2/w
                        cy = (y1+y2)/2/h
                        nw = (x2-x1)/w
                        nh = (y2-y1)/h
                        # 去重
                        is_dup = False
                        for _, acx, acy, _, _ in result:
                            if abs(acx - cx) < 0.1 and abs(acy - cy) < 0.1:
                                is_dup = True
                                break
                        if not is_dup:
                            result.append((0, cx, cy, nw, nh))
    return result[:15]

def label_shop(img_path):
    img = cv2.imread(str(img_path))
    if img is None:
        return []
    h, w = img.shape[:2]
    result = []

    # 金色文字（NPC 名字）
    for y in range(int(h*0.05), int(h*0.6), 10):
        for x in range(int(w*0.1), int(w*0.9), 10):
            b, g, r = img[y, x]
            if r > 150 and g > 120 and b < 100:
                result.append((2, (x+50)/w, (y+20)/h, 0.2, 0.08))
                break
        if result:
            break

    # 藍色/綠色按鈕
    for y in range(int(h*0.4), int(h*0.95), 10):
        for x in range(int(w*0.15), int(w*0.85), 10):
            b, g, r = img[y, x]
            if (g > 120 and r < 100) or (b > 120 and r < 100):
                result.append((3, (x+40)/w, (y+25)/h, 0.15, 0.08))
                break
        if len([r for r in result if r[0]==3]) > 0:
            break
    return result[:5]

def label_player(img_path):
    img = cv2.imread(str(img_path))
    if img is None:
        return []
    h, w = img.shape[:2]
    result = []

    # 頭像/名字
    for y in range(int(h*0.05), int(h*0.6), 12):
        for x in range(int(w*0.08), int(w*0.92), 12):
            b, g, r = img[y, x]
            # 白/金色邊框或綠/白名字
            if (r > 150 and g > 150) or (g > 130 and r > 130 and b < 100):
                # 去重
                is_dup = False
                for _, acx, _, _, _ in result:
                    if abs(acx - (x+30)/w) < 0.08:
                        is_dup = True
                        break
                if not is_dup:
                    result.append((1, (x+30)/w, (y+60)/h, 0.1, 0.2))
                    if len(result) >= 3:
                        break
        if len(result) >= 3:
            break
    return result[:3]

counts = {0: 0, 1: 0, 2: 0, 3: 0}

# 怪物
print("[1/3] Monster...")
frame_files = sorted(IMAGES_DIR.glob("frame_*.jpg"))
for i, img_path in enumerate(frame_files):
    anns = label_monster(img_path)
    name = img_path.stem
    lp = LABELS_DIR / f"{name}.txt"
    with open(lp, 'w') as f:
        for a in anns:
            f.write(f"{a[0]} {a[1]:.6f} {a[2]:.6f} {a[3]:.6f} {a[4]:.6f}\n")
            counts[a[0]] += 1
    if (i+1) % 200 == 0:
        print(f"    {i+1}/{len(frame_files)}")
print(f"    Done: {counts[0]} monster annotations")

# 商店
print("[2/3] Shop...")
for img_path in sorted(IMAGES_DIR.glob("shop_*.jpg")):
    anns = label_shop(img_path)
    name = img_path.stem
    lp = LABELS_DIR / f"{name}.txt"
    with open(lp, 'w') as f:
        for a in anns:
            f.write(f"{a[0]} {a[1]:.6f} {a[2]:.6f} {a[3]:.6f} {a[4]:.6f}\n")
            counts[a[0]] += 1
print(f"    Done: NPC={counts[2]}, Shop={counts[3]}")

# 玩家
print("[3/3] Player...")
for img_path in sorted(IMAGES_DIR.glob("player_*.jpg")):
    anns = label_player(img_path)
    name = img_path.stem
    lp = LABELS_DIR / f"{name}.txt"
    with open(lp, 'w') as f:
        for a in anns:
            f.write(f"{a[0]} {a[1]:.6f} {a[2]:.6f} {a[3]:.6f} {a[4]:.6f}\n")
            counts[a[0]] += 1
print(f"    Done: {counts[1]} player annotations")

print()
print("=" * 50)
print(f"Monster: {counts[0]}")
print(f"Player: {counts[1]}")
print(f"NPC: {counts[2]}")
print(f"Shop: {counts[3]}")
print("=" * 50)
