-- Magazines and the chamber, in Lua over the game's own reload.
--
-- The game reloads from a spare-round reserve per ammo type, and both of
-- its reloads (the button's and the VR belt's) start only when that reserve
-- holds rounds. So Tactical keeps the reserve "pinned" to the rounds in the
-- next magazine it would seat (0 when there's none, so no reload starts),
-- and at reload_end sets the gun's rounds to that magazine's real count.
--
--  * The rig: 3 primary pouches and 2 pistol pouches (a PKM box takes all
--    of a primary's, an RPG round two), each one magazine.
--  * A reload keeps the old magazine, with its rounds, in a free pouch, or
--    drops it. A round in the chamber stays with the gun (+1).
--  * Rounds the game gives (ammo boxes, a looted gun's rounds, the
--    mission's starting ammo) are loose until packed into empty pouches.
local catalog = require "tactical.catalog"
local arsenal = require "tactical.arsenal"
local state = require "tactical.state"
local util = require "tactical.util"

local magazines = {}

local RELOAD_GRACE_TICKS = 2  -- ticks after a reload whose reserve changes aren't loose rounds
local POUCHES = { primary = 3, pistol = 2 }

local function pouch(slot) return state.rig[slot] end

local function used(slot, family)
  local n = 0
  for _, m in ipairs(pouch(slot)) do if family == nil or m.family == family then n = n + 1 end end
  return n
end

-- Room for one more magazine of this gun.
local function room(def)
  local slot = def.slot
  if used(slot) >= POUCHES[slot] then return false end
  return used(slot, def.family) < catalog.pouches(def)
end

function magazines.stow(def, cap, rounds)
  if rounds <= 0 or not room(def) then return false end
  local list = pouch(def.slot)
  list[#list + 1] = { family = def.family, cap = cap, rounds = rounds }
  return true
end

-- The fullest magazine this gun takes, left in its pouch.
local function best(def)
  local list, bi = pouch(def.slot), nil
  for i, m in ipairs(list) do
    if m.family == def.family and (not bi or m.rounds > list[bi].rounds) then bi = i end
  end
  return bi
end

function magazines.peek(def)
  local i = best(def)
  return i and pouch(def.slot)[i] or nil
end

function magazines.take(def)
  local i = best(def)
  if not i then return nil end
  return table.remove(pouch(def.slot), i)
end

-- Fill empty pouches from loose rounds, for the guns carried (the one in
-- hand first).
function magazines.pack()
  local order = {}
  for _, hand in ipairs(arsenal.HANDS) do
    local itemId = state.held[hand]
    if itemId and state.carried[itemId] then order[#order + 1] = itemId end
  end
  for itemId in pairs(state.carried) do order[#order + 1] = itemId end
  for _, itemId in ipairs(order) do
    local key = state.carried[itemId]
    local def = catalog.byKey[key]
    local ammo = catalog.ammo(itemId)
    if not catalog.noMagazine[key] and ammo then
      local cap = arsenal.gun(key).cap
      while (state.loose[ammo] or 0) > 0 and room(def) do
        local rounds = math.min(cap, state.loose[ammo])
        state.loose[ammo] = state.loose[ammo] - rounds
        magazines.stow(def, cap, rounds)
      end
    end
  end
end

-- What the game's reserve should hold for each ammo type in use: the next
-- magazine's rounds for the gun in hand (the gun hand first).
local function wanted()
  local want = {}
  for _, hand in ipairs(arsenal.HANDS) do
    local itemId = state.held[hand]
    local key = itemId and state.carried[itemId]
    if key and not catalog.noMagazine[key] then
      local def = catalog.byKey[key]
      local ammo = catalog.ammo(itemId)
      if ammo and want[ammo] == nil then
        local m = magazines.peek(def)
        want[ammo] = m and m.rounds or 0
      end
    end
  end
  return want
end

-- Each tick: take what the game added to a reserve as loose rounds, then
-- set every reserve in use back to its pin.
function magazines.sync()
  local want = wanted()
  local gained = false
  for ammo, pin in pairs(state.pinned) do
    local now = util.try(mod.ammo.get, ammo)
    if now and now > pin and (state.reloading[ammo] or -1) < state.ticks then
      state.loose[ammo] = (state.loose[ammo] or 0) + (now - pin)
      gained = true
    end
  end
  if gained then
    magazines.pack()
    want = wanted()
  end
  for ammo, pin in pairs(want) do
    if util.try(mod.ammo.get, ammo) ~= pin then util.try(mod.ammo.set, ammo, pin) end
    state.pinned[ammo] = pin
  end
end

-- Every ammo type a carried gun uses starts pinned at what the game holds,
-- so the mission's starting rounds count as loose.
function magazines.begin()
  local seen = {}
  for itemId, key in pairs(state.carried) do
    local ammo = catalog.ammo(itemId)
    if ammo and not seen[ammo] and not catalog.noMagazine[key] then
      seen[ammo] = true
      state.loose[ammo] = (state.loose[ammo] or 0) + (util.try(mod.ammo.get, ammo) or 0)
      state.pinned[ammo] = 0
      util.try(mod.ammo.set, ammo, 0)
    end
  end
  -- a fresh rig: full magazines in the pouches and one in each gun (0.18.4's starting kit)
  if state.kitApplied then
    for _, key in pairs(state.carried) do
      local def = catalog.byKey[key]
      local g = arsenal.gun(key)
      if not catalog.noMagazine[key] then
        g.loaded = g.cap + 1
        while room(def) do magazines.stow(def, g.cap, g.cap) end
      end
    end
  end
  magazines.pack()
end

function magazines.trackAmmo(itemId)
  local ammo = catalog.ammo(itemId)
  if ammo and state.pinned[ammo] == nil then
    state.pinned[ammo] = util.try(mod.ammo.get, ammo) or 0
  end
end

local function stowOrDrop(def, cap, rounds)
  if rounds > 0 and not magazines.stow(def, cap, rounds) then
    mod.log(string.format("%s: dropped a magazine with %d rounds (no free pouch)", def.name, rounds))
  end
end

local function pinNext(def, ammo)
  local m = magazines.peek(def)
  local pin = m and m.rounds or 0
  util.try(mod.ammo.set, ammo, pin)
  state.pinned[ammo] = pin
end

-- reload_start: the old magazine leaves the gun. It's stowed once the new
-- one is out of its pouch (reload_end), so a full rig keeps it.
function magazines.reloadStart(hand, itemId)
  local key = state.carried[itemId]
  if not key or catalog.noMagazine[key] then return end
  local def, g = catalog.byKey[key], arsenal.gun(key)
  if g.out then stowOrDrop(def, g.out.cap, g.out.rounds); g.out = nil end   -- a reload never finished
  local now = util.try(mod.weapon.loaded, hand) or 0
  local out
  if now > 0 then
    -- the button's reload: the gun still holds its rounds; one stays chambered
    g.chamber, out = 1, now - 1
  else
    -- a hand reload has already taken the magazine out (its rounds went
    -- to the hand or the reserve); the count is the last one seen
    g.chamber, out = 0, g.loaded or 0
  end
  g.out = { cap = g.cap, rounds = out }
  g.loaded = g.chamber
  local ammo = catalog.ammo(itemId)
  if ammo then
    state.reloading[ammo] = math.huge   -- the reserve's changes are the reload's until it ends
    pinNext(def, ammo)
  end
end

-- reload_end: the next magazine is seated, and the old one stowed.
function magazines.reloadEnd(hand, itemId)
  local key = state.carried[itemId]
  if not key or catalog.noMagazine[key] then return end
  local def, g = catalog.byKey[key], arsenal.gun(key)
  local m = magazines.take(def)
  if m then
    g.cap = m.cap
    arsenal.apply(itemId, key)
    g.loaded = util.try(mod.weapon.setLoaded, hand, m.rounds + g.chamber) or (m.rounds + g.chamber)
  else
    g.loaded = util.try(mod.weapon.setLoaded, hand, g.chamber) or g.chamber
  end
  g.chamber = 0
  if g.out then stowOrDrop(def, g.out.cap, g.out.rounds); g.out = nil end
  local ammo = catalog.ammo(itemId)
  if ammo then
    state.reloading[ammo] = state.ticks + RELOAD_GRACE_TICKS
    pinNext(def, ammo)
  end
end

-- reload_attempt: no magazine for this gun in the rig, no reload. (The
-- reserve's pin can't cover every case: a magazine let go of in a hand
-- reload gives its rounds back to the reserve.)
function magazines.attempt(hand, itemId)
  local key = state.carried[itemId]
  if not key or catalog.noMagazine[key] then return nil end
  if magazines.peek(catalog.byKey[key]) then return nil end
  return false
end

-- weapon_drawn: the gun comes up with its own rounds. The game has just
-- topped it up from the reserve, so the reserve is pinned again too.
function magazines.drawn(hand, itemId)
  local key = state.carried[itemId]
  if not key or catalog.noMagazine[key] then return end
  local def, g = catalog.byKey[key], arsenal.gun(key)
  if g.loaded == nil then
    g.loaded = util.try(mod.weapon.loaded, hand) or 0
  end
  g.loaded = util.try(mod.weapon.setLoaded, hand, g.loaded) or g.loaded
  local ammo = catalog.ammo(itemId)
  if ammo then
    state.reloading[ammo] = state.ticks + RELOAD_GRACE_TICKS
    pinNext(def, ammo)
  end
end

-- The rounds in each held gun, as the game counts them now. While a hand is
-- still switching to a gun, its count isn't the gun's: the game fills it from
-- the reserve, capped at the cartridge gun's own magazine (the gun's stats
-- are only applied at weapon_drawn), so it waits for the draw.
function magazines.observe(hand, itemId)
  local key = state.carried[itemId]
  if key and not catalog.noMagazine[key] and state.drawn[hand] == itemId then
    local n = util.try(mod.weapon.loaded, hand)
    if n then arsenal.gun(key).loaded = n end
  end
end

function magazines.summary(def)
  local mags, rounds = 0, 0
  for _, m in ipairs(pouch(def.slot)) do
    if m.family == def.family then mags, rounds = mags + 1, rounds + m.rounds end
  end
  return mags, rounds
end

return magazines
