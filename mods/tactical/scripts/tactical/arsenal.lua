-- Which Tactical gun each cartridge gun is, the starting kit, carry limits,
-- and the per-gun stats the game's weapon types carry while one is held.
--
-- The game's inventory holds one of each cartridge gun, and several Tactical
-- guns ride on the same one (the M4A1, HK416, L85A2, AUG, FAMAS and KAC PDW
-- all on the AR33). So each gun the player carries gets a cartridge gun of
-- its own: its home one when that's free, else a free one from a pool that
-- fires the same way and, where it can, takes the same ammo. An M4A1 in the
-- hands and an HK416 on the back are then two guns, the HK416 on the KF7,
-- sharing one rig of STANAG magazines.
local catalog = require "tactical.catalog"
local progress = require "tactical.progress"
local state = require "tactical.state"
local util = require "tactical.util"

local arsenal = {}

-- The older script host did not expose runtime pickup IDs or drop events.
arsenal.hasPickupIds = mod.pickups and type(mod.pickups.drop) == "function"

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

-- Compatibility with hosts that do not report the actual dropped pickup.
function arsenal.noteDrop(itemId, key)
  local list = state.drops[itemId]
  if not list then list = {}; state.drops[itemId] = list end
  list[#list + 1] = { key = key, at = state.clock }
end

-- Modern hosts report the world pickup that received the guard's weapon.
-- That pickup keeps the identity until it is taken or freed, regardless of
-- which other guards drop a weapon on the same cartridge slot in between.
function arsenal.trackDrop(pickup, itemId, key)
  if pickup == nil or not catalog.isGunItem(itemId) then return end
  state.pickupDrops[pickup] = { item = itemId, key = key }
end

function arsenal.removeDrop(pickup)
  if pickup ~= nil then state.pickupDrops[pickup] = nil end
end

local function identify(itemId, pickup)
  if arsenal.hasPickupIds then
    local d = pickup ~= nil and state.pickupDrops[pickup]
    arsenal.removeDrop(pickup)
    return d and d.item == itemId and d.key or catalog.defaultKey(itemId)
  end
  -- Legacy hosts only know that a guard with this gun died recently.
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

-- Cartridge guns a Tactical gun can borrow. Not the golden guns (one-shot
-- kills), the Moonraker laser (no ammo), the pump shotgun (its rack reads a
-- lower count as a shot) or the launchers (their own projectiles).
local POOL = { "m16", "ak47", "spectre", "mp5k", "mp5ksil", "uzi", "fnp90", "skorpion", "autoshot",
               "wppk", "wppksil", "tt33", "silverwppk", "ruger", "sniperrifle" }
local AUTOMATIC = { m16 = true, ak47 = true, spectre = true, mp5k = true, mp5ksil = true, uzi = true,
                    fnp90 = true, skorpion = true, autoshot = true }
local PISTOL_LOOK = { wppk = true, wppksil = true, tt33 = true, silverwppk = true, ruger = true }

function arsenal.carries(key)
  for _, k in pairs(state.carried) do if k == key then return true end end
  return false
end

local function free(itemId)
  return state.carried[itemId] == nil and not util.try(mod.inventory.has, itemId)
end

-- The cartridge gun this Tactical gun should take now, or nil when none is free.
function arsenal.slotFor(key)
  local def = catalog.byKey[key]
  if free(def.itemId) then return def.itemId end
  if catalog.noMagazine[key] then return nil end   -- launchers keep their own
  local ammo = catalog.ammo(def.itemId)
  local needsAuto = def.modes & 6 ~= 0
  local best, bestScore
  for i, name in ipairs(POOL) do
    local id = mod.items[name]
    if free(id) then
      local score = (catalog.ammoOf[name] == ammo and 8 or 0)
        + ((needsAuto and AUTOMATIC[name]) and 16 or 0)
        + ((not needsAuto and not AUTOMATIC[name]) and 1 or 0)
        + (((def.slot == "pistol") == (PISTOL_LOOK[name] == true)) and 4 or 0)
        - i / 100
      if not best or score > bestScore then best, bestScore = id, score end
    end
  end
  return best
end

-- Put a Tactical gun in the inventory on a cartridge gun of its own.
function arsenal.issue(key)
  if arsenal.carries(key) then return nil end
  local itemId = arsenal.slotFor(key)
  if not itemId then
    mod.log("no free cartridge gun for " .. catalog.byKey[key].name)
    return nil
  end
  util.try(mod.inventory.give, itemId)
  state.carried[itemId] = key
  enforceCarry(catalog.byKey[key].slot, itemId)
  return itemId
end

-- A cartridge gun in the inventory (held or drawn) that has no Tactical gun yet.
function arsenal.carry(itemId, pickup)
  if not catalog.isGunItem(itemId) then return state.carried[itemId] end
  local key = state.carried[itemId]
  if not key then
    key = identify(itemId, pickup)
    if not key then return nil end
    state.carried[itemId] = key
    enforceCarry(catalog.byKey[key].slot, itemId)
  end
  return key
end

-- item_pickup: a looted gun. When the player already carries a different
-- Tactical gun on that cartridge gun, the game only gave its rounds, so the
-- looted gun gets a cartridge gun of its own.
function arsenal.pickedUp(itemId, pickup)
  local had = state.carried[itemId]
  if not had then return arsenal.carry(itemId, pickup) end
  local key = identify(itemId, pickup)
  if key and key ~= had and not arsenal.carries(key) then
    local slot = arsenal.issue(key)
    if slot then mod.log(catalog.byKey[key].name .. " carried on the " .. catalog.itemName[slot]) end
    return key
  end
  return had
end

-- The starting kit: the loadout's primary and pistol in place of the
-- mission's own guns.
function arsenal.applyKit(stage)
  if NO_STARTING_KIT[stage] or not mod.setting("starting_kit", true) then return end
  local lo = progress.loadout()
  -- the cartridge's own guns go; gadgets, keys and thrown weapons stay
  for name, id in pairs(mod.items) do
    if catalog.ammoOf[name] and util.try(mod.inventory.has, id) then util.try(mod.inventory.take, id) end
  end
  state.carried = {}
  for _, key in ipairs({ lo.primary, lo.pistol }) do
    if catalog.byKey[key] and progress.owned(key) and arsenal.issue(key) then
      arsenal.gun(key).cap = progress.magazineChoice(key)
    end
  end
  state.kitApplied = true
end

-- The cartridge gun's own damage, for the multiplier (the item a gun
-- borrows may hit harder or softer than its home one).
function arsenal.baseDamage(itemId)
  if not state.original[itemId] then state.original[itemId] = util.try(mod.weapon.stats, itemId) end
  local st = state.original[itemId]
  return st and st.damage or nil
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
    if arsenal.carries(state.lastPrimary) then keys[state.lastPrimary] = true end
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
