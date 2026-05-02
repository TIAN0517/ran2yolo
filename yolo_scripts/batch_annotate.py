# ============================================================
# 批量自動標註工具 - RAN2 YOLO 訓練
# 自動偵測 HP 血條來識別怪物位置
# ============================================================
import os
import cv2
from pathlib import Path

# 設定
PROJECT_DIR = Path(__file__).parent
IMAGES_DIR = PROJECT_DIR / "training_data" / "images"
LABELS_DIR = PROJECT_DIR / "training_data" / "labels"

# 血條識別參數（怪物）
HP_MIN_WIDTH = 3
HP_MAX_WIDTH = 6
HP_MIN_HEIGHT = 8
HP_MAX_HEIGHT = 15
HP_R_THRESH = 200
HP_G_THRESH = 80
HP_B_THRESH = 80

def is_hp_bar_color(pixel):
    """判斷是否為 HP 血條顏色（紅色）"""
    r, g, b = pixel[2], pixel[1], pixel[0]
    return (r > HP_R_THRESH and g < HP_G_THRESH and b < HP_B_THRESH)

def get_monster_candidates(img):
    """掃描圖片，透過血條偵測怪物候選位置"""
    if img is None:
        return []

    h, w = img.shape[:2]
    candidates = []

    scan_top = min(50, h // 10)
    scan_bottom = min(h // 2, h - 20)

    if scan_bottom <= scan_top:
        return candidates

    for y in range(scan_top, scan_bottom, 2):
        for x in range(10, w - 10, 2):
            if y >= h or x >= w:
                continue
            b, g, r = img[y, x]
            if r > 200 and g < 80 and b < 80:
                # 計算血條寬度
                bar_width = 1
                for dx in range(1, 10):
                    if x + dx < w:
                        b2, g2, r2 = img[y, x + dx]
                        if r2 > 200 and g2 < 80 and b2 < 80:
                            bar_width += 1
                        else:
                            break

                if bar_width < 3 or bar_width > 15:
                    continue

                # 計算血條高度
                bar_height = 1
                for dy in range(1, 15):
                    if y + dy < h:
                        b2, g2, r2 = img[y + dy, x]
                        if r2 > 200 and g2 < 80 and b2 < 80:
                            bar_height += 1
                        else:
                            break

                # 去重
                is_dup = False
                for cx, cy, _, _ in candidates:
                    if abs(x - cx) < 20 and abs(y - cy) < 10:
                        is_dup = True
                        break

                if not is_dup:
                    candidates.append((x, y, bar_width, bar_height))

    return candidates

def process_image(img_path, class_id=0):
    """處理單張圖片，生成 YOLO 標註"""
    img = cv2.imread(str(img_path))
    if img is None:
        return 0

    h, w = img.shape[:2]
    if h < 200 or w < 200:
        return 0

    candidates = get_monster_candidates(img)

    label_path = LABELS_DIR / (img_path.stem + ".txt")

    with open(label_path, 'w', encoding='utf-8') as f:
        for x, y, bar_w, bar_h in candidates:
            monster_h = bar_h * 10
            monster_w = bar_w * 8

            cx = (x + bar_w // 2) / w
            cy = (y + bar_h + monster_h // 2) / h
            nw = monster_w / w
            nh = monster_h / h

            cx = max(0, min(1, cx))
            cy = max(0, min(1, cy))
            nw = max(0.01, min(1, nw))
            nh = max(0.01, min(1, nh))

            f.write(f"{class_id} {cx:.6f} {cy:.6f} {nw:.6f} {nh:.6f}\n")

    return len(candidates)

def main():
    LABELS_DIR.mkdir(parents=True, exist_ok=True)

    print("=" * 50)
    print("  RAN2 批量自動標註工具")
    print("=" * 50)
    print(f"圖片目錄: {IMAGES_DIR}")
    print(f"標籤目錄: {LABELS_DIR}")
    print()

    # 獲取所有圖片
    images = sorted(
        list(IMAGES_DIR.glob("*.jpg")) +
        list(IMAGES_DIR.glob("*.png"))
    )

    print(f"找到 {len(images)} 張圖片")
    print()

    total_objects = 0
    images_with_objects = 0

    for i, img_path in enumerate(images):
        count = process_image(img_path, class_id=0)  # 0 = monster
        total_objects += count
        if count > 0:
            images_with_objects += 1

        if (i + 1) % 100 == 0 or i == 0:
            print(f"進度: {i + 1}/{len(images)} | 偵測到目標: {count} | 總計: {total_objects}")

    print()
    print("=" * 50)
    print(f"完成！")
    print(f"處理圖片: {len(images)}")
    print(f"有目標圖片: {images_with_objects}")
    print(f"偵測到的目標總數: {total_objects}")
    print("=" * 50)

    # 統計每張圖的目標數
    object_counts = {}
    for label_file in LABELS_DIR.glob("*.txt"):
        with open(label_file, encoding='utf-8', errors='ignore') as f:
            count = len([l for l in f if l.strip()])
            object_counts[count] = object_counts.get(count, 0) + 1

    print("\n目標數量分布:")
    for count in sorted(object_counts.keys()):
        print(f"  {count} 個目標: {object_counts[count]} 張圖")

if __name__ == "__main__":
    main()
