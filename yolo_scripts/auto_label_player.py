# ============================================================
# 玩家圖片自動標注腳本
# 使用頭像/名字檢測自動標注其他玩家
# ============================================================
import os
import cv2
import numpy as np
from pathlib import Path

# 設定
PROJECT_DIR = Path(__file__).parent
IMAGES_DIR = PROJECT_DIR / "training_data" / "images"
LABELS_DIR = PROJECT_DIR / "training_data" / "labels"

# 類別
CLASS_PLAYER = 1  # 玩家

def auto_label_player_image(img_path):
    """
    自動標注玩家圖片
    檢測：玩家頭像框、玩家名字牌
    """
    img = cv2.imread(str(img_path))
    if img is None:
        return []

    h, w = img.shape[:2]
    annotations = []

    # ===== 檢測 1: 玩家頭像框（通常是圓形/方形頭像）=====
    # 頭像通常在角色上方，大小相對固定
    for y in range(int(h * 0.15), int(h * 0.5), 8):
        for x in range(int(w * 0.15), int(w * 0.85), 8):
            if y >= h or x >= w:
                continue
            pixel = img[y, x]
            b, g, r = pixel

            # 檢測可能是頭像框的白色/金色邊框
            if r > 180 and g > 180 and b > 180:
                # 找到潛在頭像，估算大小
                x1 = max(0, x - 20)
                y1 = max(0, y - 20)
                x2 = min(w, x + 25)
                y2 = min(h, y + 30)

                if (x2 - x1) > 15 and (y2 - y1) > 15:
                    cx = (x1 + x2) / 2 / w
                    cy = (y1 + y2) / 2 / h
                    nw = (x2 - x1) / w
                    nh = (y2 - y1) / h

                    # 擴展到整個玩家（頭像下方區域）
                    player_y2 = min(h, y + 100)
                    player_cy = (y1 + player_y2) / 2 / h
                    player_nh = (player_y2 - y1) / h

                    annotations.append((CLASS_PLAYER, cx, player_cy, nw, player_nh))
                    break
        if annotations:
            break

    # ===== 檢測 2: 玩家名字牌（綠色/白色名字）=====
    for y in range(int(h * 0.2), int(h * 0.6), 5):
        for x in range(int(w * 0.1), int(w * 0.9), 5):
            if y >= h or x >= w:
                continue
            pixel = img[y, x]
            b, g, r = pixel

            # 檢測綠色名字（普通玩家）或白色名字
            if (g > 150 and r > 150 and b < 100) or (r > 200 and g > 200 and b > 200):
                # 估算玩家區域
                x1 = max(0, x - 40)
                y1 = max(0, y - 30)
                x2 = min(w, x + 40)
                y2 = min(h, y + 80)

                if (x2 - x1) > 20 and (y2 - y1) > 20:
                    cx = (x1 + x2) / 2 / w
                    cy = (y1 + y2) / 2 / h
                    nw = (x2 - x1) / w
                    nh = (y2 - y1) / h

                    # 去重
                    is_dup = False
                    for a in annotations:
                        if abs(a[1] - cx) < 0.1 and abs(a[2] - cy) < 0.1:
                            is_dup = True
                            break

                    if not is_dup:
                        annotations.append((CLASS_PLAYER, cx, cy, nw, nh))
                        break
        if len(annotations) >= 2:
            break

    return annotations[:3]  # 最多3個標注

def process_all_player_images():
    """處理所有 player_ 圖片"""
    LABELS_DIR.mkdir(parents=True, exist_ok=True)

    print("=" * 60)
    print("  玩家圖片自動標注")
    print("=" * 60)

    player_images = sorted(IMAGES_DIR.glob("player_*.jpg"))
    print(f"\n找到 {len(player_images)} 張 player 圖片")

    total_annotations = 0
    images_with_annotations = 0

    for i, img_path in enumerate(player_images):
        annotations = auto_label_player_image(img_path)

        label_path = LABELS_DIR / img_path.stem
        if not str(label_path).endswith('.txt'):
            label_path = Path(str(label_path) + ".txt")

        with open(label_path, 'w', encoding='utf-8') as f:
            for cls_id, cx, cy, nw, nh in annotations:
                f.write(f"{cls_id} {cx:.6f} {cy:.6f} {nw:.6f} {nh:.6f}\n")

        if annotations:
            total_annotations += len(annotations)
            images_with_annotations += 1

        if (i + 1) % 20 == 0 or i == 0:
            print(f"  進度: {i + 1}/{len(player_images)} | 標注數: {total_annotations}")

    print()
    print("=" * 60)
    print(f"  完成！")
    print(f"  處理圖片: {len(player_images)}")
    print(f"  有標注圖片: {images_with_annotations}")
    print(f"  總標注數: {total_annotations}")
    print("=" * 60)

    return images_with_annotations, total_annotations

if __name__ == "__main__":
    process_all_player_images()
