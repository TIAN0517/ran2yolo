-- ============================================================
-- 座標/地圖驗證腳本
-- 去野外走動測試
-- ============================================================

local addresses = {
  -- 座標區
  {name = "PosX",     addr = "Game.exe+930DF8", isFloat = true},
  {name = "PosZ",     addr = "Game.exe+930DFC", isFloat = true},
  {name = "PosY",     addr = "Game.exe+930E00", isFloat = true},

  -- 地圖
  {name = "MapID",    addr = "Game.exe+930DEC", isFloat = false},

  -- 等級（確認讀取）
  {name = "Level",    addr = "Game.exe+930248", isFloat = false},

  -- HP 確認
  {name = "HP",       addr = "Game.exe+930300", isFloat = false},
  {name = "MaxHP",    addr = "Game.exe+930304", isFloat = false},
}

print("========================================")
print("座標/地圖驗證腳本")
print("去野外走動測試")
print("========================================")

for i, info in ipairs(addresses) do
  local address = getAddress(info.addr)

  if address ~= nil and address ~= 0 then
    local ok, value = pcall(function()
      if info.isFloat then
        return readFloat(address)
      else
        return readInteger(address)
      end
    end)

    if ok then
      local display
      if info.isFloat then
        display = string.format("%.2f", value)
      else
        display = string.format("%d", value)
      end
      print(string.format("%-8s = %12s  [%s]", info.name, display, info.addr))
    else
      print(string.format("%-8s = ❌讀取錯誤    [%s]", info.name, info.addr))
    end
  else
    print(string.format("%-8s = ❌地址無效    [%s]", info.name, info.addr))
  end
end

print("========================================")
print("請走動後再執行一次，確認座標是否變化")
print("========================================")
