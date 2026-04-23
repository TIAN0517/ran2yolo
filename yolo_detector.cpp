// ============================================================
// YOLO 物件偵測器實現 (Win7~Win11 通用)
// ONNX Runtime v1.12.1 - 動態載入
// ============================================================
#include "yolo_detector.h"
#include "visionentity.h"
#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <cmath>

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
    OrtStatus* status = g_onnxLoader.OrtCreateEnv(ORT_LOGGING_LEVEL_WARNING, "YoloDetector", &env);
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
    int numThreads = 2;  // 預設 2 執行緒，減少記憶體使用
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
    char** input_names = (char**)allocator->Alloc(allocator, sizeof(char*) * num_input_nodes);
    if (!input_names) {
        printf("[YoloDetector] Alloc input_names failed\n");
        return false;
    }

    for (size_t i = 0; i < num_input_nodes; i++) {
        OrtChar* input_name = NULL;
        if (!g_onnxLoader.OrtSessionGetInputName ||
            g_onnxLoader.OrtSessionGetInputName(session, i, allocator, &input_name) != NULL) {
            printf("[YoloDetector] OrtSessionGetInputName[%zu] failed\n", i);
            return false;
        }
        input_names[i] = input_name;
    }

    // 6. 獲取輸出名稱
    size_t num_output_nodes = 0;
    if (!g_onnxLoader.OrtSessionGetOutputCount ||
        g_onnxLoader.OrtSessionGetOutputCount(session, &num_output_nodes) != NULL ||
        num_output_nodes == 0) {
        printf("[YoloDetector] OrtSessionGetOutputCount failed\n");
        return false;
    }

    char** output_names = (char**)allocator->Alloc(allocator, sizeof(char*) * num_output_nodes);
    if (!output_names) {
        printf("[YoloDetector] Alloc output_names failed\n");
        return false;
    }

    for (size_t i = 0; i < num_output_nodes; i++) {
        OrtChar* output_name = NULL;
        if (!g_onnxLoader.OrtSessionGetOutputName ||
            g_onnxLoader.OrtSessionGetOutputName(session, i, allocator, &output_name) != NULL) {
            printf("[YoloDetector] OrtSessionGetOutputName[%zu] failed\n", i);
            return false;
        }
        output_names[i] = output_name;
    }

    m_outputNames = (void*)output_names;
    m_numOutputNames = (int)num_output_nodes;

    // 7. 獲取輸入類型
    OrtTypeInfo* type_info = NULL;
    if (!g_onnxLoader.OrtSessionGetInputTypeInfo ||
        g_onnxLoader.OrtSessionGetInputTypeInfo(session, 0, &type_info) != NULL) {
        printf("[YoloDetector] OrtSessionGetInputTypeInfo failed\n");
        return false;
    }

    const OrtTensorTypeAndShapeInfo* tensor_info = NULL;
    if (!g_onnxLoader.OrtCastTypeInfoToTensorInfo ||
        g_onnxLoader.OrtCastTypeInfoToTensorInfo(type_info, &tensor_info) != NULL) {
        printf("[YoloDetector] OrtCastTypeInfoToTensorInfo failed\n");
        if (type_info && g_onnxLoader.OrtReleaseTypeInfo) g_onnxLoader.OrtReleaseTypeInfo(type_info);
        return false;
    }

    // 獲取輸入維度
    size_t num_dims = 0;
    if (!g_onnxLoader.OrtGetDimensionsCount ||
        g_onnxLoader.OrtGetDimensionsCount(tensor_info, &num_dims) != NULL) {
        printf("[YoloDetector] OrtGetDimensionsCount failed\n");
        if (type_info && g_onnxLoader.OrtReleaseTypeInfo) g_onnxLoader.OrtReleaseTypeInfo(type_info);
        return false;
    }

    // 分配維度緩衝區
    int64_t* dims = (int64_t*)allocator->Alloc(allocator, sizeof(int64_t) * num_dims);
    if (!dims) {
        printf("[YoloDetector] Alloc dims failed\n");
        if (type_info && g_onnxLoader.OrtReleaseTypeInfo) g_onnxLoader.OrtReleaseTypeInfo(type_info);
        return false;
    }

    if (!g_onnxLoader.OrtGetDimensions ||
        g_onnxLoader.OrtGetDimensions(tensor_info, dims, num_dims) != NULL) {
        printf("[YoloDetector] OrtGetDimensions failed\n");
        allocator->Free(allocator, dims);
        if (type_info && g_onnxLoader.OrtReleaseTypeInfo) g_onnxLoader.OrtReleaseTypeInfo(type_info);
        return false;
    }

    // 解析維度 [1, 3, 640, 640]
    // 假設格式為 NCHW
    for (size_t d = 0; d < num_dims; d++) {
        if (d == 2) m_inputH = (int)dims[d];  // Height
        if (d == 3) m_inputW = (int)dims[d];  // Width
    }

    allocator->Free(allocator, dims);
    if (type_info && g_onnxLoader.OrtReleaseTypeInfo) g_onnxLoader.OrtReleaseTypeInfo(type_info);

    // 計算候選框數量
    // YOLOv8 輸出格式: [1, 84, 8400] (1x84x8400)
    // 84 = 4 (bbox) + 80 (classes)
    // 根據實際輸出大小推斷
    m_numClasses = 80;  // COCO default
    m_numProposals = m_inputW * m_inputH / 64;  // 預估

    // 8. 配置輸入緩衝區
    size_t input_tensor_size = 1LL * 3 * m_inputW * m_inputH;
    m_inputTensor.resize(input_tensor_size);

    // 9. 建立 Run 選項
    m_runOptions = g_onnxLoader.OrtCreateRunOptions ? g_onnxLoader.OrtCreateRunOptions() : NULL;
    if (m_runOptions) {
        // 不使用紀錄 (提高效能)
    }

    m_initialized = true;
    printf("[YoloDetector] Initialized: input=%dx%d, proposals=%d, classes=%d\n",
           m_inputW, m_inputH, m_numProposals, m_numClasses);
    return true;
}

