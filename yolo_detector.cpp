// ============================================================
// YOLO 物件偵測器實現 (Win7~Win11 通用)
// ONNX Runtime v1.12.1 - 動態載入
// ============================================================
#include "yolo_detector.h"
#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <cmath>
#include <malloc.h>
#undef min  // Avoid conflict with OrtRun member variable
#undef max  // Avoid conflict with OrtRun member variable

// Helper macros to avoid yolo_min/yolo_max conflicts with OrtRun members
// Use inline functions for type safety
template<typename T>
inline T yolo_min(T a, T b) { return a < b ? a : b; }
template<typename T>
inline T yolo_max(T a, T b) { return a > b ? a : b; }

// g_onnxLoader 定義於 onnx_loader.cpp

// 預設閾值
const float YoloDetector::DEFAULT_CONF_THRESH = 0.5f;
const float YoloDetector::DEFAULT_NMS_THRESH = 0.45f;

// ============================================================
// 建構/解構
// ============================================================
YoloDetector::YoloDetector()
    : m_env(NULL)
    , m_session(NULL)
    , m_allocator(NULL)
    , m_inputNames(NULL)
    , m_outputNames(NULL)
    , m_numOutputNames(0)
    , m_inputW(DEFAULT_INPUT_W)
    , m_inputH(DEFAULT_INPUT_H)
    , m_numProposals(8400)    // YOLOv8n: 80*80 + 40*40 + 20*20 = 8400
    , m_numClasses(80)         // COCO: 80 classes
    , m_confThresh(DEFAULT_CONF_THRESH)
    , m_nmsThresh(DEFAULT_NMS_THRESH)
    , m_lastInferenceMs(0.0f)
    , m_runOptions(NULL)
    , m_initialized(false)
{
}

YoloDetector::~YoloDetector() {
    Destroy();
}

// ============================================================
// 初始化 (ANSI 版本)
// ============================================================
bool YoloDetector::Init(const char* modelPath) {
    return InitInternal(modelPath);
}

// ============================================================
// 初始化 (Unicode 版本)
// ============================================================
bool YoloDetector::Init(const wchar_t* modelPath) {
    // 轉換為 ANSI
    char ansiPath[MAX_PATH];
    WideCharToMultiByte(CP_ACP, 0, modelPath, -1, ansiPath, MAX_PATH, NULL, NULL);
    return InitInternal(ansiPath);
}

