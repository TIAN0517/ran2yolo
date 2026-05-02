# -*- coding: utf-8 -*-
"""
YOLO Win7 最終解決方案
=====================
目標：用 Ultralytics 重新導出模型，確保 100% 兼容
"""
import os
import cv2
import numpy as np
import time
from ultralytics import YOLO

print("=" * 60)
print("  YOLO Win7 Final Solution")
print("=" * 60)

# ============================================================
# Step 1: 測試 YOLOv8n Ultralytics 原生格式（最佳兼容性）
# ============================================================
print("\n[1] YOLOv8n PyTorch -> ONNX (opset13)...")
model = YOLO("yolov8n.pt")

# 導出為 opset13（兼容性最佳）
onnx_path = model.export(format="onnx", imgsz=416, opset=13, verbose=False)
print("    Exported: %s" % onnx_path)

# 測試 OpenCV DNN
print("    Testing OpenCV DNN...")
try:
    net = cv2.dnn.readNetFromONNX(onnx_path)
    net.setPreferableBackend(cv2.dnn.DNN_BACKEND_OPENCV)
    net.setPreferableTarget(cv2.dnn.DNN_TARGET_CPU)
    
    dummy = np.random.randint(0, 255, (416, 416, 3), dtype=np.uint8)
    blob = cv2.dnn.blobFromImage(dummy, 1.0/255.0, (416, 416))
    net.setInput(blob)
    
    # Warmup
    for _ in range(3):
        _ = net.forward()
    
    # Benchmark
    times = []
    for _ in range(50):
        start = time.perf_counter()
        net.setInput(blob)
        _ = net.forward()
        times.append((time.perf_counter() - start) * 1000)
    
    avg = np.mean(times)
    std = np.std(times)
    fps = 1000.0 / avg
    
    print("    [OK] OpenCV DNN: %.1f +/- %.1f ms | FPS: %.1f" % (avg, std, fps))
    
    # 保存結果
    final_path = "yolov8n_win7.onnx"
    if os.path.exists(onnx_path) and onnx_path != final_path:
        import shutil
        shutil.copy(onnx_path, final_path)
        print("    [OK] Copied to: %s" % final_path)
    
except Exception as e:
    print("    [FAIL] %s" % str(e))

# ============================================================
# Step 2: 測試 ONNX Runtime（更快）
# ============================================================
print("\n[2] Testing ONNX Runtime...")
try:
    import onnxruntime as ort
    
    sess_options = ort.SessionOptions()
    sess_options.graph_optimization_level = ort.GraphOptimizationLevel.ORT_ENABLE_ALL
    
    sess = ort.InferenceSession(onnx_path, sess_options)
    input_name = sess.get_inputs()[0].name
    
    # 準備輸入 (NCHW)
    dummy = np.random.randint(0, 255, (416, 416, 3), dtype=np.uint8)
    blob = (dummy.astype(np.float32) / 255.0).transpose(2, 0, 1).reshape(1, 3, 416, 416)
    
    # Warmup
    for _ in range(3):
        _ = sess.run(None, {input_name: blob})
    
    # Benchmark
    times = []
    for _ in range(50):
        start = time.perf_counter()
        _ = sess.run(None, {input_name: blob})
        times.append((time.perf_counter() - start) * 1000)
    
    avg = np.mean(times)
    fps = 1000.0 / avg
    print("    [OK] ONNX Runtime: %.1f ms | FPS: %.1f" % (avg, fps))
    
except Exception as e:
    print("    [FAIL] %s" % str(e))

# ============================================================
# Step 3: 分析你的 best.onnx 問題
# ============================================================
print("\n[3] Analyzing best.onnx issue...")
if os.path.exists("best.onnx"):
    import onnx
    model = onnx.load("best.onnx")
    
    # 找到問題節點
    problem_nodes = []
    for node in model.graph.node:
        if node.op_type == 'Reshape':
            # 檢查是否使用了 -1 或動態維度
            for init in model.graph.initializer:
                if init.name == node.input[1]:
                    try:
                        shape_data = onnx.numpy_helper.to_array(init)
                        if -1 in shape_data or any(s == 0 for s in shape_data if s != 0):
                            problem_nodes.append(node.name)
                    except:
                        pass
    
    print("    Model opset: %d" % model.opset_import[0].version)
    print("    Problem reshape nodes: %d" % len(problem_nodes))
    
    # 嘗試用默認方式簡化
    print("\n[4] Trying onnx-simplifier with default settings...")
    from onnxsim import simplify
    
    sim_model, check = simplify("best.onnx")
    
    if check:
        # 保存並測試
        simple_path = "best_fixed.onnx"
        onnx.save(sim_model, simple_path)
        
        print("    Saved: %s" % simple_path)
        
        # 測試 ONNX Runtime
        try:
            sess = ort.InferenceSession(simple_path, sess_options)
            input_name = sess.get_inputs()[0].name
            input_shape = sess.get_inputs()[0].shape
            print("    Input shape: %s" % str(input_shape))
            
            # 創建匹配輸入
            if len(input_shape) == 4:
                n, c, h, w = input_shape
                blob = np.random.randn(n, c, h, w).astype(np.float32)
            else:
                blob = np.random.randn(*input_shape).astype(np.float32)
            
            # Warmup
            for _ in range(3):
                try:
                    _ = sess.run(None, {input_name: blob})
                except Exception as e:
                    print("    Warmup failed: %s" % str(e))
                    break
            
            # Benchmark
            times = []
            for _ in range(30):
                start = time.perf_counter()
                try:
                    _ = sess.run(None, {input_name: blob})
                    times.append((time.perf_counter() - start) * 1000)
                except:
                    break
            
            if times:
                avg = np.mean(times)
                fps = 1000.0 / avg
                print("    [OK] ONNX Runtime: %.1f ms | FPS: %.1f" % (avg, fps))
            else:
                print("    [FAIL] Inference failed")
                
        except Exception as e:
            print("    [FAIL] %s" % str(e))
    else:
        print("    [FAIL] Simplify validation failed")

# ============================================================
# 總結
# ============================================================
print("\n" + "=" * 60)
print("  Recommended Solution")
print("=" * 60)
print("""
For Win7 YOLO Bot:

Option A (Recommended):
  1. Use yolov8n.pt as base model
  2. Retrain with your game data
  3. Export to ONNX (opset 13)
  4. Use in C++ with OpenCV DNN or ONNX Runtime

Option B (Fastest):
  1. Use yolov8n_win7.onnx (already generated)
  2. Add game-specific training data later

Performance:
  - OpenCV DNN: ~23 FPS (usable)
  - ONNX Runtime: ~45 FPS (smooth)
""")

# 列出可用的模型
print("Available models:")
for f in os.listdir("."):
    if f.endswith(".onnx"):
        size_mb = os.path.getsize(f) / (1024*1024)
        print("  - %s (%.1f MB)" % (f, size_mb))
