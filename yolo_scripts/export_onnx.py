# ============================================================
# 导出 YOLO 模型为 ONNX 格式
# ============================================================
import os
from pathlib import Path

PROJECT_DIR = Path(__file__).parent
BEST_MODEL = PROJECT_DIR / "runs/detect/train/weights/best.pt"
OUTPUT_ONNX = PROJECT_DIR / "best.onnx"

def export_to_onnx():
    if not BEST_MODEL.exists():
        print(f"[错误] 找不到模型: {BEST_MODEL}")
        print("请先运行 train_yolo.py 完成训练")
        return

    print("========== 导出 ONNX ==========")
    print(f"输入: {BEST_MODEL}")
    print(f"输出: {OUTPUT_ONNX}")
    print("=" * 40)

    try:
        from ultralytics import YOLO

        model = YOLO(str(BEST_MODEL))
        model.export(format='onnx', imgsz=640)

        print("\n[完成] ONNX 模型已导出!")
        print(f"路径: {PROJECT_DIR / 'best.onnx'}")

        # 检查文件大小
        if OUTPUT_ONNX.exists():
            size_mb = OUTPUT_ONNX.stat().st_size / (1024 * 1024)
            print(f"大小: {size_mb:.2f} MB")

    except ImportError:
        print("\n[错误] 请先安装 ultralytics: pip install ultralytics")
    except Exception as e:
        print(f"\n[错误] {e}")

if __name__ == "__main__":
    export_to_onnx()
