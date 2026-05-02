-- ============================================================
-- JyTrainer_Win11 偏移掃描腳本
-- 用法: 在 CE 中載入 Game.exe，然後執行此腳本
-- ============================================================

local GAME_NAME = "Game.exe"
local OUTPUT_FILE = "jy_offsets_result.txt"

-- 確保遊戲已載入
local gameModule = getModuleHandle(GAME_NAME)
if gameModule == 0 then
  print("錯誤: 找不到 " .. GAME_NAME)
  return
end
print("Game.exe Base: 0x" .. string.format("%X", gameModule))

-- 開啟輸出檔案
local f = io.open(OUTPUT_FILE, "w")
f:write("-- JyTrainer 偏移驗證結果\n")
f:write("-- 時間: " .. os.date("%Y-%m-%d %H:%M:%S") .. "\n")
f:write("-- GameBase: 0x" .. string.format("%X", gameModule) .. "\n\n")

-- 輔助函式
local function readPtr(addr)
  return readPointer(addr) or 0
end

local function readInt(addr)
  return readInteger(addr) or 0
end

local function readFloat(addr)
  return readFloat(addr) or 0.0
end

-- ============================================================
-- 1. 找 GLGaeaServer 指標 (TLS 方式)
-- ============================================================
print("\n[1] 掃描 GLGaeaServer...")
f:write("[Identity]\n")

-- 方法1: 掃描 TLS 回調中的 GaeaServer 指標
local gaeaServerAddr = 0
-- 常見模式: 在某個已知地址附近找指標

-- 嘗試讀取舊的 TLS 位置 (可能已過時)
local oldTLSAddr = gameModule + 0xD087F8
local tlsValue1 = readPtr(oldTLSAddr)
print("  TLS old addr (0xD087F8): 0x" .. string.format("%X", tlsValue1))

-- 嘗試掃描記憶體找 GLGaeaServer
local found = false
local scan = createMemScan()
scan.firstScan(soUnknownValue, sftByte, nil, gameModule, gameModule + 0x900000, true, false, true)
local results = createFoundList(scan)
results.initialize()

-- 在結果中找指標
for i = 0, math.min(results.Count - 1, 1000) do
  local addr = results.Address[i]
  local val = readPtr(addr)
  -- GLGaeaServer 通常在某個穩定的地址範圍內
  if val >= gameModule and val < gameModule + 0x1000000 then
    -- 進一步驗證: 讀取指標指向的內容
    local firstField = readPtr(val)
    if firstField ~= 0 then
      print("  可能找到: [0x" .. string.format("%X", addr) .. "] = 0x" .. string.format("%X", val))
      -- 寫入到檔案
      local offset = addr - gameModule
      f:write("GLGaeaServer_Obj=0x" .. string.format("%X", offset) .. "\n")
      gaeaServerAddr = val
      found = true
      break
    end
  end
end
results.destroy()
scan.destroy()

if not found then
  print("  未自動找到，請手動掃描...")
  f:write("-- GLGaeaServer: 請手動掃描\n")
  f:write("GLGaeaServer_Obj=??????\n")
end

-- ============================================================
-- 2. 找 GLCharClient 指標
-- ============================================================
print("\n[2] 掃描 GLCharClient...")
f:write("\n[PlayerPointer]\n")

-- GLCharClient 通常在 TLS 中
local charClientAddr = 0

-- 嘗試舊位置
local oldCharAddr = gameModule + 0xD30678
local charValue = readPtr(oldCharAddr)
print("  舊位置 (0xD30678): 0x" .. string.format("%X", charValue))

if charValue ~= 0 then
  f:write("GLCharClient_Ptr=0xD30678\n")
  f:write("-- 驗證: 讀到值 = 0x" .. string.format("%X", charValue) .. "\n")
  charClientAddr = charValue
else
  -- 嘗試其他位置
  local candidates = {
    0xD30678, 0xD3067C, 0xD30500, 0xD30400
  }
  for _, off in ipairs(candidates) do
    local val = readPtr(gameModule + off)
    if val ~= 0 then
      print("  在 0x" .. string.format("%X", off) .. " 找到: 0x" .. string.format("%X", val))
      f:write("GLCharClient_Ptr=0x" .. string.format("%X", off) .. "\n")
      charClientAddr = val
      break
    end
  end
end

-- ============================================================
-- 3. 驗證玩家屬性偏移
-- ============================================================
print("\n[3] 驗證玩家屬性...")
f:write("\n[Player]\n")