// ============================================================
// 內部初始化 (C API) - 使用動態載入的 ONNX Runtime
// ============================================================
bool YoloDetector::InitInternal(const char* modelPath) {
    if (m_initialized) {
        Destroy();
    }

    // 先確保 ONNX Runtime 已載入
    if (!g_onnxLoader.IsLoaded()) {
        if (!g_onnxLoader.Load(NULL)) {
            printf("[YoloDetector] Failed to load ONNX Runtime\n");
            return false;
        }
    }

    // 1. 建立 ONNX Runtime 環境
    OrtEnv* env = NULL;
    OrtStatus* status = g_onnxLoader.OrtCreateEnv(
        (OrtLoggingLevel)ORT_LOGGING_LEVEL_WARNING, "YoloDetector", &env);
    if (status != NULL) {
        printf("[YoloDetector] OrtCreateEnv failed: %s\n", g_onnxLoader.OrtGetErrorMessage(status));
        g_onnxLoader.OrtReleaseStatus(status);
        return false;
    }
    m_env = env;

    // 2. 建立 Session 選項
    OrtSessionOptions* session_options = g_onnxLoader.OrtCreateSessionOptions();
    if (!session_options) {
        printf("[YoloDetector] OrtCreateSessionOptions failed\n");
        return false;
    }

    // 效能優化設定
    g_onnxLoader.OrtSetSessionGraphOptimizationLevel(session_options, ORT_ENABLE_ALL);

    // 執行緒數 (根據 CPU 核心數)
    int numThreads = 2;
    g_onnxLoader.OrtSetIntraOpNumThreads(session_options, numThreads);
    g_onnxLoader.OrtSetInterOpNumThreads(session_options, numThreads);

    // 3. 建立 Session
    OrtSession* session = NULL;
    status = g_onnxLoader.OrtCreateSession(env, modelPath, session_options, &session);
    if (status != NULL) {
        printf("[YoloDetector] OrtCreateSession failed: %s\n", g_onnxLoader.OrtGetErrorMessage(status));
        printf("[YoloDetector] Model path: %s\n", modelPath);
        g_onnxLoader.OrtReleaseStatus(status);
        g_onnxLoader.OrtReleaseSessionOptions(session_options);
        return false;
    }
    m_session = session;
    g_onnxLoader.OrtReleaseSessionOptions(session_options);

    // 4. 建立 Allocator
    OrtAllocator* allocator = NULL;
    OrtStatus* allocStatus = g_onnxLoader.OrtGetAllocator(NULL, &allocator);
    if (allocStatus != NULL) {
        printf("[YoloDetector] OrtGetAllocator failed\n");
        return false;
    }
    m_allocator = allocator;

    // 5. 獲取輸入名稱
    size_t num_input_nodes = 0;
    if (!g_onnxLoader.OrtSessionGetInputCount ||
        g_onnxLoader.OrtSessionGetInputCount(session, &num_input_nodes) != NULL ||
        num_input_nodes == 0) {
        printf("[YoloDetector] OrtSessionGetInputCount failed\n");
        return false;
    }

    // 分配輸入名稱陣列
    char** input_names = (char**)malloc(sizeof(char*) * num_input_nodes);
    if (!input_names) {
        printf("[YoloDetector] Alloc input_names failed\n");
        return false;
    }

    for (size_t i = 0; i < num_input_nodes; i++) {
        char* input_name = NULL;
        if (!g_onnxLoader.OrtSessionGetInputName ||
            g_onnxLoader.OrtSessionGetInputName(session, i, allocator, &input_name) != NULL) {
            printf("[YoloDetector] OrtSessionGetInputName[%zu] failed\n", i);
            free(input_names);
            return false;
        }
        input_names[i] = input_name;
    }
    m_inputNames = input_names;

    // 6. 獲取輸出名稱
    size_t num_output_nodes = 0;
    if (!g_onnxLoader.OrtSessionGetOutputCount ||
        g_onnxLoader.OrtSessionGetOutputCount(session, &num_output_nodes) != NULL ||
        num_output_nodes == 0) {
        printf("[YoloDetector] OrtSessionGetOutputCount failed\n");
        return false;
    }

    char** output_names = (char**)malloc(sizeof(char*) * num_output_nodes);
    if (!output_names) {
        printf("[YoloDetector] Alloc output_names failed\n");
        return false;
    }

    for (size_t i = 0; i < num_output_nodes; i++) {
        char* output_name = NULL;
        if (!g_onnxLoader.OrtSessionGetOutputName ||
            g_onnxLoader.OrtSessionGetOutputName(session, i, allocator, &output_name) != NULL) {
            printf("[YoloDetector] OrtSessionGetOutputName[%zu] failed\n", i);
            free(output_names);
            return false;
        }
        output_names[i] = output_name;
    }
    m_outputNames = output_names;
    m_numOutputNames = (int)num_output_nodes;

    // 7. 設定模型參數
    m_inputW = DEFAULT_INPUT_W;
    m_inputH = DEFAULT_INPUT_H;
    m_numProposals = 8400;
    m_numClasses = 80;

    // 預分配內部緩衝區
    m_inputTensor.resize(3 * m_inputW * m_inputH);
    m_outputTensor.resize(84 * m_numProposals);

    // 8. 建立 RunOptions
    if (g_onnxLoader.OrtCreateRunOptions) {
        g_onnxLoader.OrtCreateRunOptions(&m_runOptions);
        if (m_runOptions && g_onnxLoader.OrtRunOptionsSetRunLogSeverity) {
            g_onnxLoader.OrtRunOptionsSetRunLogSeverity(m_runOptions, (OrtLoggingLevel)ORT_LOGGING_LEVEL_ERROR);
        }
    }

    m_initialized = true;
    printf("[YoloDetector] Initialized successfully\n");
    printf("[YoloDetector] Input size: %dx%d, Proposals: %d, Classes: %d\n",
           m_inputW, m_inputH, m_numProposals, m_numClasses);
    return true;
}

