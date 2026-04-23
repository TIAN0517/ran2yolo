#pragma once
// ============================================================
// ONNX Runtime 配置 (Win7~Win11 通用)
// ============================================================
//
// ONNX Runtime v1.12.1 x86 (Win7 相容)
// 下載: https://github.com/microsoft/onnxruntime/releases/tag/v1.12.1
// 選擇: onnxruntime-win-x86-1.12.1.zip
//
// 目錄結構:
//   onnxruntime/              <- 解壓到此目錄
//   ├── include/
//   │   └── onnxruntime_c_api.h
//   ├── lib/
//   │   └── onnxruntime.lib
//   └── onnxruntime.dll       <- 複製到 exe 同目錄
//
// ============================================================

#ifndef ONNX_RUNTIME_CONFIG_H
#define ONNX_RUNTIME_CONFIG_H

// ============================================================
// ONNX Runtime DLL 路徑
// ============================================================
#ifdef _WIN32
    // DLL 放在 exe 同目錄
    #define ONNX_DLL_PATH "onnxruntime.dll"
#else
    #define ONNX_DLL_PATH "libonnxruntime.so"
#endif

// ============================================================
// 平台檢測
// ============================================================
#ifdef _WIN32
    #ifdef _M_IX86
        #define ONNX_PLATFORM "win-x86"
    #elif defined(_M_X64)
        #define ONNX_PLATFORM "win-x64"
    #endif
#endif

// ============================================================
// DLL 直接載入 (避免編譯依賴)
// ============================================================
#ifdef USE_DYNAMIC_ONNX

#include <windows.h>

// ONNX Runtime C API 函式指標
typedef OrtEnv* (ORT_API* OrtCreateEnvFn)(OrtLoggingLevel, _In_ const char*);
typedef void (ORT_API* OrtReleaseEnvFn)(OrtEnv*);
typedef OrtStatus* (ORT_API* OrtCreateSessionOptionsFn)(_Outptr_ OrtSessionOptions**);
typedef void (ORT_API* OrtReleaseSessionOptionsFn)(OrtSessionOptions*);
typedef OrtStatus* (ORT_API* OrtSetSessionGraphOptimizationLevelFn)(OrtSessionOptions*, GraphOptimizationLevel);
typedef OrtStatus* (ORT_API* OrtSetIntraOpNumThreadsFn)(OrtSessionOptions*, int);
typedef OrtStatus* (ORT_API* OrtSetInterOpNumThreadsFn)(OrtSessionOptions*, int);
typedef OrtStatus* (ORT_API* OrtCreateSessionFn)(OrtEnv*, _In_ const char*, OrtSessionOptions*, _Outptr_ OrtSession**);
typedef void (ORT_API* OrtReleaseSessionFn)(OrtSession*);
typedef OrtStatus* (ORT_API* OrtSessionGetInputCountFn)(OrtSession*, _Out_ size_t*);
typedef OrtStatus* (ORT_API* OrtSessionGetOutputCountFn)(OrtSession*, _Out_ size_t*);
typedef OrtStatus* (ORT_API* OrtSessionGetInputNameFn)(OrtSession*, size_t, OrtAllocator*, _Out_ char**);
typedef OrtStatus* (ORT_API* OrtSessionGetOutputNameFn)(OrtSession*, size_t, OrtAllocator*, _Out_ char**);
typedef OrtStatus* (ORT_API* OrtSessionGetInputTypeInfoFn)(OrtSession*, size_t, _Outptr_ OrtTypeInfo**);
typedef OrtStatus* (ORT_API* OrtSessionGetOutputTypeInfoFn)(OrtSession*, size_t, _Outptr_ OrtTypeInfo**);
typedef void (ORT_API* OrtReleaseTypeInfoFn)(OrtTypeInfo*);
typedef OrtStatus* (ORT_API* OrtCastTypeInfoToTensorFn)(_In_ const OrtTypeInfo*, _Outptr_ const OrtTensorTypeAndShapeInfo**);
typedef OrtStatus* (ORT_API* OrtGetDimensionsCountFn)(_In_ const OrtTensorTypeAndShapeInfo*, _Out_ size_t*);
typedef OrtStatus* (ORT_API* OrtGetDimensionsFn)(_In_ const OrtTensorTypeAndShapeInfo*, _Out_ int64_t*, size_t);
typedef OrtStatus* (ORT_API* OrtCreateMemoryInfoFn)(_In_ const char*, OrtAllocatorType, int, OrtMemType, _Outptr_ OrtMemoryInfo**);
typedef void (ORT_API* OrtReleaseMemoryInfoFn)(OrtMemoryInfo*);
typedef OrtStatus* (ORT_API* OrtCreateTensorWithDataAsOrtValueFn)(
    _In_ OrtMemoryInfo*, _In_ void*, size_t, _In_ const int64_t*, size_t, ONNXTensorElementDataType, _Outptr_ OrtValue**);
