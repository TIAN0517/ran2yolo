-- ============================================================================
-- CE MCP Bridge v11.4.0
-- RAN2 Game Memory Access via Named Pipe
-- ============================================================================
-- Usage: dofile("path\\ce_mcp_bridge.lua")
-- ============================================================================

print("========================================")
print("  CE MCP Bridge v11.4.0 Loading...")
print("========================================")

-- Named Pipe Name
local PIPE_NAME = "CE_MCP_Bridge_v99"
local PIPE_BUFFER_SIZE = 4096

-- Global command handler storage
local g_commandHandlers = {}

-- Connection state
local g_pipeHandle = nil
local g_connected = false
local g_lastError = ""

-- Helper: Create named pipe client
local function createPipeClient()
    if g_pipeHandle then
        return g_pipeHandle
    end

    -- Open existing pipe
    g_pipeHandle = io.open("\\\\.\\pipe\\" .. PIPE_NAME, "r+b")

    if g_pipeHandle then
        print("[MCP] Connected to pipe: " .. PIPE_NAME)
        g_connected = true
        return g_pipeHandle
    else
        g_lastError = "Pipe not found: " .. PIPE_NAME
        return nil
    end
end

-- Helper: Close pipe
local function closePipe()
    if g_pipeHandle then
        g_pipeHandle:close()
        g_pipeHandle = nil
        g_connected = false
        print("[MCP] Pipe closed")
    end
end

-- Helper: Send command and get response
local function sendCommand(cmd)
    if not createPipeClient() then
        return nil, g_lastError
    end

    -- Send command
    local written = g_pipeHandle:write(cmd .. "\n")
    if not written then
        return nil, "Write failed"
    end
    g_pipeHandle:flush()

    -- Read response
    local response = g_pipeHandle:read("*line")
    if not response then
        closePipe()
        return nil, "Read failed"
    end

    return response
end

-- Helper: Parse JSON (simple parser for responses)
local function parseJSON(str)
    local json = {}
    local i = 1
    local n = #str

    -- Handle null
    if str == "null" then
        return nil
    end

    -- Handle numbers
    local num = tonumber(str)
    if num then
        return num
    end

    -- Handle booleans
    if str == "true" then return true end
    if str == "false" then return false end

    -- Handle strings
    if str:sub(1,1) == '"' then
        return str:sub(2, -2)
    end

    -- Handle arrays
    if str:sub(1,1) == "[" then
        local arr = {}
        local content = str:sub(2, -2)
        for item in content:gmatch("[^,]+") do
            table.insert(arr, parseJSON(item))
        end
        return arr
    end

    -- Handle objects
    if str:sub(1,1) == "{" then
        local content = str:sub(2, -2)
        for key, val in content:gmatch('"([^"]+)"%s*:%s*([^,]+)') do
            json[key] = parseJSON(val)
        end
        return json
    end

    return str
end

-- Global function for command handling
function handleCommand(cmdType, params)
    local cmd = string.format('{"cmd":"%s"', cmdType)

    if params then
        local pstr = ""
        for k, v in pairs(params) do
            if type(v) == "string" then
                pstr = pstr .. string.format(',"%s":"%s"', k, v)
            else
                pstr = pstr .. string.format(',"%s":%s', k, tostring(v))
            end
        end
        cmd = cmd .. pstr
    end

    cmd = cmd .. "}"

    local resp = sendCommand(cmd)
    if resp then
        return parseJSON(resp)
    end
    return nil
end

-- Register command handler
function registerCommandHandler(cmdType, handler)
    g_commandHandlers[cmdType] = handler
end

-- Connection check
function isConnected()
    return g_connected
end

-- Memory read functions
function readMemory(address, size)
    if type(address) == "string" then
        address = tonumber(address, 16) or tonumber(address)
    end

    local bytes = {}
    for i = 0, size - 1 do
        local val = readBytes(address + i, 1, false)
        if val == nil then break end
        table.insert(bytes, val)
    end

    return table.concat(bytes, ",")
end

function readDword(address)
    if type(address) == "string" then
        address = tonumber(address, 16) or tonumber(address)
    end
    return readInteger(address)
end

function readFloat(address)
    if type(address) == "string" then
        address = tonumber(address, 16) or tonumber(address)
    end
    return readFloat(address)
end

function writeMemory(address, value)
    if type(address) == "string" then
        address = tonumber(address, 16) or tonumber(address)
    end

    if type(value) == "string" then
        local bytes = {}
        for b in value:gmatch("%d+") do
            table.insert(bytes, tonumber(b))
        end
        for i, v in ipairs(bytes) do
            writeBytes(address + i - 1, v, false)
        end
    else
        writeInteger(address, value)
    end
end

-- Scan functions
function scanExact(value, scanType)
    if type(value) == "string" then
        value = tonumber(value, 16) or tonumber(value)
    end

    if scanType == "hex" then
        value = tonumber(value, 16)
    end

    return scanExactValue(value)
end

-- Process functions
function getGameBase()
    -- Get first module base address
    local modules = enumModules()
    for _, mod in ipairs(modules) do
        if string.find(mod.name, "Game") or string.find(mod.name, "Ran") then
            return mod.Address
        end
    end
    return nil
end

-- Status display
print("")
print("========================================")
print("  MCP Bridge Ready!")
print("========================================")
print("Pipe: " .. PIPE_NAME)
print("")
print("Available functions:")
print("  isConnected()           - Check connection")
print("  readMemory(addr, size)   - Read bytes")
print("  readDword(addr)         - Read 4 bytes")
print("  readFloat(addr)         - Read float")
print("  writeMemory(addr, val)  - Write value")
print("  getGameBase()           - Get game module base")
print("")
print("Example:")
print('  readDword("0x310000")')
print('  scanExact(810)')
print("========================================")