// ============================================================
// 釋放資源
// ============================================================
void YoloDetector::Destroy() {
    if (m_runOptions) {
        if (g_onnxLoader.OrtReleaseRunOptions) {
            g_onnxLoader.OrtReleaseRunOptions(m_runOptions);
        }
        m_runOptions = NULL;
    }

    if (m_allocator) {
        if (g_onnxLoader.OrtReleaseAllocator) {
            g_onnxLoader.OrtReleaseAllocator(m_allocator);
        }
        m_allocator = NULL;
    }

    if (m_inputNames && m_allocator) {
        for (int i = 0; i < (int)m_numOutputNames; i++) {
            if (((char**)m_inputNames)[i] && g_onnxLoader.OrtAllocatorFree) {
                g_onnxLoader.OrtAllocatorFree(m_allocator, ((char**)m_inputNames)[i]);
            }
        }
        free(m_inputNames);
        m_inputNames = NULL;
    }

    if (m_outputNames && m_allocator) {
        for (int i = 0; i < m_numOutputNames; i++) {
            if (((char**)m_outputNames)[i] && g_onnxLoader.OrtAllocatorFree) {
                g_onnxLoader.OrtAllocatorFree(m_allocator, ((char**)m_outputNames)[i]);
            }
        }
        free(m_outputNames);
        m_outputNames = NULL;
    }

    if (m_session) {
        if (g_onnxLoader.OrtReleaseSession) {
            g_onnxLoader.OrtReleaseSession(m_session);
        }
        m_session = NULL;
    }

    if (m_env) {
        if (g_onnxLoader.OrtReleaseEnv) {
            g_onnxLoader.OrtReleaseEnv((OrtEnv*)m_env);
        }
        m_env = NULL;
    }

    m_initialized = false;
    printf("[YoloDetector] Destroyed\n");
}

// ============================================================
// 執行推論
// ============================================================
int YoloDetector::Detect(const uint8_t* pixels, int imgW, int imgH,
                         YoloBox* results, int maxResults) {
    if (!m_initialized || !m_session) {
        return 0;
    }

    LARGE_INTEGER freq, start, end;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&start);

    // 預處理影像
    PreprocessInPlace(pixels, imgW, imgH);

    // 建立輸入 Tensor
    OrtMemoryInfo* memory_info = NULL;
    g_onnxLoader.OrtCreateMemoryInfo("Cpu", OrtArenaAllocator, 0, OrtMemTypeDefault, &memory_info);

    size_t input_dims[] = {1, 3, (size_t)m_inputH, (size_t)m_inputW};
    OrtValue* input_tensor = NULL;

    OrtStatus* status = g_onnxLoader.OrtCreateTensorWithDataAsOrtValue(
        m_allocator,
        m_inputTensor.data(),
        m_inputTensor.size(),
        input_dims,
        4,
        ONNX_TENSOR_ELEMENT_DATA_TYPE_UNDEFINED,
        &input_tensor);

    if (status != NULL) {
        printf("[YoloDetector] OrtCreateTensorWithDataAsOrtValue failed: %s\n",
               g_onnxLoader.OrtGetErrorMessage(status));
        g_onnxLoader.OrtReleaseStatus(status);
        if (memory_info && g_onnxLoader.OrtReleaseMemoryInfo) {
            g_onnxLoader.OrtReleaseMemoryInfo(memory_info);
        }
        return 0;
    }

    // 執行推論
    OrtValue* output_tensor = NULL;
    const char* input_names[] = {((char**)m_inputNames)[0]};
    const char* output_names[] = {((char**)m_outputNames)[0]};

    status = g_onnxLoader.OrtRun(
        m_session,
        m_runOptions,
        input_names,
        (OrtValue**)&input_tensor,
        1,
        output_names,
        &output_tensor,
        NULL);

    if (status != NULL) {
        printf("[YoloDetector] OrtRun failed: %s\n", g_onnxLoader.OrtGetErrorMessage(status));
        g_onnxLoader.OrtReleaseStatus(status);
        g_onnxLoader.OrtReleaseValue(input_tensor);
        if (memory_info && g_onnxLoader.OrtReleaseMemoryInfo) {
            g_onnxLoader.OrtReleaseMemoryInfo(memory_info);
        }
        return 0;
    }

    // 獲取輸出維度
    size_t output_dims_num = 0;
    if (g_onnxLoader.OrtValueGetCount) {
        g_onnxLoader.OrtValueGetCount(output_tensor, &output_dims_num);
    }

    // 解析輸出
    float* output_data = NULL;
    size_t* output_dims = NULL;
    int output_type = 0;

    if (g_onnxLoader.OrtGetTensorData) {
        g_onnxLoader.OrtGetTensorData(output_tensor, &output_data, &output_dims, &output_type);
    }

    int detected = 0;
    if (output_data) {
        int output_size = 84 * m_numProposals;
        detected = ParseYolov8Output(output_data, output_size, m_numProposals,
                                     results, maxResults);
    }

    // 清理
    if (input_tensor && g_onnxLoader.OrtReleaseValue) {
        g_onnxLoader.OrtReleaseValue(input_tensor);
    }
    if (output_tensor && g_onnxLoader.OrtReleaseValue) {
        g_onnxLoader.OrtReleaseValue(output_tensor);
    }
    if (memory_info && g_onnxLoader.OrtReleaseMemoryInfo) {
        g_onnxLoader.OrtReleaseMemoryInfo(memory_info);
    }

    QueryPerformanceCounter(&end);
    m_lastInferenceMs = (float)(1000.0 * (end.QuadPart - start.QuadPart) / freq.QuadPart);

    return detected;
}

