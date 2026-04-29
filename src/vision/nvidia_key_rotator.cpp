// nvidia_key_rotator.cpp
// NVIDIA API Key 自動輪替器實現
// ============================================================
#include "nvidia_key_rotator.h"

// 全域輪替器實例
static NvidiaKeyRotator s_keyRotator;
NvidiaKeyRotator& GetNvidiaKeyRotator() {
    return s_keyRotator;
}