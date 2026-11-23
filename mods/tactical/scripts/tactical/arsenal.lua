-- Which Tactical gun each cartridge gun is, the starting kit, carry limits,
-- and the per-gun stats the game's weapon types carry while one is held.
--
-- Several Tactical guns ride on one cartridge gun (the M4A1, HK416, L85A2,
-- AUG, FAMAS and KAC PDW all on the AR33), so the player carries at most one
-- of each family at a time; which one it is lives here.
local catalog = require "tactical.catalog"
local progress = require "tactical.progress"
local state = require "tactical.state"
local util = require "tactical.util"

local arsenal = {}

local HANDS = { "right", "left" }
local DROP_MEMORY = 90       -- seconds a killed guard's gun stays the likely loot
-- The solo intro records give these two missions fists and the watch magnet
-- (gevr_tactical_loadouts.h gevrTacticalMissionAllowsStartingKit).
local NO_STARTING_KIT = { [27] = true, [24] = true }   -- Bunker 2, Archives
local CARRY = { primary = 2, pistol = 2 }               -- hands and back; two pistol wells

function arsenal.key(itemId)
  return state.carried[itemId]
end

function arsenal.gun(key)
  local g = state.guns[key]
  if not g then
    local def = catalog.byKey[key]
    g = { loaded = nil, cap = progress.magazineChoice(key), chamber = 0, family = def.family }
    state.guns[key] = g
  end
  return g
end

-- A guard's gun that may soon be loot.
function arsenal.noteDrop(itemId, key)
  local list = state.drops[itemId]
  if not list then list = {}; state.drops[itemId] = list end
  list[#list + 1] = { key = key, at = state.clock }
end

-- Which Tactical gun a newly carried cartridge gun is: the newest guard's
-- gun of that item killed lately, else the item's own Tactical gun.
local function identify(itemId)
  local list = state.drops[itemId]
  while list and #list > 0 do
    local d = table.remove(list)
    if state.clock - d.at <= DROP_MEMORY then return d.key end
  end
  return catalog.defaultKey(itemId)
end

local function restore(itemId)
  local orig = state.original[itemId]
  if orig and state.applied[itemId] then
    local t = { magazine = orig.magazine, damage = orig.damage }
    if orig.fireRate then t.fireRate = orig.fireRate end
    util.try(mod.weapon.setStats, itemId, t)
  end
  state.applied[itemId] = nil
end

-- Give the held cartridge gun this Tactical gun's magazine and rate of fire.
-- Stats are per weapon type (guards' too), so they're put back once no hand
-- holds the item.
function arsenal.apply(itemId, key)
  local def = catalog.byKey[key]
  if not state.original[itemId] then
    state.original[itemId] = util.try(mod.weapon.stats, itemId)
  end
  local orig = state.original[itemId]
  if not orig then return end
  local g = arsenal.gun(key)
  local t = {}
  if not catalog.noMagazine[key] then t.magazine = util.clamp(g.cap + 1, 1, 255) end
  if orig.fireRate and def.rpm > 0 then
    t.fireRate = util.clamp(math.floor(3600 / def.rpm + 0.5), 1, 60)
  end
  if next(t) then util.try(mod.weapon.setStats, itemId, t) end
  state.applied[itemId] = key
end

local function holding(itemId)
  return state.held.right == itemId or state.held.left == itemId
end

function arsenal.releaseUnheld()
  for itemId in pairs(state.applied) do
    if not holding(itemId) then restore(itemId) end
  end
end

-- Carry: forget guns no longer in the inventory (dropped, taken by the game).
function arsenal.refreshCarried()
  for itemId in pairs(state.carried) do
    if not util.try(mod.inventory.has, itemId) then state.carried[itemId] = nil end
  end
end

local function countSlot(slot)
  local n = 0
  for _, key in pairs(state.carried) do
    if catalog.byKey[key].slot == slot then n = n + 1 end
  end
  return n
end

-- Over the carry limit, the oldest gun of that kind not in a hand goes.
local function enforceCarry(slot, keep)
  if not mod.setting("carry_limit", true) then return end
  while countSlot(slot) > CARRY[slot] do
    local victim
    for itemId, key in pairs(state.carried) do
      if itemId ~= keep and catalog.byKey[key].slot == slot and not holding(itemId) then
        if not victim or key == state.lastPrimary then victim = itemId end
      end
    end
    if not victim then return end
    util.try(mod.inventory.take, victim)
    mod.log("dropped " .. catalog.byKey[state.carried[victim]].name .. " (carry limit)")
    state.carried[victim] = nil
  end
end

function arsenal.carry(itemId)
  if not catalog.isGunItem(itemId) then return nil end
  local key = state.carried[itemId]
  if not key then
    key = identify(itemId)
    if not key then return nil end
    state.carried[itemId] = key
    enforceCarry(catalog.byKey[key].slot, itemId)
  end
  return key
end

-- The starting kit: the loadout's primary and pistol in place of the
-- mission's own guns.
function arsenal.applyKit(stage)
  for _, def in ipairs(catalog.guns) do
    if progress.owned(def.key) then state.ownedAtStart[def.key] = true end
  end
  if NO_STARTING_KIT[stage] or not mod.setting("starting_kit", true) then return end
  local lo = progress.loadout()
  -- the cartridge's own guns go; gadgets, keys and thrown weapons stay
  for itemId, _ in pairs(catalog.byItem) do
    if util.try(mod.inventory.has, itemId) then util.try(mod.inventory.take, itemId) end
  end
  for name, id in pairs(mod.items) do
    if catalog.ammoOf[name] and not catalog.byItem[id] and util.try(mod.inventory.has, id) then
      util.try(mod.inventory.take, id)
    end
  end
  state.carried = {}
  for _, key in ipairs({ lo.primary, lo.pistol }) do
    local def = catalog.byKey[key]
    if def and progress.owned(key) and not state.carried[def.itemId] then
      util.try(mod.inventory.give, def.itemId)
      state.carried[def.itemId] = key
      arsenal.gun(key).cap = progress.magazineChoice(key)
    end
  end
  state.kitApplied = true
end

-- The guns this player brings home: those in hand and the primary on the back.
function arsenal.broughtHome()
  local keys = {}
  for _, hand in ipairs(HANDS) do
    local itemId = state.held[hand]
    local key = itemId and state.carried[itemId]
    if key then keys[key] = true end
  end
  if state.lastPrimary then
    local def = catalog.byKey[state.lastPrimary]
    if def and state.carried[def.itemId] == state.lastPrimary then keys[state.lastPrimary] = true end
  end
  return keys
end

-- A hand's held item, as the tick sees it; the primary put away goes on the back.
function arsenal.noteHeld(hand, itemId)
  local before = state.held[hand]
  if before == itemId then return end
  local key = before and state.carried[before]
  if key and catalog.byKey[key].slot == "primary" then state.lastPrimary = key end
  state.held[hand] = itemId
end

arsenal.HANDS = HANDS
return arsenal
