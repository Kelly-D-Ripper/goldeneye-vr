-- Sights. The KF7 and AR33 carry the game's own magnifier, and the sniper
-- rifle its scope. A Tactical gun on one of them has iron sights unless its
-- stock build has a magnified optic, so the game's own lens comes off
-- (mod.weapon.scope(hand, false)). Each draw puts the game's own back, so
-- this runs in weapon_drawn.
local catalog = require "tactical.catalog"
local state = require "tactical.state"
local util = require "tactical.util"

local sights = {}

local OWN_SCOPE = { ak47 = true, m16 = true, sniperrifle = true }
-- Stock builds with a magnified optic, and the cartridge gun whose own
-- scope stands in for it: the SVD's PSO-1 on the sniper rifle.
local KEEPS_SCOPE = { [7] = "sniperrifle" }

function sights.drawn(hand, itemId)
  if not mod.weapon.scope then return end   -- a game build before mod.weapon.scope
  local key = state.carried[itemId]
  local name = catalog.itemName[itemId]
  if not key or not OWN_SCOPE[name] then return end
  if KEEPS_SCOPE[key] == name then return end
  util.try(mod.weapon.scope, hand, false)
end

return sights
