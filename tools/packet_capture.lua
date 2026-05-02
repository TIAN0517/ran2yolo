-- packet_capture.lua
-- Ran2 Online 封包捕獲腳本
-- 用於 CE Lua Bridge 或獨立執行
-- 捕獲戰鬥相關封包並輸出到日誌

local PACKET_LOG_FILE = "packet_capture_log.txt"
local MAX_PACKETS = 500

-- 捕獲的封包計數
local packetCount = 0
local sendCount = 0
local recvCount = 0

-- 初始化日誌
local function initLog()
    local f = io.open(PACKET_LOG_FILE, "w")
    if f then
        f:write("=== Ran2 Packet Capture ===\n")
        f:write("Time: " .. os.date("%Y-%m-%d %H:%M:%S") .. "\n")
        f:write("==============================\n\n")
        f:close()
    end
end

-- 寫入封包到日誌
local function logPacket(direction, packetData, size)
    local f = io.open(PACKET_LOG_FILE, "a")
    if not f then return end

    local timestamp = os.date("%H:%M:%S")
    local dirStr = direction == "SEND" and ">>>" or "<<<"
    local typeStr = direction == "SEND" and "SEND" or "RECV"

    f:write(string.format("[%s] %s %s (size=%d)\n", timestamp, dirStr, typeStr, size))

    -- 十六進制輸出
    local hexStr = ""
    local asciiStr = ""
    for i = 1, math.min(size, 128) do
        local b = string.byte(packetData, i) or 0
        hexStr = hexStr .. string.format("%02X ", b)
        if i % 16 == 0 then
            f:write("  " .. asciiStr .. "\n")
            asciiStr = ""
        elseif i % 8 == 0 then
            asciiStr = asciiStr .. " "
        end
        if b >= 32 and b <= 126 then
            asciiStr = asciiStr .. string.char(b)
        else
            asciiStr = asciiStr .. "."
        end
    end
    if #asciiStr > 0 then
        f:write(string.rep(" ", 56 - (#hexStr % 59)) .. asciiStr .. "\n")
    end

    -- 解析常見封包類型
    if size >= 8 then
        local msgId = 0
        local opcode = 0
        local subtype = 0

        -- 嘗試讀取 MsgID (假設在 offset 4 或 8)
        if size >= 12 then
            -- v7.6 格式: [size(4)] [session(4)] [msgId(4)] [data...]
            local p = string.unpack("I4", packetData, 5)
            msgId = p

            -- 嘗試解析攻擊封包
            if msgId == 0x3A22 then
                f:write("  >>> ATTACK PACKET DETECTED <<<\n")
            elseif msgId == 0x34F7 then
                f:write("  >>> NPC_BUY PACKET <<<\n")
            elseif msgId == 0x34F8 then
                f:write("  >>> NPC_SELL PACKET <<<\n")
            elseif msgId == 0x3427 then
                f:write("  >>> PICKUP_ITEM PACKET <<<\n")
            elseif msgId == 0x3429 then
                f:write("  >>> PICKUP_GOLD PACKET <<<\n")
            elseif msgId == 0x34D2 then
                f:write("  >>> NPC_TALK PACKET <<<\n")
            end
        end
    end

    f:write("\n")
    f:close()
end

-- Hook WS2_32 send 函數
local function hookSend()
    print("[PacketCapture] Hooking WS2_32 send...")

    -- 這個函數需要在 CE Lua 中實現
    -- 這裡只是佔位

    print("[PacketCapture] Send hook installed!")
end

-- Hook WS2_32 recv 函數
local function hookRecv()
    print("[PacketCapture] Hooking WS2_32 recv...")

    -- 這個函數需要在 CE Lua 中實現
    -- 這裡只是佔位

    print("[PacketCapture] Recv hook installed!")
end

-- 初始化
initLog()
print("========================================")
print("  Ran2 Packet Capture Started")
print("  Log file: " .. PACKET_LOG_FILE)
print("  Press Ctrl+C to stop")
print("========================================")
print("")

-- 捕獲狀態
local isCapturing = true

-- 主循環 (在 CE Lua 中這部分會被定時調用)
local function captureLoop()
    -- 這個函數會被定時調用
    -- 在實際實現中，這裡檢查捕獲到的封包

    if packetCount >= MAX_PACKETS then
        print("[PacketCapture] Max packets reached, stopping...")
        isCapturing = false
    end
end

-- 手動觸發的封包測試
local function testPacket(msgId, data)
    print(string.format("[Test] Sending test packet: MsgID=0x%04X", msgId))
    logPacket("SEND", data, #data)
end

-- 返回狀態
return {
    isCapturing = function() return isCapturing end,
    getPacketCount = function() return packetCount end,
    getSendCount = function() return sendCount end,
    getRecvCount = function() return recvCount end,
    captureLoop = captureLoop,
    testPacket = testPacket
}
