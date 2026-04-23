# ============================================================
# YOLO 怪物檢測訓練腳本 - 高容忍度版
# 使用 YOLOv8m + 數據增強 + 大輸入尺寸
# ============================================================
import os
import shutil
import yaml
from pathlib import Path

PROJECT_DIR = Path(__file__).parent
TRAIN_DATA_DIR = PROJECT_DIR / "training_data"
IMAGES_DIR = TRAIN_DATA_DIR / "images"
LABELS_DIR = TRAIN_DATA_DIR / "labels"

# 訓練設定
MODEL_NAME = "yolov8n.pt"      # 輕量快速模型
IMG_SIZE = 640                   # 輸入尺寸
BATCH_SIZE = 16                  # 批次大小
EPOCHS = 50                     # 訓練輪數
PATIENCE = 10                   # 早停耐心值

def validate_data():
    """驗證數據"""
    print("\n========== 驗證數據 ==========")

    frame_files = list(IMAGES_DIR.glob("frame_*.jpg"))
    print(f"截圖數量: {len(frame_files)}")

    monster_count = 0
    for lf in LABELS_DIR.glob("frame_*.txt"):
        with open(lf, 'r', encoding='utf-8', errors='ignore') as f:
            monster_count += sum(1 for line in f if line.strip())

    print(f"怪物標註: {monster_count}")

    if len(frame_files) < 100:
        print("[錯誤] 數據太少！")
        return False

    return True

def create_config():
    """建立訓練設定"""
    config = {
        'path': str(TRAIN_DATA_DIR.absolute()),
        'train': 'images',
        'val': 'images',
        'test': 'images',
        'nc': 1,
        'names': {0: 'monster'}
    }

    yaml_path = TRAIN_DATA_DIR / "monster.yaml"
    with open(yaml_path, 'w', encoding='utf-8') as f:
        yaml.dump(config, f, default_flow_style=False)

    return str(yaml_path)

def train():
    """開始訓練"""
    print("=" * 60)
    print("  YOLO 怪物檢測訓練 - 高容忍度版")
    print("=" * 60)

    if not validate_data():
        return

    yaml_path = create_config()

    print("\n========== 訓練設定 ==========")
    print(f"模型: {MODEL_NAME}")
    print(f"圖片大小: {IMG_SIZE}")
    print(f"批次大小: {BATCH_SIZE}")
    print(f"訓練輪數: {EPOCHS}")
    print(f"數據增強: 開啟")
    print("=" * 60)

    try:
        from ultralytics import YOLO
        model = YOLO(str(PROJECT_DIR / MODEL_NAME))

        print("\n開始訓練...")
        results = model.train(
            data=yaml_path,
            epochs=EPOCHS,
            imgsz=IMG_SIZE,
            batch=BATCH_SIZE,
            project=str(PROJECT_DIR / "runs"),
            name='monster_train',
            exist_ok=True,
            verbose=True,
            patience=PATIENCE,

            # 數據增強 - 提高容忍度
            augment=True,
            hsv_h=0.015,
            hsv_s=0.7,
            hsv_v=0.4,
            degrees=10.0,
            translate=0.1,
            scale=0.5,
            shear=0.0,
            flipud=0.5,
            fliplr=0.5,
            mosaic=1.0,
            mixup=0.1,

            save=True,
            save_period=10,
            plots=True,
        )

        print("\n" + "=" * 60)
        print("[完成] 訓練完成！")

        # 複製模型
        best_pt = PROJECT_DIR / "runs" / "monster_train" / "weights" / "best.pt"
        final_onnx = PROJECT_DIR / "models" / "best.onnx"
        final_onnx.parent.mkdir(exist_ok=True)

        if best_pt.exists():
            print(f"最佳模型: {best_pt}")

            print("\n匯出 ONNX...")
            try:
                export_model = YOLO(str(best_pt))
                export_model.export(format='onnx', imgsz=IMG_SIZE)

                src_onnx = PROJECT_DIR / "runs" / "monster_train" / "weights" / "best.onnx"
                if src_onnx.exists():
                    shutil.copy(src_onnx, final_onnx)
                    print(f"模型已儲存: {final_onnx}")
            except Exception as e:
                print(f"匯出失敗: {e}")

    except ImportError:
        print("\n[錯誤] 請先安裝: pip install ultralytics")
    except Exception as e:
        print(f"\n[錯誤] {e}")
        import traceback
        traceback.print_exc()

if __name__ == "__main__":
    print("=" * 60)
    print("  YOLO 怪物檢測訓練")
    print("=" * 60)

    try:
        import ultralytics
        print(f"[OK] ultralytics {ultralytics.__version__}")
    except ImportError:
        print("[安裝] pip install ultralytics...")
        os.system("pip install ultralytics")

    train()