// ============================================================
// 釋放資源
// ============================================================
void YoloDetector::Destroy() {
    if (m_runOptions) {
        if (g_onnxLoader.OrtReleaseRunOptions)
            g_onnxLoader.OrtReleaseRunOptions((OrtRunOptions*)m_runOptions);
        m_runOptions = NULL;
    }

    if (m_outputNames && m_allocator) {
        char** names = (char**)m_outputNames;
        for (int i = 0; i < m_numOutputNames; i++) {
            if (names[i]) {
                if (g_onnxLoader.OrtAllocatorFree)
                    g_onnxLoader.OrtAllocatorFree((OrtAllocator*)m_allocator, names[i]);
            }
        }
        if (g_onnxLoader.OrtAllocatorFree)
            g_onnxLoader.OrtAllocatorFree((OrtAllocator*)m_allocator, names);
        m_outputNames = NULL;
    }

    if (m_inputNames && m_allocator) {
        // 同上
    }

    if (m_allocator) {
        if (g_onnxLoader.OrtReleaseAllocator)
            g_onnxLoader.OrtReleaseAllocator((OrtAllocator*)m_allocator);
        m_allocator = NULL;
    }

    if (m_session) {
        if (g_onnxLoader.OrtReleaseSession)
            g_onnxLoader.OrtReleaseSession((OrtSession*)m_session);
        m_session = NULL;
    }

    if (m_env) {
        if (g_onnxLoader.OrtReleaseEnv)
            g_onnxLoader.OrtReleaseEnv((OrtEnv*)m_env);
        m_env = NULL;
    }

    m_inputTensor.clear();
    m_outputTensor.clear();
    m_initialized = false;
}

