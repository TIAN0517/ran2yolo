#pragma once
// nvidia_key_rotator.h
// NVIDIA API Key 自動輪替器（解決 rate limit 問題）
// ============================================================
#ifndef _NVIDIA_KEY_ROTATOR_H_
#define _NVIDIA_KEY_ROTATOR_H_

#include <windows.h>
#include <vector>
#include <string>
#include <atomic>
#include <chrono>

class NvidiaKeyRotator {
public:
    NvidiaKeyRotator() {
        // 6 個 NVIDIA API Keys
        m_keys = {
            "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",
            "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",
            "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",
            "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",
            "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",
            "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"
        };

        m_failureCount.resize(m_keys.size(), 0);
        m_lastFailureTime.resize(m_keys.size(), 0);
    }

    // 取得目前要用的 Key（自動輪替）
    std::string GetCurrentKey() {
        size_t idx = m_currentIndex % m_keys.size();

        // 如果這個 Key 連續失敗 3 次，且還在 5 分鐘冷卻期內，就跳過
        auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        if (m_failureCount[idx] >= 3 && (now - m_lastFailureTime[idx]) < 300000) {
            m_currentIndex++;
            return GetCurrentKey(); // 遞迴找下一個
        }

        return m_keys[idx];
    }

    // 呼叫成功 → 重置失敗計數，切下一個
    void ReportSuccess() {
        size_t idx = m_currentIndex % m_keys.size();
        m_failureCount[idx] = 0;
        m_currentIndex++; // 輪替到下一個
        // 輸出到 Debug Console
        char buf[128];
        snprintf(buf, sizeof(buf), "[KeyRotator] Key %d 成功，切換到下一個", (int)idx);
        OutputDebugStringA(buf);
    }

    // 呼叫失敗 → 記錄失敗次數，馬上切下一個
    void ReportFailure() {
        size_t idx = m_currentIndex % m_keys.size();
        m_failureCount[idx]++;
        m_lastFailureTime[idx] = std::chrono::steady_clock::now().time_since_epoch().count();
        m_currentIndex++; // 立即切下一個
        // 輸出到 Debug Console
        char buf[128];
        snprintf(buf, sizeof(buf), "[KeyRotator] Key %d 失敗 (count=%d)，切換到下一個",
                 (int)idx, m_failureCount[idx]);
        OutputDebugStringA(buf);
    }

    size_t GetCurrentIndex() const {
        return m_currentIndex % m_keys.size();
    }

    int GetFailureCount() const {
        return m_failureCount[m_currentIndex % m_keys.size()];
    }

    // 取得所有 Key 的狀態（除錯用）
    std::string GetStatus() const {
        char buf[256];
        snprintf(buf, sizeof(buf), "Idx=%d Fails=[%d,%d,%d,%d,%d,%d]",
                 (int)GetCurrentIndex(),
                 m_failureCount[0], m_failureCount[1], m_failureCount[2],
                 m_failureCount[3], m_failureCount[4], m_failureCount[5]);
        return std::string(buf);
    }

private:
    std::vector<std::string> m_keys;
    std::vector<int> m_failureCount;
    std::vector<long long> m_lastFailureTime;
    std::atomic<size_t> m_currentIndex{0};
};

// 全域輪替器實例
NvidiaKeyRotator& GetNvidiaKeyRotator();

#endif // _NVIDIA_KEY_ROTATOR_H_