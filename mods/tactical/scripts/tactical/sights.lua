-- Sights. Native Tactical never shows the game's own scopes on its guns:
-- the KF7's and AR33's magnifiers and the sniper rifle's scope are all off,
-- and its optics (the SVD's PSO-1, the ACOGs, the variable-power scopes)
-- use their native profiles and housings when looks.lua can bind them
-- to the pack-optics renderer. Older engines still suppress the stock scopes.
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