// ============================================================
// 影像前處理: BGRA -> NCHW float tensor
// ============================================================
void YoloDetector::Preprocess(const uint8_t* src, int srcW, int srcH,
                              float* dst, int dstW, int dstH) {
    // 計算縮放比例 (letterbox 方式保持比例)
    float scale = (float)dstW / (float)srcW;
    if (srcH * scale > dstH) {
        scale = (float)dstH / (float)srcH;
    }

    int newW = (int)(srcW * scale);
    int newH = (int)(srcH * scale);
    int padX = (dstW - newW) / 2;
    int padY = (dstH - newH) / 2;

    // 計算每個輸出像素對應的輸入像素位置
    float scaleInv = 1.0f / scale;

    // NCHW 格式: [C, H, W]
    for (int y = 0; y < dstH; y++) {
        for (int x = 0; x < dstW; x++) {
            // 計算對應的原始圖片座標
            int srcX = (int)((x - padX) * scaleInv);
            int srcY = (int)((y - padY) * scaleInv);

            // 超出範圍設為 0
            if (srcX < 0 || srcX >= srcW || srcY < 0 || srcY >= srcH) {
                dst[0 * dstH * dstW + y * dstW + x] = 0.0f;
                dst[1 * dstH * dstW + y * dstW + x] = 0.0f;
                dst[2 * dstH * dstW + y * dstW + x] = 0.0f;
            } else {
                // BGRA -> RGB (浮點化 + 正規化到 0-1)
                const uint8_t* pixel = src + (srcY * srcW + srcX) * 4;
                // 假設是 BGRA 格式
                dst[0 * dstH * dstW + y * dstW + x] = pixel[2] / 255.0f;  // R
                dst[1 * dstH * dstW + y * dstW + x] = pixel[1] / 255.0f;  // G
                dst[2 * dstH * dstW + y * dstW + x] = pixel[0] / 255.0f;  // B
            }
        }
    }
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

    // 1. 前處理
    Preprocess(pixels, imgW, imgH, m_inputTensor.data(), m_inputW, m_inputH);

    // 2. 準備輸入
    OrtSession* session = (OrtSession*)m_session;
    OrtAllocator* allocator = (OrtAllocator*)m_allocator;
    OrtMemoryInfo* memory_info = NULL;
    g_onnxLoader.OrtCreateMemoryInfo("Cpu", OrtArenaAllocator, 0, OrtMemTypeDefault, &memory_info);

    // 輸入維度 [1, 3, H, W]
    std::vector<int64_t> input_dims = {1, 3, m_inputH, m_inputW};

    // 建立輸入 tensor
    OrtValue* input_tensor = NULL;
    OrtStatus* status = g_onnxLoader.OrtCreateTensorWithDataAsOrtValue(
        allocator,
        m_inputTensor.data(),
        m_inputTensor.size() * sizeof(float),
        input_dims.data(),
        (size_t)input_dims.size(),
        ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,
        &input_tensor
    );

    if (status != NULL) {
        printf("[YoloDetector] OrtCreateTensorWithDataAsOrtValue failed: %s\n",
               g_onnxLoader.OrtGetErrorMessage ? g_onnxLoader.OrtGetErrorMessage(status) : "unknown");
        g_onnxLoader.OrtReleaseStatus(status);
        if (memory_info) g_onnxLoader.OrtReleaseMemoryInfo ? g_onnxLoader.OrtReleaseMemoryInfo(memory_info) : (void)0;
        return 0;
    }

    // 3. 執行推論
    OrtValue* output_tensor = NULL;
    const char* input_names_arr[] = {"images"};  // YOLOv8 預設輸入名稱
    const char* output_names_arr[] = {"output0"}; // YOLOv8 預設輸出名稱

    status = g_onnxLoader.OrtRun(
        (OrtSession*)m_session,
        NULL,  // run options
        input_names_arr,
        (const OrtValue* const*)&input_tensor,
        1,
        output_names_arr,
        1,
        &output_tensor
    );

    if (status != NULL) {
        printf("[YoloDetector] OrtRun failed: %s\n", g_onnxLoader.OrtGetErrorMessage ? g_onnxLoader.OrtGetErrorMessage(status) : "unknown");
        if (input_tensor) g_onnxLoader.OrtReleaseValue ? g_onnxLoader.OrtReleaseValue(input_tensor) : (void)0;
        if (memory_info) g_onnxLoader.OrtReleaseMemoryInfo ? g_onnxLoader.OrtReleaseMemoryInfo(memory_info) : (void)0;
        return 0;
    }

    // 4. 獲取輸出
    float* output_data = NULL;
    status = g_onnxLoader.OrtGetTensorData<float>(
        output_tensor,
        &output_data,
        NULL,  // 忽略 shape
        NULL   // 忽略 num_dims
    );

    if (status != NULL) {
        printf("[YoloDetector] OrtGetTensorData failed: %s\n", g_onnxLoader.OrtGetErrorMessage ? g_onnxLoader.OrtGetErrorMessage(status) : "unknown");
        g_onnxLoader.OrtReleaseStatus(status);
        if (output_tensor) g_onnxLoader.OrtReleaseValue ? g_onnxLoader.OrtReleaseValue(output_tensor) : (void)0;
        if (input_tensor) g_onnxLoader.OrtReleaseValue ? g_onnxLoader.OrtReleaseValue(input_tensor) : (void)0;
        if (memory_info) g_onnxLoader.OrtReleaseMemoryInfo ? g_onnxLoader.OrtReleaseMemoryInfo(memory_info) : (void)0;
        return 0;
    }

    // 5. 獲取輸出維度
    size_t output_dims_num = 0;
    g_onnxLoader.OrtValueGetCount ? g_onnxLoader.OrtValueGetCount(output_tensor, &output_dims_num) : (void)0;

    // 6. 後處理
    int num_boxes = 0;
    if (output_dims_num >= 3) {
        num_boxes = ParseYolov8Output(output_data, (int)m_outputTensor.size(),
                                       m_numProposals, results, maxResults);
    }

    // 清理
    if (g_onnxLoader.OrtReleaseValue) {
        if (input_tensor) g_onnxLoader.OrtReleaseValue(input_tensor);
        if (output_tensor) g_onnxLoader.OrtReleaseValue(output_tensor);
    }
    if (memory_info && g_onnxLoader.OrtReleaseMemoryInfo)
        g_onnxLoader.OrtReleaseMemoryInfo(memory_info);

    QueryPerformanceCounter(&end);
    m_lastInferenceMs = (float)(end.QuadPart - start.QuadPart) * 1000.0f / (float)freq.QuadPart;

    return num_boxes;
}

