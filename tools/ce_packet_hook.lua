-- CE Lua: Ran2 Packet Capture
-- 在 Cheat Engine 中執行此腳本來捕獲封包
-- 當你打怪時，攻擊/技能封包會被記錄

-- ============= 配置 =============
local LOG_FILE = "C:\\Users\\tian7\\Desktop\\BossJy\\JyTrainer_Win11\\tools\\packet_log.txt"
local MAX_LOG_SIZE = 10000  -- 最大日誌行數

-- ============= 封包解析 =============
local packetNames = {
  [0x3A22] = "ATTACK",
  [0x3A23] = "ATTACK2",
  [0x3E3]  = "SKILL",
  [0x3427] = "PICKUP_ITEM",
  [0x3429] = "PICKUP_GOLD",
  [0x34F7] = "NPC_BUY",
  [0x34F8] = "NPC_SELL",
  [0x34D2] = "NPC_TALK",
  [0x34D3] = "NPC_CLOSE",
  [0x3424] = "MOVE",
  [0x3425] = "MOVE_STOP",
}

local function getPacketName(msgId)
  return packetNames[msgId] or string.format("UNKNOWN_0x%04X", msgId)
end

-- ============= 日誌輸出 =============
local logCount = 0

local function writeLog(msg)
  local f = io.open(LOG_FILE, "a")
  if f then
    f:write(msg .. "\n")
    f:close()
  end
  logCount = logCount + 1
  print(msg)
end

local function writePacketHex(dir, data, size)
  local timestamp = os.date("%H:%M:%S")

  if size < 4 then return end

  -- 讀取 MsgID (假設在 offset 4 或 8)
  local msgIdOffset = 5  -- 1-indexed for string.sub
  local msgId = 0
  local p1, p2, p3, p4 = string.byte(data, msgIdOffset, msgIdOffset + 3)
  if p1 then
    msgId = p1 + p2 * 256 + p3 * 65536 + p4 * 16777216
  end

  local name = getPacketName(msgId)
  local hex = ""
  for i = 1, math.min(size, 64) do
    hex = hex .. string.format("%02X ", string.byte(data, i))
  end

  writeLog(string.format("[%s] %s | ID=0x%04X | %s | HEX: %s",
    timestamp, dir, msgId, name, hex))
end

-- ============= 初始化日誌文件 =============
local function initLog()
  local f = io.open(LOG_FILE, "w")
  if f then
    f:write("=== Ran2 Packet Capture Log ===\n")
    f:write("Time: " .. os.date("%Y-%m-%d %H:%M:%S") .. "\n")
    f:write("================================\n\n")
    f:close()
  end
  writeLog("Packet capture started!")
  writeLog("")
end

-- ============= Send Hook =============
local origSend = nil

local function onSend(socket, data, size)
  if size and size > 0 and data then
    writePacketHex("SEND", data, size)
  end
  if origSend then
    return origSend(socket, data, size)
  end
  return size
end

-- ============= Recv Hook =============
local origRecv = nil

local function onRecv(socket, data, size)
  if size and size > 0 and data then
    writePacketHex("RECV", data, size)
  end
  if origRecv then
    return origRecv(socket, data, size)
  end
  return size
end

-- ============= 內存讀取測試 =============
local function readPlayerOffsets()
  local gameBase = getAddress("Game.exe")
  if not gameBase or gameBase == 0 then
    print("Game.exe not found!")
    return
  end

  print("")
  print("=== Player Data Read Test ===")
  print(string.format("GameBase: 0x%08X", gameBase))

  -- HP/MP/SP
  local offsets = {
    {"HP", 0x930300},
    {"MaxHP", 0x930304},
    {"MP", 0x930308},
    {"MaxMP", 0x93030C},
    {"SP", 0x930310},
    {"MaxSP", 0x930314},
    {"Gold", 0x930250},
    {"Level", 0x930248},
    {"STR", 0x932BF4},
    {"DEX", 0x932C00},
    {"SPR", 0x932BFC},
    {"VIT", 0x932BF8},
    {"PhysAtkMin", 0x933128},
    {"PhysAtkMax", 0x93312C},
    {"Defense", 0x933130},
    {"SprAtkMin", 0x933134},
    {"SprAtkMax", 0x933138},
    {"ArrowCount", 0x9308D8},
    {"MapID", 0x930DEC},
    {"PosX", 0x930DF8},
    {"PosZ", 0x930DFC},
  }

  for _, t in ipairs(offsets) do
    local name = t[1]
    local off = t[2]
    local addr = gameBase + off
    local value = readInteger(addr)
    if value then
      print(string.format("  %-15s = %d @ 0x%08X", name, value, addr))
    else
      print(string.format("  %-15s = FAILED @ 0x%08X", name, addr))
    end
  end

  print("")
end

-- ============= 開始捕獲 =============
initLog()

print("")
print("=== Instructions ===")
print("1. 攻擊怪物 - 捕獲 ATTACK/SKILL 封包")
print("2. 走動 - 捕獲 MOVE 封包")
print("3. 撿物 - 捕獲 PICKUP 封包")
print("4. 攻擊 NPC - 捕獲 NPC_BUY/SELL 封包")
print("")
print("按 Ctrl+L 打开日志文件查看结果")
print("")

-- 自動讀取一次玩家數據
readPlayerOffsets()

-- 返回掛鉤函數供 CE 調用
return {
  onSend = onSend,
  onRecv = onRecv,
  readPlayerOffsets = readPlayerOffsets,
  LOG_FILE = LOG_FILE
}
