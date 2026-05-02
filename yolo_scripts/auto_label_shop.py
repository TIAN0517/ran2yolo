# ============================================================
# 商店圖片自動標注腳本
# 使用模板匹配和位置檢測自動標注商店 NPC
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
CLASS_NPC = 2      # NPC
CLASS_SHOP = 3     # 商店

def auto_label_shop_image(img_path):
    """
    自動標注商店圖片
    檢測：NPC名字、NPC箭頭、購買按鈕、對話框
    """
    img = cv2.imread(str(img_path))
    if img is None:
        return []

    h, w = img.shape[:2]
    annotations = []

    # ===== 檢測 1: NPC 名字區域（通常在上方，有特定顏色）=====
    # 掃描上半部分，找類似名字牌的區域
    for y in range(int(h * 0.1), int(h * 0.5), 5):
        for x in range(int(w * 0.2), int(w * 0.8), 5):
            if y >= h or x >= w:
                continue
            pixel = img[y, x]
            b, g, r = pixel

            # 檢測金色/黃色文字（NPC 名字常用色）
            if r > 180 and g > 150 and g < 220 and b < 100:
                # 找到潛在名字區域，擴展邊界
                x1, y1 = x, y
                x2, y2 = x, y

                # 向右擴展（找文字寬度）
                for dx in range(50):
                    if x + dx < w:
                        p = img[y, x + dx]
                        if p[0] < 150:  # 還有文字
                            x2 = x + dx

                # 向下擴展（找文字高度）
                for dy in range(20):
                    if y + dy < h:
                        p = img[y + dy, x]
                        if p[0] < 150:
                            y2 = y + dy

                if x2 - x1 > 20 and y2 - y1 > 5:
                    # 轉換為 YOLO 格式
                    cx = (x1 + x2) / 2 / w
                    cy = (y1 + y2) / 2 / h
                    nw = (x2 - x1) / w
                    nh = (y2 - y1) / h

                    # 確保在範圍內
                    cx = max(0.01, min(0.99, cx))
                    cy = max(0.01, min(0.99, cy))
                    nw = max(0.01, min(0.99, nw))
                    nh = max(0.01, min(0.99, nh))

                    annotations.append((CLASS_NPC, cx, cy, nw, nh))
                    break
            if annotations:
                break

    # ===== 檢測 2: 對話框區域（通常在中間，藍色邊框）=====
    # 掃描中上部，找藍色邊框區域
    dialog_found = False
    for y in range(int(h * 0.3), int(h * 0.7), 10):
        for x in range(int(w * 0.1), int(w * 0.9), 10):
            if y >= h or x >= w:
                continue
            pixel = img[y, x]
            b, g, r = pixel

            # 檢測藍色邊框
            if b > 150 and r < 100 and g < 100:
                # 找到對話框，估算整個對話框區域
                x1 = max(0, x - 50)
                y1 = max(0, y - 50)
                x2 = min(w, x + 300)
                y2 = min(h, y + 200)

                if (x2 - x1) > 100 and (y2 - y1) > 50:
                    cx = (x1 + x2) / 2 / w
                    cy = (y1 + y2) / 2 / h
                    nw = (x2 - x1) / w
                    nh = (y2 - y1) / h

                    annotations.append((CLASS_SHOP, cx, cy, nw, nh))
                    dialog_found = True
                    break
        if dialog_found:
            break

    # ===== 檢測 3: 按鈕區域（通常在下方，綠色/藍色）=====
    for y in range(int(h * 0.6), int(h * 0.9), 5):
        for x in range(int(w * 0.2), int(w * 0.8), 5):
            if y >= h or x >= w:
                continue
            pixel = img[y, x]
            b, g, r = pixel

            # 檢測綠色/藍色按鈕
            if (g > 150 and r < 100 and b < 120) or (b > 150 and g < 100 and r < 100):
                # 找到按鈕，擴展
                x1 = x - 30
                y1 = y - 10
                x2 = x + 80
                y2 = y + 25

                x1 = max(0, x1)
                y1 = max(0, y1)
                x2 = min(w, x2)
                y2 = min(h, y2)

                if (x2 - x1) > 30 and (y2 - y1) > 15:
                    cx = (x1 + x2) / 2 / w
                    cy = (y1 + y2) / 2 / h
                    nw = (x2 - x1) / w
                    nh = (y2 - y1) / h

                    annotations.append((CLASS_SHOP, cx, cy, nw, nh))
                    break
        if len([a for a in annotations if a[0] == CLASS_SHOP]) > 1:
            break

    return annotations[:5]  # 最多5個標注

def process_all_shop_images():
    """處理所有 shop_ 圖片"""
    LABELS_DIR.mkdir(parents=True, exist_ok=True)

    print("=" * 60)
    print("  商店圖片自動標注")
    print("=" * 60)

    shop_images = sorted(IMAGES_DIR.glob("shop_*.jpg"))
    print(f"\n找到 {len(shop_images)} 張 shop 圖片")

    total_annotations = 0
    images_with_annotations = 0

    for i, img_path in enumerate(shop_images):
        annotations = auto_label_shop_image(img_path)

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
            print(f"  進度: {i + 1}/{len(shop_images)} | 標注數: {total_annotations}")

    print()
    print("=" * 60)
    print(f"  完成！")
    print(f"  處理圖片: {len(shop_images)}")
    print(f"  有標注圖片: {images_with_annotations}")
    print(f"  總標注數: {total_annotations}")
    print("=" * 60)

    return images_with_annotations, total_annotations

if __name__ == "__main__":
    process_all_shop_images()