// ============================================================
// 執行推論 (使用 vector)
// ============================================================
int YoloDetector::Detect(const std::vector<uint8_t>& pixels, int imgW, int imgH,
                         std::vector<YoloBox>& results) {
    results.clear();
    YoloBox boxes[256];
    int count = Detect(pixels.data(), imgW, imgH, boxes, 256);
    for (int i = 0; i < count; i++) {
        results.push_back(boxes[i]);
    }
    return count;
}

// ============================================================
// 解析 YOLOv8 輸出
// YOLOv8 輸出格式: [1, 84, 8400] 或 [1, 4+numClasses, numProposals]
// ============================================================
int YoloDetector::ParseYolov8Output(float* output, int outSize, int numProposals,
                                     YoloBox* results, int maxResults) {
    // YOLOv8 輸出格式: [batch, 4+numClasses, numProposals]
    // 4 = x, y, w, h (中心座標 + 寬高)
    int numChannels = 4 + m_numClasses;  // 通常是 84

    // 找出所有超過信心度閾值的候選框
    int count = 0;
    for (int p = 0; p < numProposals && count < maxResults; p++) {
        // 指向這個候選框的資料開頭
        float* ptr = output + p * numChannels;

        // 找最大類別分數
        float maxScore = 0.0f;
        int maxClassId = 0;
        for (int c = 4; c < numChannels; c++) {
            float score = ptr[c];
            if (score > maxScore) {
                maxScore = score;
                maxClassId = c - 4;
            }
        }

        // 只考慮怪物類別 (根據你的模型調整)
        // 假設 class 0 = monster
        if (maxScore >= m_confThresh) {
            // 解析邊界框
            float cx = ptr[0];  // 中心 X
            float cy = ptr[1];  // 中心 Y
            float w = ptr[2];   // 寬度
            float h = ptr[3];   // 高度

            // 轉換為左上右下座標
            results[count].x1 = cx - w * 0.5f;
            results[count].y1 = cy - h * 0.5f;
            results[count].x2 = cx + w * 0.5f;
            results[count].y2 = cy + h * 0.5f;
            results[count].confidence = maxScore;
            results[count].classId = maxClassId;
            results[count].classScore = maxScore;

            count++;
        }
    }

    // NMS
    return Postprocess(output, outSize, count, results, maxResults);
}

// ============================================================
// NMS 後處理
// ============================================================
int YoloDetector::Postprocess(float* output, int outputSize, int numBoxes,
                              YoloBox* results, int maxResults) {
    if (numBoxes == 0) return 0;

    // 按信心度排序
    for (int i = 0; i < numBoxes - 1; i++) {
        for (int j = i + 1; j < numBoxes; j++) {
            if (results[j].confidence > results[i].confidence) {
                YoloBox tmp = results[i];
                results[i] = results[j];
                results[j] = tmp;
            }
        }
    }

    // NMS
    bool* suppressed = (bool*)malloc(sizeof(bool) * numBoxes);
    if (!suppressed) return numBoxes;
    memset(suppressed, 0, sizeof(bool) * numBoxes);

    int count = 0;
    for (int i = 0; i < numBoxes; i++) {
        if (suppressed[i]) continue;

        // 保留這個框
        results[count++] = results[i];

        // 抑制重疊的框
        for (int j = i + 1; j < numBoxes; j++) {
            if (suppressed[j]) continue;
            if (results[j].classId != results[i].classId) continue;

            float iou = ComputeIoU(results[i], results[j]);
            if (iou > m_nmsThresh) {
                suppressed[j] = true;
            }
        }
    }

    free(suppressed);
    return count;
}