typedef OrtStatus* (ORT_API* OrtGetTensorDataFn)(
    _In_ OrtValue*, _Out_ void**, _Out_opt_ OrtMemoryInfo**, _Out_opt_ int64_t*, _Out_opt_ size_t*);
typedef OrtStatus* (ORT_API* OrtRunFn)(
    OrtSession*, _In_opt_ const OrtRunOptions*,
    _In_ const char* const*, _In_ const OrtValue* const*, size_t,
    _In_ const char* const*, size_t, _Out_ OrtValue**);
typedef OrtStatus* (ORT_API* OrtCreateRunOptionsFn)(_Outptr_ OrtRunOptions**);
typedef void (ORT_API* OrtReleaseRunOptionsFn)(OrtRunOptions*);
typedef const char* (ORT_API* OrtGetErrorMessageFn)(_In_ OrtStatus*);
typedef void (ORT_API* OrtReleaseStatusFn)(OrtStatus*);
typedef OrtStatus* (ORT_API* OrtGetAllocatorWithDefaultOptionsFn)(_Outptr_ OrtAllocator**);
typedef void (ORT_API* OrtReleaseAllocatorFn)(OrtAllocator*);
typedef void* (ORT_API* OrtAllocatorFn)(void*, size_t);
typedef void (ORT_API* OrtFreeFn)(void*, void*);
typedef OrtStatus* (ORT_API* OrtValueGetCountFn)(_In_ const OrtValue*, _Out_ size_t*);

// ONNX Runtime 動態載入器
class OnnxRuntimeLoader {
public:
    static OnnxRuntimeLoader& Instance() {
        static OnnxRuntimeLoader loader;
        return loader;
    }

