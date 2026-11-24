-- Sights. Native Tactical never shows the game's own scopes on its guns:
-- the KF7's and AR33's magnifiers and the sniper rifle's scope are all off,
-- and its optics (the SVD's PSO-1, the ACOGs, the variable-power scopes)
-- are drawn by Tactical's own lens renderer, with their reticles, zoom
-- ranges and night and thermal modes. That renderer isn't in the mod API
-- (PARITY.md, C2), so until it is, Tactical guns here have no magnified
-- optic at all, rather than the game's scope standing in for theirs.
-- Each draw puts the game's own scope back, so this runs in weapon_drawn.
local catalog = require "tactical.catalog"
local state = require "tactical.state"
local util = require "tactical.util"

local sights = {}

local OWN_SCOPE = { ak47 = true, m16 = true, sniperrifle = true }

function sights.drawn(hand, itemId)
  if not mod.weapon.scope then return end   -- a game build before mod.weapon.scope
  local name = catalog.itemName[itemId]
  if state.carried[itemId] and OWN_SCOPE[name] then
    util.try(mod.weapon.scope, hand, false)
  end
end

return sights
