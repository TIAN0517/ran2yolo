-- ============================================================================
-- RAN2 封包結構驗證腳本
-- 用途: 驗證 NPC BUY/SELL/背包 等封包偏移
-- 使用: 在 CE Lua Console 執行 dofile("路徑\\validate_packets.lua")
-- ============================================================================

print("========================================")
print("  RAN2 封包結構驗證腳本")
print("========================================")

-- 封包結構定義
local PACKETS = {
  NPC_TALK = {msgid = 13522, size = 14, name = "NPC對話"},
  NPC_BUY  = {msgid = 13559, size = 35, name = "NPC購買"},
  NPC_SELL = {msgid = 13560, size = 33, name = "NPC出售"},
  ATTACK   = {msgid = 14882, size = 24, name = "攻擊"},
  MOVE     = {msgid = 13351, size = 12, name = "移動"},
  PICKUP   = {msgid = 4912,  size = 17, name = "撿物"}
}

-- NPCBuyPkt 欄位偏移 (from offsets.h)
local NPCBUY_FIELDS = {
  {name = "dwSize",    offset = 0,  size = 4, hex = true},
  {name = "dwMsgID",   offset = 4,  size = 4, hex = true},
  {name = "itemId",    offset = 8,  size = 4, hex = false},
  {name = "subId",     offset = 12, size = 4, hex = false},
  {name = "channel",   offset = 16, size = 4, hex = false},
  {name = "npcGlobId", offset = 20, size = 4, hex = true},
  {name = "tab",       offset = 24, size = 2, hex = false},
  {name = "slot",      offset = 26, size = 2, hex = false},
  {name = "qty",       offset = 28, size = 2, hex = false},
  {name = "emCrow",    offset = 30, size = 1, hex = false},
  {name = "crc",       offset = 31, size = 4, hex = true}
}

-- NPCSellPkt 欄位偏移
local NPCSELL_FIELDS = {
  {name = "dwSize",    offset = 0,  size = 4, hex = true},
  {name = "dwMsgID",   offset = 4,  size = 4, hex = true},
  {name = "itemId",    offset = 8,  size = 4, hex = false},
  {name = "subId",     offset = 12, size = 4, hex = false},
  {name = "invSlotX",  offset = 16, size = 2, hex = false},
  {name = "invSlotY",  offset = 18, size = 2, hex = false},
  {name = "npcGlobId", offset = 20, size = 4, hex = true},
  {name = "emCrow",    offset = 24, size = 1, hex = false},
  {name = "crc",       offset = 25, size = 4, hex = true}
}

-- 讀取記憶體並格式化
function readMemHex(addr, size)
  local bytes = {}
  for i = 0, size - 1 do
    local b = readBytes(addr + i, 1, false)
    table.insert(bytes, string.format("%02X", b))
  end
  return table.concat(bytes, " ")
end

function readMemDword(addr)
  return readInteger(addr)
end

function readMemWord(addr)
  return readInteger(addr)
end

function readMemByte(addr)
  return readInteger(addr)
end

-- 解析 MsgID (小端序)
function parseMsgID(hex)
  local parts = {}
  for part in string.gmatch(hex, "%S+") do
    table.insert(parts, part)
  end
  if #parts >= 2 then
    return tonumber(parts[1], 16) + tonumber(parts[2], 16) * 256
  end
  return 0
end

-- 驗證封包結構
function validatePacket(name, addr, expectedSize, fields)
  print("")
  print(string.format("[%s] 驗證 @ 0x%x (size=%d)", name, addr, expectedSize))

  local hex = readMemHex(addr, expectedSize)
  print("  Hex: " .. hex)

  local msgid = parseMsgID(string.sub(hex, 1, 12))
  print(string.format("  MsgID: 0x%04X (%d)", msgid, msgid))

  -- 解析每個欄位
  print("  欄位:")
  for i, f in ipairs(fields) do
    local fhex = string.sub(hex, f.offset * 3 + 1, f.offset * 3 + f.size * 3 - 1)
    local val
    if f.size == 4 then
      val = readMemDword(addr + f.offset)
    elseif f.size == 2 then
      val = readMemWord(addr + f.offset)
    else
      val = readMemByte(addr + f.offset)
    end

    if f.hex then
      print(string.format("    +%02d %-10s = 0x%08X (%d)", f.offset, f.name, val, val))
    else
      print(string.format("    +%02d %-10s = %d", f.offset, f.name, val))
    end
  end
