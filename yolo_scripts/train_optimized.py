# ============================================================
# YOLOv8n 優化訓練腳本
# 目標：保持速度 + 提升精度
# ============================================================
import os
import shutil
import yaml
from pathlib import Path

PROJECT_DIR = Path(__file__).parent
TRAIN_DATA_DIR = PROJECT_DIR / "training_data"
IMAGES_DIR = TRAIN_DATA_DIR / "images"
LABELS_DIR = TRAIN_DATA_DIR / "labels"
MODEL_PATH = PROJECT_DIR / "yolov8n.pt"

# 優化設定
IMG_SIZE = 1280      # 較大的輸入尺寸 = 更精準
BATCH_SIZE = 8       # 較小的批次（因為輸入更大）
EPOCHS = 100          # 更多訓練輪數
PATIENCE = 20         # 早停耐心

def clean_bad_labels():
    """清理無效/錯誤的標籤"""
    print("[清理] 檢查並清理無效標籤...")

    removed = 0
    for label_path in LABELS_DIR.glob("*.txt"):
        try:
            # 刪除空檔案
            if label_path.stat().st_size == 0:
                label_path.unlink()
                removed += 1
                continue

            # 檢查標籤格式
            with open(label_path, 'r') as f:
                lines = f.readlines()

            valid_lines = []
            for line in lines:
                parts = line.strip().split()
                if len(parts) >= 5:
                    try:
                        cls = int(parts[0])
                        cx, cy, nw, nh = map(float, parts[1:5])
                        # 過濾不合理的大小
                        if 0 < nw < 1 and 0 < nh < 1 and 0 <= cx <= 1 and 0 <= cy <= 1:
                            valid_lines.append(line)
                    except:
                        pass

            # 重寫有效標籤
            with open(label_path, 'w') as f:
                f.writelines(valid_lines)

            if len(valid_lines) == 0:
                label_path.unlink()
                removed += 1

        except Exception as e:
            pass

    print(f"[清理] 移除 {removed} 個無效標籤")

def create_config():
    """建立訓練設定"""
    config = {
        'path': str(TRAIN_DATA_DIR.absolute()),
        'train': 'images',
        'val': 'images',
        'nc': 1,
        'names': {0: 'monster'}
    }

    yaml_path = TRAIN_DATA_DIR / "monster.yaml"
    with open(yaml_path, 'w', encoding='utf-8') as f:
        yaml.dump(config, f, default_flow_style=False)
    return str(yaml_path)

def train():
    """執行訓練"""
    print("=" * 60)
    print("  YOLOv8n 優化訓練")
    print("=" * 60)
    print(f"輸入尺寸: {IMG_SIZE} (越大越精準)")
    print(f"訓練輪數: {EPOCHS}")
    print("=" * 60)

    # 清理數據
    clean_bad_labels()

    yaml_path = create_config()

    try:
        from ultralytics import YOLO
        model = YOLO(str(MODEL_PATH))

        print("\n開始訓練...")

        results = model.train(
            data=yaml_path,
            epochs=EPOCHS,
            imgsz=IMG_SIZE,
            batch=BATCH_SIZE,
            project=str(PROJECT_DIR / "runs"),
            name='monster_optimized',
            exist_ok=True,
            verbose=True,
            patience=PATIENCE,

            # 增強設定
            augment=True,
            hsv_h=0.02,         # 色調增強
            hsv_s=0.6,          # 飽和度增強
            hsv_v=0.4,          # 亮度增強
            degrees=15.0,        # 旋轉範圍（增大）
            translate=0.2,       # 平移範圍（增大）
            scale=0.6,           # 縮放範圍（增大）
            shear=5.0,           # 剪切
            flipud=0.5,         # 上下翻轉
            fliplr=0.5,         # 左右翻轉
            mosaic=1.0,          # 馬賽克增強
            mixup=0.15,          # 混合增強
            copy_paste=0.1,      # 複製粘貼增強

            # 損失函數權重
            box=7.5,             # 邊界框損失權重
            cls=0.5,             # 類別損失權重
            dfl=1.5,             # 分佈焦點損失權重

            save=True,
            save_period=10,
            plots=True,
        )

        # 驗證
        print("\n驗證模型...")
        val_results = model.val(data=yaml_path, imgsz=IMG_SIZE)

        map50 = val_results.results_dict.get('metrics/mAP50(B)', 0.0)
        map50_95 = val_results.results_dict.get('metrics/mAP50-95(B)', 0.0)

        print(f"\nmAP@0.5: {map50:.4f}")
        print(f"mAP@0.5:0.95: {map50_95:.4f}")

        # 複製最佳模型
        best_pt = PROJECT_DIR / "runs" / "monster_optimized" / "weights" / "best.pt"
        final_onnx = PROJECT_DIR / "models" / "best.onnx"
        final_onnx.parent.mkdir(exist_ok=True)

        if best_pt.exists():
            print(f"\n最佳模型: {best_pt}")

            print("匯出 ONNX...")
            try:
                export_model = YOLO(str(best_pt))
                export_model.export(format='onnx', imgsz=IMG_SIZE)

                src_onnx = best_pt.parent / "best.onnx"
                if src_onnx.exists():
                    shutil.copy(src_onnx, final_onnx)
                    print(f"已儲存: {final_onnx}")
            except Exception as e:
                print(f"匯出失敗: {e}")

        print("\n" + "=" * 60)
        print("訓練完成！")
        print("=" * 60)

    except ImportError:
        print("\n[錯誤] 請先安裝: pip install ultralytics")
    except Exception as e:
        print(f"\n[錯誤] {e}")
        import traceback
        traceback.print_exc()

if __name__ == "__main__":
    try:
        import ultralytics
        print(f"[OK] ultralytics {ultralytics.__version__}")
    except ImportError:
        print("[安裝] pip install ultralytics...")
        os.system("pip install ultralytics")

    train()