// ============================================================
// 計算 IoU
// ============================================================
float YoloDetector::ComputeIoU(const YoloBox& a, const YoloBox& b) {
    float x1 = std::max(a.x1, b.x1);
    float y1 = std::max(a.y1, b.y1);
    float x2 = std::min(a.x2, b.x2);
    float y2 = std::min(a.y2, b.y2);

    float interW = x2 - x1;
    float interH = y2 - y1;

    if (interW <= 0 || interH <= 0) return 0.0f;

    float interArea = interW * interH;
    float areaA = (a.x2 - a.x1) * (a.y2 - a.y1);
    float areaB = (b.x2 - b.x1) * (b.y2 - b.y1);
    float unionArea = areaA + areaB - interArea;

    if (unionArea <= 0) return 0.0f;
    return interArea / unionArea;
}

// ============================================================
// 座標縮放
// ============================================================
void YoloDetector::ScaleCoords(float& x1, float& y1, float& x2, float& y2,
                               int padX, int padY, float scale, int origW, int origH) {
    // 從模型座標轉換回原始圖片座標
    x1 = (x1 - padX) / scale;
    y1 = (y1 - padY) / scale;
    x2 = (x2 - padX) / scale;
    y2 = (y2 - padY) / scale;

    // Clamp to image bounds
    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 > origW) x2 = (float)origW;
    if (y2 > origH) y2 = (float)origH;
}

// ============================================================
// 轉換為 VisualMonster
// ============================================================
void YoloDetector::ConvertToVisualMonster(const YoloBox& box, int imgW, int imgH,
                                          int gameW, int gameH, VisualMonster& out) {
    // 計算 letterbox padding
    float scale = (float)m_inputW / (float)imgW;
    if (imgH * scale > m_inputH) {
        scale = (float)m_inputH / (float)imgH;
    }

    int newW = (int)(imgW * scale);
    int newH = (int)(imgH * scale);
    int padX = (m_inputW - newW) / 2;
    int padY = (m_inputH - newH) / 2;

    // 座標轉換 (從模型座標到原始圖片座標)
    float x1 = box.x1;
    float y1 = box.y1;
    float x2 = box.x2;
    float y2 = box.y2;
    ScaleCoords(x1, y1, x2, y2, padX, padY, scale, imgW, imgH);

    // 計算中心點
    float centerX = (x1 + x2) * 0.5f;
    float centerY = (y1 + y2) * 0.5f;

    // 轉換為遊戲相對座標 (0-1023, 0-767)
    out.relX = (int)(centerX * gameW / imgW);
    out.relY = (int)(centerY * gameH / imgH);

    // 螢幕座標 (這裡假設 imgW/imgH == gameW/gameH)
    out.screenX = (int)centerX;
    out.screenY = (int)centerY;

    // 血量百分比 (從模型輸出估計，或預設 100)
    out.hpPct = 100;

    // 優先級 (根據 Y 座標，越近越大)
    out.priority = (int)(centerY * 100 / gameH);

    // 血條寬度
    out.width = (int)(x2 - x1);
}

// ============================================================
// 便利函式實現
// ============================================================
int ConvertYoloToVisualMonsters(YoloDetector* detector,
                                const std::vector<YoloBox>& boxes,
                                int imgW, int imgH,
                                int gameW, int gameH,
                                VisualMonster* outMonsters,
                                int maxMonsters) {
    if (!detector || boxes.empty() || !outMonsters) return 0;

    int count = 0;
    for (size_t i = 0; i < boxes.size() && count < maxMonsters; i++) {
        detector->ConvertToVisualMonster(boxes[i], imgW, imgH, gameW, gameH, outMonsters[count]);
        count++;
    }

    // 按優先級排序
    for (int i = 0; i < count - 1; i++) {
        for (int j = i + 1; j < count; j++) {
            if (outMonsters[j].priority > outMonsters[i].priority) {
                VisualMonster tmp = outMonsters[i];
                outMonsters[i] = outMonsters[j];
                outMonsters[j] = tmp;
            }
        }
    }

    return count;
}
