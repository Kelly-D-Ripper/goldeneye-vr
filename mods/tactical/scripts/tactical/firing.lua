-- Fire modes and damage.
--
-- The game asks weapon_fire_attempt once per shot, an automatic at each of
-- its rate's ticks, and a declined shot clicks dry with nothing more until
-- the trigger is let go. So semi and burst on an automatic are a count of
-- shots in one pull: past it, the shot is declined. A pull ends when the
-- ticks stop coming at the gun's rate, or after a decline.
--
-- Damage: a Tactical gun's multiplier is applied to the player's shots on
-- guards in before_damage, so guards' own guns of that type stay as they are.
local catalog = require "tactical.catalog"
local progress = require "tactical.progress"
local state = require "tactical.state"
local util = require "tactical.util"
local arsenal = require "tactical.arsenal"

local firing = {}

local SLACK = 0.05

local function automatic(itemId)
  local orig = state.original[itemId] or util.try(mod.weapon.stats, itemId)
  return orig ~= nil and orig.fireRate ~= nil
end

local function interval(itemId)
  local s = util.try(mod.weapon.stats, itemId)
  return (s and s.fireRate or 6) / 60
end

-- nil lets the shot go; false declines it.
function firing.attempt(hand, itemId)
  local key = state.carried[itemId]
  if not key or not automatic(itemId) then return nil end
  local def = catalog.byKey[key]
  local mode = progress.firemode(key)
  local limit = mode == "semi" and 1 or mode == "burst" and def.burst or math.huge
  local p = state.pull[hand]
  if p.last < 0 or state.clock - p.last > interval(itemId) * 1.5 + SLACK then p.count = 0 end
  p.count = p.count + 1
  p.last = state.clock
  if p.count > limit then
    p.count, p.last = 0, -1
    return false
  end
  return nil
end

function firing.cycleMode(hand)
  local itemId = state.held[hand]
  local key = itemId and state.carried[itemId]
  if not key then return nil end
  local modes = catalog.modes(catalog.byKey[key])
  if #modes < 2 then return nil end
  local now = progress.firemode(key)
  local nexti = 1
  for i, m in ipairs(modes) do if m == now then nexti = i % #modes + 1 end end
  progress.setFiremode(key, modes[nexti])
  -- one pulse per step: semi 1, burst 2, auto 3
  local pulses = modes[nexti] == "semi" and 1 or modes[nexti] == "burst" and 2 or 3
  for _ = 1, pulses do util.try(mod.hands.haptic, hand, 0.6, 0.06) end
  return modes[nexti]
end

-- The multiplier for a shot by a player's gun: this headset's own, or (on
-- the co-op host) the gun a player told us about.
function firing.multiplier(slot, itemId)
  local key
  if slot == nil or slot == (mod.net.slot() or 0) then
    key = state.carried[itemId]
  elseif state.peers[slot] then
    key = state.peers[slot][itemId]
  end
  local def = key and catalog.byKey[key]
  if not def then return 1 end
  local m = util.clamp(def.damage, 0.75, 1.75)
  -- a gun on a borrowed cartridge gun hits as it would on its home one
  if itemId ~= def.itemId then
    local home, here = arsenal.baseDamage(def.itemId), arsenal.baseDamage(itemId)
    if home and here and here > 0 then m = m * home / here end
  end
  return m
end

function firing.beforeDamage(d)
  if d.target ~= "guard" or d.by == nil or d.item == nil then return nil end
  local m = firing.multiplier(d.by, d.item)
  local g = state.guards[d.id]
  if g and g.armoured and d.part == "body" then m = m * 0.5 end
  if m == 1 then return nil end
  return util.clamp(d.amount * m, 0, 1000)
end

return firing