    bool Load() {
        if (m_loaded) return true;

        m_dll = LoadLibraryA(ONNX_DLL_PATH);
        if (!m_dll) {
            printf("[OnnxRuntime] Failed to load DLL: %s\n", ONNX_DLL_PATH);
            return false;
        }

        // 載入所有需要的函式
        #define LOAD_FN(name) \
            name = (name##Fn)GetProcAddress(m_dll, #name); \
            if (!name) { printf("[OnnxRuntime] Missing function: %s\n", #name); return false; }

        LOAD_FN(OrtCreateEnv);
        LOAD_FN(OrtReleaseEnv);
        LOAD_FN(OrtCreateSessionOptions);
        LOAD_FN(OrtReleaseSessionOptions);
        LOAD_FN(OrtSetSessionGraphOptimizationLevel);
        LOAD_FN(OrtSetIntraOpNumThreads);
        LOAD_FN(OrtSetInterOpNumThreads);
        LOAD_FN(OrtCreateSession);
        LOAD_FN(OrtReleaseSession);
        LOAD_FN(OrtSessionGetInputCount);
        LOAD_FN(OrtSessionGetOutputCount);
        LOAD_FN(OrtSessionGetInputName);
        LOAD_FN(OrtSessionGetOutputName);
        LOAD_FN(OrtSessionGetInputTypeInfo);
        LOAD_FN(OrtSessionGetOutputTypeInfo);
        LOAD_FN(OrtReleaseTypeInfo);
        LOAD_FN(OrtCastTypeInfoToTensor);
        LOAD_FN(OrtGetDimensionsCount);
        LOAD_FN(OrtGetDimensions);
        LOAD_FN(OrtCreateMemoryInfo);
        LOAD_FN(OrtReleaseMemoryInfo);
        LOAD_FN(OrtCreateTensorWithDataAsOrtValue);
        LOAD_FN(OrtGetTensorData);
        LOAD_FN(OrtRun);
        LOAD_FN(OrtCreateRunOptions);
        LOAD_FN(OrtReleaseRunOptions);
        LOAD_FN(OrtGetErrorMessage);
        LOAD_FN(OrtReleaseStatus);
        LOAD_FN(OrtGetAllocatorWithDefaultOptions);
        LOAD_FN(OrtReleaseAllocator);
        LOAD_FN(OrtValueGetCount);

        #undef LOAD_FN

        m_loaded = true;
        printf("[OnnxRuntime] Loaded successfully\n");
        return true;
    }

    void Unload() {
        if (m_dll) {
            FreeLibrary(m_dll);
            m_dll = NULL;
        }
        m_loaded = false;
    }

    bool IsLoaded() const { return m_loaded; }

    // 函式指標成員
    OrtCreateEnvFn OrtCreateEnv;
    OrtReleaseEnvFn OrtReleaseEnv;
    OrtCreateSessionOptionsFn OrtCreateSessionOptions;
    OrtReleaseSessionOptionsFn OrtReleaseSessionOptions;
    OrtSetSessionGraphOptimizationLevelFn OrtSetSessionGraphOptimizationLevel;
    OrtSetIntraOpNumThreadsFn OrtSetIntraOpNumThreads;
    OrtSetInterOpNumThreadsFn OrtSetInterOpNumThreads;
    OrtCreateSessionFn OrtCreateSession;
    OrtReleaseSessionFn OrtReleaseSession;
    OrtSessionGetInputCountFn OrtSessionGetInputCount;
    OrtSessionGetOutputCountFn OrtSessionGetOutputCount;
    OrtSessionGetInputNameFn OrtSessionGetInputName;
    OrtSessionGetOutputNameFn OrtSessionGetOutputName;
    OrtSessionGetInputTypeInfoFn OrtSessionGetInputTypeInfo;
    OrtSessionGetOutputTypeInfoFn OrtSessionGetOutputTypeInfo;
    OrtReleaseTypeInfoFn OrtReleaseTypeInfo;
    OrtCastTypeInfoToTensorFn OrtCastTypeInfoToTensor;
    OrtGetDimensionsCountFn OrtGetDimensionsCount;
    OrtGetDimensionsFn OrtGetDimensions;
    OrtCreateMemoryInfoFn OrtCreateMemoryInfo;
    OrtReleaseMemoryInfoFn OrtReleaseMemoryInfo;
    OrtCreateTensorWithDataAsOrtValueFn OrtCreateTensorWithDataAsOrtValue;
    OrtGetTensorDataFn OrtGetTensorData;
    OrtRunFn OrtRun;
    OrtCreateRunOptionsFn OrtCreateRunOptions;
    OrtReleaseRunOptionsFn OrtReleaseRunOptions;
    OrtGetErrorMessageFn OrtGetErrorMessage;
    OrtReleaseStatusFn OrtReleaseStatus;
    OrtGetAllocatorWithDefaultOptionsFn OrtGetAllocatorWithDefaultOptions;
    OrtReleaseAllocatorFn OrtReleaseAllocator;
    OrtValueGetCountFn OrtValueGetCount;

private:
    OnnxRuntimeLoader() : m_dll(NULL), m_loaded(false) {}
    ~OnnxRuntimeLoader() { Unload(); }

    HMODULE m_dll;
    bool m_loaded;
};

// 巨集: 使用動態載入的 ONNX Runtime
#define ONNX_RT OnnxRuntimeLoader::Instance()

#endif // USE_DYNAMIC_ONNX

// ============================================================
// 編譯器設定
// ============================================================
// ONNX Runtime v1.12.1 x86 Windows
// 包含目錄: $(ProjectDir)onnxruntime\include
// 程式庫目錄: $(ProjectDir)onnxruntime\lib
// 附加相依性: onnxruntime.lib
// 後續建置事件: copy $(ProjectDir)onnxruntime\onnxruntime.dll $(OutDir)

#endif // ONNX_RUNTIME_CONFIG_H