int YoloDetector::Detect(const std::vector<uint8_t>& pixels, int imgW, int imgH,
                         std::vector<YoloBox>& results) {
    results.clear();
    YoloBox temp[256];
    int count = Detect(pixels.data(), imgW, imgH, temp, 256);
    for (int i = 0; i < count; i++) {
        results.push_back(temp[i]);
    }
    return count;
}

// ============================================================
// 影像前處理 (in-place)
// ============================================================
void YoloDetector::PreprocessInPlace(const uint8_t* src, int srcW, int srcH) {
    // 計算縮放比例
    float scale = yolo_min((float)m_inputW / srcW, (float)m_inputH / srcH);
    int newW = (int)(srcW * scale);
    int newH = (int)(srcH * scale);
    int padX = (m_inputW - newW) / 2;
    int padY = (m_inputH - newH) / 2;

    // 填充黑色
    std::fill(m_inputTensor.begin(), m_inputTensor.end(), 0.0f);

    // 雙線性插值縮放 + BGR->RGB + 歸一化
    float* dstPtr = m_inputTensor.data();

    for (int y = 0; y < newH; y++) {
        float srcY = (y + 0.5f) / scale - 0.5f;
        int y0 = (int)std::floor(srcY);
        int y1 = yolo_min(y0 + 1, srcH - 1);
        float yFrac = srcY - y0;

        for (int x = 0; x < newW; x++) {
            float srcX = (x + 0.5f) / scale - 0.5f;
            int x0 = (int)std::floor(srcX);
            int x1 = yolo_min(x0 + 1, srcW - 1);
            float xFrac = srcX - x0;

            // 雙線性插值
            for (int c = 0; c < 3; c++) {
                float v00 = src[(y0 * srcW + x0) * 4 + c];     // BGRA -> B, G, R
                float v01 = src[(y0 * srcW + x1) * 4 + c];
                float v10 = src[(y1 * srcW + x0) * 4 + c];
                float v11 = src[(y1 * srcW + x1) * 4 + c];

                float value = v00 * (1 - xFrac) * (1 - yFrac) +
                              v01 * xFrac * (1 - yFrac) +
                              v10 * (1 - xFrac) * yFrac +
                              v11 * xFrac * yFrac;

                // BGR -> RGB, 並存入 NCHW
                int dstC = (c == 0) ? 2 : (c == 2 ? 0 : 1);  // BGR -> RGB
                dstPtr[dstC * m_inputH * m_inputW + (padY + y) * m_inputW + (padX + x)] =
                    value / 255.0f;
            }
        }
    }
}

