# ============================================================
# 快速標註工具 - NPC / 玩家 / 商店
# 用於補充缺少的類別標註
# ============================================================
import os
import cv2
from pathlib import Path

# 設定
PROJECT_DIR = Path(__file__).parent
IMAGES_DIR = PROJECT_DIR / "training_data" / "images"
LABELS_DIR = PROJECT_DIR / "training_data" / "labels"

# 類別定義
CLASSES = {
    "怪物": 0,
    "玩家": 1,
    "NPC": 2,
    "商店": 3
}

def analyze_static_images():
    """分析靜態圖片，自動生成標註"""
    LABELS_DIR.mkdir(parents=True, exist_ok=True)

    # 玩家相關圖片
    player_images = [
        "player.png", "Player_pkt.png", "Player_red_name.png",
        "QWEASD.png", "player.txt"
    ]

    # NPC 相關圖片
    npc_images = [
        "npc.png", "npc1.png", "npcc.png", "NPC_NAME.png",
        "mob.png", "mob_name.png", "mobname.png", "mob_hp.png"
    ]

    # 商店相關圖片
    shop_images = [
        "buy.png", "dialog.png", "charm.png", "arrow.png"
    ]

    results = []

    for img_name, images_list, class_id, class_name in [
        (player_images, player_images, 1, "玩家"),
        (npc_images, npc_images, 2, "NPC"),
        (shop_images, shop_images, 3, "商店")
    ]:
        for img_name in images_list:
            img_path = IMAGES_DIR / img_name
            if not img_name.endswith('.png'):
                img_path = IMAGES_DIR / (img_name + ".png")

            if img_path.exists():
                img = cv2.imread(str(img_path))
                if img is not None:
                    h, w = img.shape[:2]
                    if h > 20 and w > 20:
                        # 自動生成標註（全圖）
                        label_path = LABELS_DIR / img_path.stem
                        if not str(label_path).endswith('.txt'):
                            label_path = Path(str(label_path) + ".txt")

                        # 根據圖片大小估算目標位置
                        # 典型 UI 元素在中間偏上
                        cx, cy = 0.5, 0.3
                        nw, nh = min(w / w, 0.4), min(h / h, 0.3)

                        with open(label_path, 'w', encoding='utf-8') as f:
                            f.write(f"{class_id} {cx:.6f} {cy:.6f} {nw:.6f} {nh:.6f}\n")

                        results.append(f"✅ {img_name} -> class {class_id} ({class_name})")
                    else:
                        results.append(f"⚠️  {img_name} 圖片太小 ({w}x{h})")
                else:
                    results.append(f"❌ 無法讀取: {img_name}")
            else:
                results.append(f"🔍 找不到: {img_name}")

    return results

def print_usage():
    """打印使用說明"""
    print("=" * 60)
    print("  RAN2 快速標註工具")
    print("=" * 60)
    print()
    print("這個腳本會分析 training_data/images/ 中的靜態圖片，")
    print("並根據檔案名稱自動分類生成標註。")
    print()
    print("檔案命名規則：")
    print("  player*, Player*  -> 玩家 (class 1)")
    print("  npc*, NPC*, mob*  -> NPC (class 2)")
    print("  buy*, dialog*, shop* -> 商店 (class 3)")
    print()
    print("=" * 60)
    print()

def main():
    print_usage()

    print("開始分析靜態圖片...")
    print()

    results = analyze_static_images()

    print("\n處理結果：")
    for r in results:
        print(f"  {r}")

    # 統計
    print("\n" + "=" * 60)
    print("統計：")
    total = len(results)
    success = len([r for r in results if r.startswith("✅")])
    failed = len([r for r in results if r.startswith("❌")])
    missing = len([r for r in results if r.startswith("🔍")])

    print(f"  總計: {total}")
    print(f"  成功: {success}")
    print(f"  失敗: {failed}")
    print(f"  找不到: {missing}")
    print("=" * 60)

    print("\n建議：")
    print("1. 手動標註工具: python simple_labeler.py")
    print("2. 批量自動標註: python batch_annotate.py")
    print("3. 確認標註正確後重新訓練: python train_yolo.py")

if __name__ == "__main__":
    main()
