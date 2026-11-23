-- Co-op over mod.net.
--
-- Guard damage and kills are told only where the guards run (the host), and
-- each player's Tactical guns live in that player's script. So:
--  * each client tells the host which Tactical gun each of its cartridge
--    guns is ("guns"), so the host can apply that gun's damage;
--  * the host tells each client of its own hits and kills ("stat"), so the
--    client's progression counts them.
-- Progression, magazines and loot stay on each player's own headset.
local catalog = require "tactical.catalog"
local state = require "tactical.state"
local progress = require "tactical.progress"

local coop = {}

local lastSent = ""

function coop.role() return mod.net.role() end

function coop.myslot() return mod.net.slot() or 0 end

-- Clients: send the carried guns when they change.
function coop.shareGuns()
  if mod.net.role() ~= "client" then return end
  local parts = {}
  for itemId, key in pairs(state.carried) do parts[#parts + 1] = itemId .. ":" .. key end
  table.sort(parts)
  local text = table.concat(parts, ",")
  if text ~= lastSent then
    if mod.net.send("guns", text) then lastSent = text end
  end
end

-- Host: a player's own hit or kill, sent to that player.
function coop.tellStat(slot, kind, itemId, head)
  if slot == nil or slot == coop.myslot() then return false end
  mod.net.send("stat", string.format("%s %d %d", kind, itemId, head and 1 or 0), slot)
  return true
end

function coop.init()
  mod.net.on("guns", function(data, from)
    if mod.net.role() ~= "host" and mod.net.role() ~= "server" then return end
    if from == nil then return end
    local map = {}
    for item, key in string.gmatch(data, "(%d+):(%d+)") do
      item, key = tonumber(item), tonumber(key)
      local def = catalog.byKey[key]
      if def and def.itemId == item then map[item] = key end
    end
    state.peers[from] = map
  end)
  mod.net.on("stat", function(data)
    local kind, item, head = string.match(data, "^(%a+) (%d+) (%d)$")
    local key = item and state.carried[tonumber(item)]
    if not key then return end
    if kind == "hit" then progress.hit(key, head == "1")
    elseif kind == "kill" then progress.kill(key) end
  end)
end

function coop.missionReset()
  lastSent = ""
end

return coop