// ============================================================
// 預處理 (外部緩衝區)
// ============================================================
void YoloDetector::Preprocess(const uint8_t* src, int srcW, int srcH,
                               float* dst, int dstW, int dstH) {
    // 計算縮放比例
    float scale = yolo_min((float)dstW / srcW, (float)dstH / srcH);
    int newW = (int)(srcW * scale);
    int newH = (int)(srcH * scale);
    int padX = (dstW - newW) / 2;
    int padY = (dstH - newH) / 2;

    // 填充黑色
    std::fill(dst, dst + 3 * dstH * dstW, 0.0f);

    // 雙線性插值
    for (int y = 0; y < newH; y++) {
        float srcY = (y + 0.5f) / scale - 0.5f;
        int y0 = (int)std::floor(srcY);
        int y1 = yolo_min(y0 + 1, srcH - 1);
        float yFrac = srcY - y0;

        for (int x = 0; x < newW; x++) {
            float srcX = (x + 0.5f) / scale - 0.5f;
            int x0 = (int)std::floor(srcX);
            int x1 = yolo_min(x0 + 1, srcW - 1);
            float xFrac = srcX - x0;

            for (int c = 0; c < 3; c++) {
                float v00 = src[(y0 * srcW + x0) * 4 + c];
                float v01 = src[(y0 * srcW + x1) * 4 + c];
                float v10 = src[(y1 * srcW + x0) * 4 + c];
                float v11 = src[(y1 * srcW + x1) * 4 + c];

                float value = v00 * (1 - xFrac) * (1 - yFrac) +
                              v01 * xFrac * (1 - yFrac) +
                              v10 * (1 - xFrac) * yFrac +
                              v11 * xFrac * yFrac;

                int dstC = (c == 0) ? 2 : (c == 2 ? 0 : 1);
                dst[dstC * dstH * dstW + (padY + y) * dstW + (padX + x)] =
                    value / 255.0f;
            }
        }
    }
}

// ============================================================
// 解析 YOLOv8 輸出
// ============================================================
int YoloDetector::ParseYolov8Output(float* output, int outSize, int numProposals,
                                    YoloBox* results, int maxResults) {
    std::vector<YoloBox> boxes;

    // YOLOv8 輸出格式: [1, 84, 8400]
    // 每個 proposal: 4 bbox + 80 class scores
    const int numClasses = 80;
    const int bboxSize = 4;

    for (int i = 0; i < numProposals && i * (bboxSize + numClasses) + numClasses < outSize; i++) {
        float* ptr = output + i * (bboxSize + numClasses);

        // 讀取邊界框 (cx, cy, w, h)
        float cx = ptr[0];
        float cy = ptr[1];
        float w = ptr[2];
        float h = ptr[3];

        // 找最大類別分數
        int classId = 0;
        float maxScore = ptr[bboxSize];
        for (int c = 1; c < numClasses; c++) {
            if (ptr[bboxSize + c] > maxScore) {
                maxScore = ptr[bboxSize + c];
                classId = c;
            }
        }

        // Sigmoid 激活 + 信心度閾值
        float confidence = 1.0f / (1.0f + exp(-maxScore));
        if (confidence < m_confThresh) continue;

        // 轉換為 (x1, y1, x2, y2)
        YoloBox box;
        box.x1 = cx - w * 0.5f;
        box.y1 = cy - h * 0.5f;
        box.x2 = cx + w * 0.5f;
        box.y2 = cy + h * 0.5f;
        box.confidence = confidence;
        box.classId = classId;
        box.classScore = maxScore;

        boxes.push_back(box);
    }

    // NMS
    int selected = Postprocess(output, outSize, (int)boxes.size(), results, maxResults);

    // 轉換座標到原始圖片
    for (int i = 0; i < selected; i++) {
        // 這裡需要原始尺寸，暫時保留模型座標
    }

    return selected;
}

