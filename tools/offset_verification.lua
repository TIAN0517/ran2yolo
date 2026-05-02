-- CE Lua: Offset 驗證腳本
-- 執行此腳本讀取所有玩家屬性偏移

local function verifyOffsets()
  local gameBase = getAddress("Game.exe")
  if not gameBase or gameBase == 0 then
    print("Game.exe not found!")
    return
  end

  print("")
  print("========================================")
  print("  Ran2 Online Offset Verification")
  print("========================================")
  print(string.format("GameBase: 0x%08X", gameBase))
  print("")

  -- 基礎屬性
  local basicOffsets = {
    {"HP", 0x930300},
    {"MaxHP", 0x930304},
    {"MP", 0x930308},
    {"MaxMP", 0x93030C},
    {"SP", 0x930310},
    {"MaxSP", 0x930314},
    {"Gold", 0x930250},
    {"Level", 0x930248},
    {"EXP", 0x9302D8},
    {"EXPMax", 0x9302E0},
    {"ArrowCount", 0x9308D8},
    {"TalismanCount", 0x9308DC},
  }

  print("--- Basic Stats ---")
  for _, t in ipairs(basicOffsets) do
    local name, off = t[1], t[2]
    local addr = gameBase + off
    local value = readInteger(addr)
    print(string.format("  %-15s = %-10d @ 0x%08X", name, value or -1, addr))
  end

  -- 屬性 (需確認)
  local statOffsets = {
    {"STR", 0x932BF4},
    {"VIT", 0x932BF8},
    {"SPR", 0x932BFC},
    {"DEX", 0x932C00},
    {"END", 0x930C04},
  }

  print("")
  print("--- Character Stats (需確認) ---")
  for _, t in ipairs(statOffsets) do
    local name, off = t[1], t[2]
    local addr = gameBase + off
    local value = readInteger(addr)
    print(string.format("  %-15s = %-10d @ 0x%08X", name, value or -1, addr))
  end

  -- 攻擊/防禦 (需確認)
  local combatOffsets = {
    {"PhysAtkMin", 0x933128},
    {"PhysAtkMax", 0x93312C},
    {"Defense", 0x933130},
    {"SprAtkMin", 0x933134},
    {"SprAtkMax", 0x933138},
    {"MagicDef", 0x93313C},  -- 這個是什麼?
    {"Accuracy", 0x932E20},   -- 這個是什麼?
  }

  print("")
  print("--- Combat Stats (需確認) ---")
  for _, t in ipairs(combatOffsets) do
    local name, off = t[1], t[2]
    local addr = gameBase + off
    local value = readInteger(addr)
    print(string.format("  %-15s = %-10d @ 0x%08X", name, value or -1, addr))
  end

  -- 座標
  local posOffsets = {
    {"MapID", 0x930DEC},
    {"PosX", 0x930DF8},
    {"PosY", 0x930E00},
    {"PosZ", 0x930DFC},
  }

  print("")
  print("--- Position ---")
  for _, t in ipairs(posOffsets) do
    local name, off = t[1], t[2]
    local addr = gameBase + off
    if name:find("Pos") then
      local value = readFloat(addr)
      print(string.format("  %-15s = %-10.2f @ 0x%08X", name, value or -1, addr))
    else
      local value = readInteger(addr)
      print(string.format("  %-15s = %-10d @ 0x%08X", name, value or -1, addr))
    end
  end

  -- GLCharacter 指針
  print("")
  print("--- GLCharacter Pointer ---")
  local glCharOff = 0x92F19C
  local glCharPtr = gameBase + glCharOff
  local glCharValue = readPointer(glCharPtr)
  print(string.format("  GLChar_Ptr    = 0x%08X @ 0x%08X", glCharValue or 0, glCharPtr))

  if glCharValue and glCharValue ~= 0 then
    print("")
    print("--- GLCharacter Data ---")
    local charOffsets = {
      {"Name", 0x050},
      {"HP", 0x7B0},
      {"MaxHP", 0x7B4},
      {"PosX", 0x890},
      {"PosY", 0x894},
      {"PosZ", 0x898},
      {"ServerID", 0x91C},
    }
    for _, t in ipairs(charOffsets) do
      local name, off = t[1], t[2]
      local addr = glCharValue + off
      if name:find("Pos") then
        local value = readFloat(addr)
        print(string.format("  %-15s = %-10.2f @ 0x%08X", name, value or -1, addr))
      else
        local value = readInteger(addr)
        print(string.format("  %-15s = %-10d @ 0x%08X", name, value or -1, addr))
      end
    end
  end

  print("")
  print("========================================")
end

-- 執行驗證
verifyOffsets()

return {verifyOffsets = verifyOffsets}
