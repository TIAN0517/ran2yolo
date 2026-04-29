-- ============================================================
-- CE Bridge Lua Script - JyTrainer Communication
-- ============================================================
-- 在 CE Console 執行: dofile("C:\\Users\\tian7\\Desktop\\BossJy\\JyTrainer_Win11\\scripts\\ce_bridge.lua")
-- ============================================================

local SHARED_PATH = [[C:\Users\tian7\Desktop\BossJy\JyTrainer_Win11\shared\]]

-- 讀取命令
function ReadCommand()
    local f = io.open(SHARED_PATH .. "command.txt", "r")
    if not f then return nil end
    local cmd = f:read("*all")
    f:close()
    return cmd ~= "" and cmd or nil
end

-- 寫入狀態
function WriteStatus(content)
    local f = io.open(SHARED_PATH .. "status.txt", "w")
    if f then
        f:write(content)
        f:close()
    end
end

-- 讀取玩家狀態
function GetPlayerStatus()
    local base = 0x015DF308
    local hp = readInteger(base)
    local maxHp = readInteger(0x015DF30C)
    local mp = readInteger(0x015DF310)
    local sp = readInteger(0x015DF318)
    local gold = readInteger(0x015DF258)
    local level = readInteger(0x015DF240)
    local posX = readFloat(0x015E0DF8)
    local posZ = readFloat(0x015E0DFC)
    local mapId = readInteger(0x015E0DEC)
    local targetId = readInteger(0x015E1D90)
    local locked = readInteger(0x015E338C)
    local arrowCount = readInteger(0x015DF8D0)

    return string.format("HP:%d/%d MP:%d SP:%d Lv:%d Gold:%d Pos:(%.1f,%.1f) Map:%d Target:%d Locked:%d Arrow:%d",
        hp, maxHp, mp, sp, level, gold, posX, posZ, mapId, targetId, locked, arrowCount)
end

-- 執行命令
function ExecuteCommand(cmd)
    if not cmd then return end

    -- 寫入回應
    local status = "CMD:" .. cmd .. " RESULT:"

    if cmd == "STATUS" then
        status = status .. GetPlayerStatus()

    elseif cmd == "SCAN_ATTACK" then
        -- 掃描攻擊封包目標
        local targetId = readInteger(0x015E1D90)
        status = status .. "TargetID:" .. targetId

    elseif cmd == "SCAN_MONSTERS" then
        -- 讀取怪物池指標（如果可用）
        local crows = readInteger(0x00CB0000 + 0xD087F8 + 0x38)
        status = status .. "CROWPtr:" .. string.format("0x%X", crows)

    elseif cmd == "NETPACKET_SCAN" then
        -- 掃描網路發送緩衝區
        status = status .. "NetSend:" .. readInteger(0x0123B5A0)

    else
        status = status .. "UNKNOWN_CMD"
    end

    WriteStatus(status)
end

-- 主循環（每 100ms 檢查一次）
function StartBridge()
    print("[CE Bridge] Started - Polling every 100ms")
    local lastCmd = ""

    while true do
        local cmd = ReadCommand()
        if cmd and cmd ~= lastCmd then
            lastCmd = cmd
            ExecuteCommand(cmd)
        end
        sleep(100)
    end
end

-- 單次讀取（不循環）
function Status()
    WriteStatus(GetPlayerStatus())
    print("[CE Bridge] Status written to status.txt")
end

-- 測試
function Test()
    print("CE Bridge Test OK")
    print("Base: 0x" .. string.format("%X", 0x015DF308))
    print("HP: " .. readInteger(0x015DF308))
end

print("[CE Bridge] Loaded - Use Status() for single read or StartBridge() for loop")