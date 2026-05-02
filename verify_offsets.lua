-- ============================================================
-- JyTrainer 偏移驗證腳本
-- 用法：在 Cheat Engine 中打開 Memory View -> Tools -> Execute Script
-- ============================================================

local addresses = {
  -- HP 狀態區 (0x930XXX) - 應該穩定讀取
  {name = "HP",       addr = "Game.exe+930300", isFloat = false},
  {name = "MaxHP",    addr = "Game.exe+930304", isFloat = false},
  {name = "MP",       addr = "Game.exe+930308", isFloat = false},
  {name = "MaxMP",    addr = "Game.exe+93030C", isFloat = false},
  {name = "SP",       addr = "Game.exe+930310", isFloat = false},
  {name = "MaxSP",    addr = "Game.exe+930314", isFloat = false},

  -- 其他 0x930XXX
  {name = "Gold",     addr = "Game.exe+930250", isFloat = false},
  {name = "Level",    addr = "Game.exe+930248", isFloat = false},
  {name = "ArrowCnt", addr = "Game.exe+9308D8", isFloat = false},
  {name = "MapID",    addr = "Game.exe+930DEC", isFloat = false},
  {name = "PosX",     addr = "Game.exe+930DF8", isFloat = true},
  {name = "PosZ",     addr = "Game.exe+930DFC", isFloat = true},

  -- 角色屬性區 (0x932XXX) - 待確認
  {name = "STR",      addr = "Game.exe+932BF4", isFloat = false},
  {name = "VIT",      addr = "Game.exe+932BF8", isFloat = false},
  {name = "SPR",      addr = "Game.exe+932BFC", isFloat = false},
  {name = "DEX",      addr = "Game.exe+932C00", isFloat = false},
  {name = "END",      addr = "Game.exe+930C04", isFloat = false},
  {name = "CombatPwr", addr = "Game.exe+932200", isFloat = false},

  -- 攻擊力區 (0x933XXX) - 待確認
  {name = "PhysAtkMin", addr = "Game.exe+933128", isFloat = false},
  {name = "PhysAtkMax", addr = "Game.exe+93312C", isFloat = false},
  {name = "Defense",    addr = "Game.exe+933130", isFloat = false},
  {name = "SprAtkMin", addr = "Game.exe+933134", isFloat = false},
  {name = "SprAtkMax", addr = "Game.exe+933138", isFloat = false},
}

print("========================================")
print("JyTrainer 偏移驗證腳本")
print("========================================")

local success = 0
local failed = 0

for i, info in ipairs(addresses) do
  local status = "❌ 失敗"
  local value = "N/A"

  local address = getAddress(info.addr)
  if address ~= nil and address ~= 0 then
    local ok, read_value = pcall(function()
      if info.isFloat then
        return readFloat(address)
      else
        return readInteger(address)
      end
    end)

    if ok then
      if info.isFloat then
        value = string.format("%.2f", read_value)
      else
        value = string.format("%d", read_value)
      end
      status = "✅ " .. string.format("%12s", value)
      success = success + 1
    else
      status = "❌ 讀取錯誤"
      failed = failed + 1
    end
  else
    status = "❌ 地址無效"
    failed = failed + 1
  end

  print(string.format("%-12s %-20s = %s", info.name, "[" .. info.addr .. "]", status))
end

print("========================================")
print(string.format("結果: 成功 %d, 失敗 %d", success, failed))
print("========================================")