// ============================================================
// NMS 後處理
// ============================================================
int YoloDetector::Postprocess(float* output, int outputSize, int numBoxes,
                              YoloBox* results, int maxResults) {
    if (numBoxes == 0) return 0;

    // 計算 IoU
    auto computeIou = [this](const YoloBox& a, const YoloBox& b) -> float {
        float interX1 = yolo_max(a.x1, b.x1);
        float interY1 = yolo_max(a.y1, b.y1);
        float interX2 = yolo_min(a.x2, b.x2);
        float interY2 = yolo_min(a.y2, b.y2);

        float interW = yolo_max(0.0f, interX2 - interX1);
        float interH = yolo_max(0.0f, interY2 - interY1);
        float interArea = interW * interH;

        float areaA = (a.x2 - a.x1) * (a.y2 - a.y1);
        float areaB = (b.x2 - b.x1) * (b.y2 - b.y1);
        float unionArea = areaA + areaB - interArea;

        return unionArea > 0 ? interArea / unionArea : 0;
    };

    // 依信心度排序
    std::sort(results, results + numBoxes,
              [](const YoloBox& a, const YoloBox& b) {
                  return a.confidence > b.confidence;
              });

    std::vector<bool> suppressed(numBoxes, false);
    int count = 0;

    for (int i = 0; i < numBoxes && count < maxResults; i++) {
        if (suppressed[i]) continue;

        results[count++] = results[i];

        for (int j = i + 1; j < numBoxes; j++) {
            if (!suppressed[j] && computeIou(results[i], results[j]) > m_nmsThresh) {
                suppressed[j] = true;
            }
        }
    }

    return count;
}

// ============================================================
// 計算 IoU
// ============================================================
float YoloDetector::ComputeIoU(const YoloBox& a, const YoloBox& b) {
    float interX1 = yolo_max(a.x1, b.x1);
    float interY1 = yolo_max(a.y1, b.y1);
    float interX2 = yolo_min(a.x2, b.x2);
    float interY2 = yolo_min(a.y2, b.y2);

    float interW = yolo_max(0.0f, interX2 - interX1);
    float interH = yolo_max(0.0f, interY2 - interY1);
    float interArea = interW * interH;

    float areaA = (a.x2 - a.x1) * (a.y2 - a.y1);
    float areaB = (b.x2 - b.x1) * (b.y2 - b.y1);
    float unionArea = areaA + areaB - interArea;

    return unionArea > 0 ? interArea / unionArea : 0;
}

// ============================================================
// 座標縮放
// ============================================================
void YoloDetector::ScaleCoords(float& x1, float& y1, float& x2, float& y2,
                                int padX, int padY, float scale, int origW, int origH) {
    x1 = (x1 - padX) / scale;
    y1 = (y1 - padY) / scale;
    x2 = (x2 - padX) / scale;
    y2 = (y2 - padY) / scale;

    x1 = yolo_max(0.0f, yolo_min((float)origW, x1));
    y1 = yolo_max(0.0f, yolo_min((float)origH, y1));
    x2 = yolo_max(0.0f, yolo_min((float)origW, x2));
    y2 = yolo_max(0.0f, yolo_min((float)origH, y2));
}

// ============================================================
// 轉換 YoloBox 到 VisualMonster
// ============================================================
void YoloDetector::ConvertToVisualMonster(const YoloBox& box, int imgW, int imgH,
                                          int gameW, int gameH, VisualMonster& out) {
    // 計算中心點
    float centerX = (box.x1 + box.x2) * 0.5f;
    float centerY = (box.y1 + box.y2) * 0.5f;

    // 轉換到遊戲座標
    out.screenX = (int)(centerX * gameW / imgW);
    out.screenY = (int)(centerY * gameH / imgH);
    out.width = (int)((box.x2 - box.x1) * gameW / imgW);
    out.priority = (int)(box.confidence * 100);  // Use priority for confidence

    // 估算優先級（基於大小，大的可能距離更近）
    int avgSize = (out.width + out.width) / 2;  // height not in struct, use width twice
    if (avgSize > 100) {
        out.priority = 100;  // 很近
    } else if (avgSize > 50) {
        out.priority = 80;  // 中等
    } else if (avgSize > 20) {
        out.priority = 50;  // 較遠
    } else {
        out.priority = 20;  // 很遠
    }
}

// ============================================================
// 便利函式
// ============================================================
int ConvertYoloToVisualMonsters(YoloDetector* detector,
                                 const std::vector<YoloBox>& boxes,
                                 int imgW, int imgH,
                                 int gameW, int gameH,
                                 VisualMonster* outMonsters,
                                 int maxMonsters) {
    int count = 0;
    for (size_t i = 0; i < boxes.size() && count < maxMonsters; i++) {
        detector->ConvertToVisualMonster(boxes[i], imgW, imgH, gameW, gameH, outMonsters[count]);
        count++;
    }
    return count;
}