end

-- 驗證函數
function validate(name)
  local addr = readInteger(0x12345678)  -- 需要替換為真實地址
  if addr and addr ~= 0 then
    local pkt = PACKETS[name]
    if pkt then
      validatePacket(name, addr, pkt.size, name == "NPC_BUY" and NPCBUY_FIELDS or NPCSELL_FIELDS)
    end
  else
    print("  地址無效，請提供有效地址")
  end
end

-- 手動讀取封包
function readPacket(name)
  print("")
  print("手動讀取 " .. name .. ":")
  print("請在 Memory View (Ctrl+G) 找到封包地址")
  print("然後執行: readPacketManual('" .. name .. "', 0x地址)")
end

function readPacketManual(name, addr)
  local pkt = PACKETS[name]
  if not pkt then
    print("未知封包: " .. name)
    return
  end

  print("")
  print("========================================")
  print("讀取 " .. pkt.name .. " 封包 @ 0x" .. string.format("%x", addr))
  print("========================================")

  local hex = readMemHex(addr, pkt.size)
  print("Hex: " .. hex)
  print("")

  -- 解析 MsgID
  local msgidBytes = {}
  for i = 0, 3 do
    local b = readBytes(addr + i, 1, false)
    table.insert(msgidBytes, string.format("%02X", b))
  end
  local msgid = parseMsgID(table.concat(msgidBytes, " "))
  print("MsgID: 0x" .. string.format("%04X", msgid) .. " (" .. msgid .. ")")

  -- 根據封包類型解析欄位
  if name == "NPC_BUY" then
    print("")
    print("欄位解析:")
    print(string.format("  itemId:    %d", readMemDword(addr + 8)))
    print(string.format("  subId:     %d", readMemDword(addr + 12)))
    print(string.format("  channel:   %d", readMemDword(addr + 16)))
    print(string.format("  npcGlobId: 0x%08X", readMemDword(addr + 20)))
    print(string.format("  tab:       %d", readMemWord(addr + 24)))
    print(string.format("  slot:      %d", readMemWord(addr + 26)))
    print(string.format("  qty:       %d", readMemWord(addr + 28)))
    print(string.format("  emCrow:    %d", readMemByte(addr + 30)))

  elseif name == "NPC_SELL" then
    print("")
    print("欄位解析:")
    print(string.format("  itemId:    %d", readMemDword(addr + 8)))
    print(string.format("  subId:     %d", readMemDword(addr + 12)))
    print(string.format("  invSlotX:  %d", readMemWord(addr + 16)))
    print(string.format("  invSlotY:  %d", readMemWord(addr + 18)))
    print(string.format("  npcGlobId: 0x%08X", readMemDword(addr + 20)))
    print(string.format("  emCrow:    %d", readMemByte(addr + 24)))
  end

  print("")
  print("========================================")
end

-- 搜索封包
function searchPacket(msgId)
  print("")
  print("搜索 MsgID: 0x" .. string.format("%04X", msgId) .. " (" .. msgId .. ")")

  -- 小端序 bytes
  local b1 = msgId % 256
  local b2 = math.floor(msgId / 256) % 256

  print("搜索特徵: " .. string.format("%02X %02X", b1, b2))
  print("")
  print("請在 CE 主介面操作:")
  print("1. Ctrl+F 打开搜索")
  print("2. Value type: Byte")
  print("3. 勾選 Hex")
  print("4. 輸入: " .. string.format("%02X %02X", b1, b2))
  print("5. First Scan")
  print("6. 在遊戲中操作後，Next Scan")
end

-- 列出所有封包
function listPackets()
  print("")
  print("可用封包:")
  for name, pkt in pairs(PACKETS) do
    print(string.format("  %-10s MsgID=0x%04X (%d) size=%d", name, pkt.msgid, pkt.msgid, pkt.size))
  end
  print("")
  print("使用方式:")
  print("  searchPacket(13559)              -- 搜索 NPC_BUY")
  print("  readPacketManual('NPC_BUY', 0x地址)  -- 讀取指定地址的封包")
end

print("腳本載入完成!")
print("")
listPackets()