if charClientAddr ~= 0 then
  -- 驗證 HP
  local hp = readInt(charClientAddr + 0x930300)
  print("  HP @ +0x930300: " .. hp)
  f:write("HP=930300 -- 驗證值: " .. hp .. "\n")

  -- 驗證 MP
  local mp = readInt(charClientAddr + 0x930308)
  print("  MP @ +0x930308: " .. mp)
  f:write("MP=930308 -- 驗證值: " .. mp .. "\n")

  -- 驗證 Gold
  local gold = readInt(charClientAddr + 0x930250)
  print("  Gold @ +0x930250: " .. gold)
  f:write("Gold=930250 -- 驗證值: " .. gold .. "\n")

  -- 驗證 Level
  local level = readInt(charClientAddr + 0x930248)
  print("  Level @ +0x930248: " .. level)
  f:write("Level=930248 -- 驗證值: " .. level .. "\n")

  -- 驗證座標
  local posX = readFloat(charClientAddr + 0x930DF8)
  local posZ = readFloat(charClientAddr + 0x930DFC)
  print("  PosX @ +0x930DF8: " .. string.format("%.2f", posX))
  print("  PosZ @ +0x930DFC: " .. string.format("%.2f", posZ))
  f:write("PosX=930DF8 -- 驗證值: " .. string.format("%.2f", posX) .. "\n")
  f:write("PosZ=930DFC -- 驗證值: " .. string.format("%.2f", posZ) .. "\n")

  -- 驗證 STR
  local str = readInt(charClientAddr + 0x932BF4)
  print("  STR @ +0x932BF4: " .. str)
  f:write("STR=932BF4 -- 驗證值: " .. str .. "\n")

  -- 驗證 VIT
  local vit = readInt(charClientAddr + 0x932BF8)
  print("  VIT @ +0x932BF8: " .. vit)
  f:write("VIT=932BF8 -- 驗證值: " .. vit .. "\n")
end

-- ============================================================
-- 4. 找實體池 (CROwList)
-- ============================================================
print("\n[4] 掃描實體池...")
f:write("\n[EntityPool]\n")

if gaeaServerAddr ~= 0 then
  -- 指標鏈: GaeaServer -> +0x38 -> +0x38 -> +0xA790 = CROwList
  local step1 = readPtr(gaeaServerAddr + 0x38)
  print("  GaeaServer+0x38: 0x" .. string.format("%X", step1))

  if step1 ~= 0 then
    local step2 = readPtr(step1 + 0x38)
    print("  +0x38+0x38: 0x" .. string.format("%X", step2))

    if step2 ~= 0 then
      local crowList = readPtr(step2 + 0xA790)
      print("  CROwList @ +0xA790: 0x" .. string.format("%X", crowList))
      f:write("CROwList=0xA790 -- 驗證值: 0x" .. string.format("%X", crowList) .. "\n")

      if crowList ~= 0 then
        f:write("CROwList_Offset=A790\n")
        f:write("LandManPtr_Offset=38\n")
      end
    end
  end
end

-- ============================================================
-- 5. 驗證攻擊目標偏移
-- ============================================================
print("\n[5] 驗證攻擊目標偏移...")
f:write("\n[Target]\n")

if charClientAddr ~= 0 then
  local hasTarget = readInt(charClientAddr + 0x93275C)
  local targetId = readInt(charClientAddr + 0x931D90)
  local lockedState = readInt(charClientAddr + 0x930D28)

  print("  HasTarget @ +0x93275C: " .. hasTarget)
  print("  TargetID @ +0x931D90: 0x" .. string.format("%X", targetId))
  print("  LockedState @ +0x930D28: " .. lockedState)

  f:write("HasTarget=93275C -- 驗證值: " .. hasTarget .. "\n")
  f:write("ID=931D90 -- 驗證值: 0x" .. string.format("%X", targetId) .. "\n")
  f:write("LockedState=930D28 -- 驗證值: " .. lockedState .. "\n")
end

-- ============================================================
-- 完成
-- ============================================================
f:write("\n-- 結束\n")
f:close()

print("\n========================================")
print("掃描完成！")
print("結果已寫入: " .. OUTPUT_FILE)
print("========================================")
print("\n請檢查輸出檔案，手動驗證紅色標記的偏移")

-- 開啟檔案
shellExecute(OUTPUT_FILE)
