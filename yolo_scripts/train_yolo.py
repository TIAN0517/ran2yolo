# ============================================================
# YOLO 訓練腳本 - RAN2 目標識別
# 使用 YOLOv8 進行目標檢測訓練
# ============================================================
import os
import shutil
import yaml
from pathlib import Path

# ============================================================
# 設定
# ============================================================
PROJECT_DIR = Path(__file__).parent
TRAIN_DATA_DIR = PROJECT_DIR / "training_data"
IMAGES_DIR = TRAIN_DATA_DIR / "images"
LABELS_DIR = TRAIN_DATA_DIR / "labels"

# 類別（繁體中文說明）
CLASSES = ["monster", "player", "npc", "npc_shop"]

# 訓練參數
IMG_SIZE = 640
BATCH_SIZE = 16
EPOCHS = 100
MODEL_NAME = "yolov8n.pt"  # 使用 yolov8n.pt 或 yolo26n.pt

# ============================================================
# 建立資料集設定檔
# ============================================================
def create_dataset_yaml():
    yaml_content = {
        'path': str(TRAIN_DATA_DIR.absolute()),
        'train': 'images',
        'val': 'images',
        'test': 'images',
        'nc': len(CLASSES),
        'names': {i: cls for i, cls in enumerate(CLASSES)}
    }

    yaml_path = TRAIN_DATA_DIR / "dataset.yaml"
    with open(yaml_path, 'w', encoding='utf-8') as f:
        yaml.dump(yaml_content, f, allow_unicode=True, default_flow_style=False)

    print(f"[OK] 資料集設定檔: {yaml_path}")
    return yaml_path

# ============================================================
# 驗證資料集
# ============================================================
def validate_dataset():
    print("\n========== 驗證資料集 ==========")

    # 統計圖片和標籤
    image_files = list(IMAGES_DIR.glob("*.jpg")) + list(IMAGES_DIR.glob("*.png"))
    label_files = list(LABELS_DIR.glob("*.txt"))

    print(f"圖片數量: {len(image_files)}")
    print(f"標籤檔案: {len(label_files)}")

    if len(image_files) == 0:
        print("[錯誤] 找不到圖片！")
        return False

    # 檢查圖片和標籤匹配
    matched = 0
    missing_labels = 0
    for img in image_files[:10]:
        label = LABELS_DIR / (img.stem + ".txt")
        if label.exists():
            matched += 1
        else:
            missing_labels += 1

    print(f"前10張圖片中: {matched} 張有標籤, {missing_labels} 張缺少標籤")

    if missing_labels > 0:
        print("[提示] 有圖片缺少標籤，請使用 simple_labeler.py 標註")

    # 統計各類別數量
    print("\n類別統計:")
    class_counts = {cls: 0 for cls in CLASSES}

    for label_file in label_files:
        with open(label_file, 'r', encoding='utf-8', errors='ignore') as f:
            for line in f:
                parts = line.strip().split()
                if parts:
                    try:
                        cls_id = int(parts[0])
                        if 0 <= cls_id < len(CLASSES):
                            class_counts[CLASSES[cls_id]] += 1
                    except:
                        pass

    for cls, count in class_counts.items():
        print(f"  {cls}: {count}")

    # 檢查類別平衡
    counts = list(class_counts.values())
    if counts:
        max_count = max(counts)
        min_count = min(counts)
        if min_count == 0:
            print("\n⚠️  警告: 有類別完全沒有標註！")
        elif max_count / min_count > 10:
            print(f"\n⚠️  警告: 類別不平衡 (最大/最小 = {max_count/min_count:.1f}x)")

    return True

# ============================================================
# 開始訓練
# ============================================================
def train_yolo():
    print("========== YOLO 訓練 ==========\n")

    # 驗證資料集
    if not validate_dataset():
        return

    # 建立設定檔
    yaml_path = create_dataset_yaml()

    print("\n========== 開始訓練 ==========")
    print(f"模型: {MODEL_NAME}")
    print(f"圖片大小: {IMG_SIZE}")
    print(f"批次大小: {BATCH_SIZE}")
    print(f"訓練輪數: {EPOCHS}")
    print(f"類別: {CLASSES}")
    print("=" * 40)

    try:
        from ultralytics import YOLO

        # 載入預訓練模型
        model = YOLO(str(PROJECT_DIR / MODEL_NAME))

        # 開始訓練
        results = model.train(
            data=str(yaml_path),
            epochs=EPOCHS,
            imgsz=IMG_SIZE,
            batch=BATCH_SIZE,
            project=str(PROJECT_DIR / "runs"),
            name='detect/train',
            exist_ok=True,
            verbose=True,
            patience=15,  # 早停
            save=True,
            save_period=10,  # 每10輪儲存
            plots=True,  # 生成訓練圖表
        )

        print("\n" + "=" * 40)
        print("[完成] 訓練完成！")

        # 複製最佳模型
        best_model = PROJECT_DIR / "runs" / "detect" / "train" / "weights" / "best.pt"
        best_onnx = PROJECT_DIR / "runs" / "detect" / "train" / "weights" / "best.onnx"
        export_onnx = PROJECT_DIR / "models" / "best.onnx"

        # 確保 models 目錄存在
        export_onnx.parent.mkdir(exist_ok=True)

        if best_model.exists():
            print(f"最佳模型: {best_model}")

            # 匯出為 ONNX
            print("\n匯出 ONNX 格式...")
            try:
                model.export(format='onnx', imgsz=IMG_SIZE)
                # 移動到 models 目錄
                if best_onnx.exists():
                    shutil.copy(best_onnx, export_onnx)
                    print(f"✅ ONNX 模型已儲存: {export_onnx}")
            except Exception as e:
                print(f"⚠️  匯出失敗: {e}")
                print("請手動執行: ultralytics export model=best.pt format=onnx")

    except ImportError:
        print("\n[錯誤] 請先安裝 ultralytics:")
        print("  pip install ultralytics")
    except Exception as e:
        print(f"\n[錯誤] {e}")
        import traceback
        traceback.print_exc()

# ============================================================
# 主程式
# ============================================================
if __name__ == "__main__":
    print("=" * 50)
    print("  YOLO 訓練腳本 - RAN2 目標識別")
    print("=" * 50)

    # 檢查依賴
    try:
        import ultralytics
        print(f"[OK] ultralytics {ultralytics.__version__}")
    except ImportError:
        print("\n[安裝] 正在安裝 ultralytics...")
        os.system("pip install ultralytics")
        print("[OK] 安裝完成")

    train_yolo()
