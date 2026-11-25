-- Guard rosters and reinforcements (gevr_tactical_loadouts.c and
-- gevr_tactical_reinforcements.inc.h in the 0.18.4 build).
--
-- Rosters run at load on every headset, from a roll of the mission and the
-- guard's id, so every headset arms the setup's guards the same way.
-- Reinforcements run where the guards do (solo, the co-op host).
local catalog = require "tactical.catalog"
local arsenal = require "tactical.arsenal"
local state = require "tactical.state"
local util = require "tactical.util"

local guards = {}

-- Bodies (the cartridge's body table rows).
local JUNGLE_COMMANDO, RUSSIAN_SOLDIER, RUSSIAN_INFANTRY, JANUS_SF = 0, 2, 3, 4
local JANUS_MARINE, SIBERIAN_1, SIBERIAN_2, ARCTIC_COMMANDO = 20, 22, 40, 41
local RUSSIAN = { [RUSSIAN_SOLDIER] = true, [RUSSIAN_INFANTRY] = true, [SIBERIAN_1] = true, [SIBERIAN_2] = true }
local DAM, TRAIN = 33, 25

-- Gun keys (guns.lua).
local AK, MP5, MP5K, AK74N, SKORPION, MP9, EVO3, M4A1, HK416, ASVAL, P90 = 0, 2, 3, 4, 6, 11, 12, 16, 17, 18, 21

local MP7A2 = 30
-- { key, preset }: gevr_tactical_loadouts.c's roster lists, with their Workshop presets
local RIFLES = { { AK74N, "-" }, { AK, "-" }, { AK74N, "Guard Wooden Grip" }, { AK, "Guard Short Wooden Grip" } }
local RARE_RIFLES = { { AK, "Suppressed Wood" }, { AK74N, "Border Patrol" } }
local MARINE = { { MP5, "-" }, { MP5K, "-" }, { EVO3, "-" }, { P90, "P90 Default" }, { AK, "Vympel" },
                 { MP5K, "P1" }, { MP5, "MP5SD Holo" }, { MP5, "SAS Entry" }, { MP5, "Blackout" }, { SKORPION, "-" } }
local ELITE = { { ASVAL, "OPK Elite" }, { M4A1, "Elite Short" }, { HK416, "Night Detail" }, { HK416, "Dark Shorty" },
                { AK74N, "Guard Elite" } }
local ELITE_CHOICES = { 1, 1, 2, 2, 3, 4, 5, 5 }
local JUNGLE = { { M4A1, "Pine Watch" }, { M4A1, "Pine Watch Suppressed" } }
local ELITE_MP9 = { MP9, "Tactical" }   -- with an MP7A2 "Tactical Tan" in the other hand natively
local GUARD_P90 = { P90, "P90 Default" }
local RARE_PERCENT = 8

-- Train: ten of the regular Marines wear vests (gevr_tactical_loadouts.c trainArmouredMarine).
local TRAIN_ARMOURED = { [2] = true, [5] = true, [14] = true, [18] = true, [29] = true,
                         [31] = true, [36] = true, [43] = true, [46] = true, [51] = true }

local function rosterKey(stage, body, id, original)
  local draw = util.draw(stage, id)
  local orig = catalog.itemName[original]
  if orig == "grenadelaunch" or orig == "rocketlaunch" then return nil end
  if RUSSIAN[body] then
    if stage == DAM or orig ~= "ak47" then return nil end
    local rate = draw % 100
    if rate < RARE_PERCENT then return table.unpack(RARE_RIFLES[rate % #RARE_RIFLES + 1]) end
    return table.unpack(RIFLES[(rate - RARE_PERCENT) * #RIFLES // (100 - RARE_PERCENT) + 1])
  elseif body == JANUS_MARINE or body == ARCTIC_COMMANDO then
    return table.unpack(MARINE[draw % #MARINE + 1])
  elseif body == JANUS_SF then
    if draw % 100 < 18 then return table.unpack(ELITE_MP9) end
    return table.unpack(ELITE[ELITE_CHOICES[draw % #ELITE_CHOICES + 1]])
  elseif body == JUNGLE_COMMANDO then
    return table.unpack(JUNGLE[draw % #JUNGLE + 1])
  elseif orig == "fnp90" then
    return table.unpack(GUARD_P90)
  end
  return nil
end

-- guard_spawning: a setup guard's gun from the roster.
function guards.spawning(g)
  if not mod.setting("guard_rosters", true) or g.weapon == nil then return nil end
  local key, preset = rosterKey(state.stage, g.body, g.id, g.weapon)
  local entry = { key = key or catalog.defaultKey(g.weapon), preset = key and preset or "-" }
  entry.armoured = state.stage == TRAIN and g.body == JANUS_MARINE and TRAIN_ARMOURED[g.id] or false
  state.guards[g.id] = entry
  if not key then return nil end
  local item = catalog.byKey[key].itemId
  if item == g.weapon then return nil end
  return { weapon = item }
end

local function droppedKey(id, item)
  local g = state.guards[id]
  if g and g.key and catalog.byKey[g.key].itemId == item then return g.key, g.preset end
  return catalog.defaultKey(item), "-"
end

function guards.dropped(pickup, item, from)
  if type(from) == "number" then
    local key, preset = droppedKey(from, item)
    arsenal.trackDrop(pickup, item, key, preset)
  end
end

-- Older hosts lack the drop event; use guard kills only for those hosts.
function guards.killed(id)
  if arsenal.hasPickupIds then return end
  local info = util.try(mod.guards.get, id)
  local item = info and info.weapon
  if item and catalog.isGunItem(item) then
    local key, preset = droppedKey(id, item)
    arsenal.noteDrop(item, key, preset)
  end
end

---------------------------------------------------------------------------
-- Reinforcements: a guard that spots a player calls a wave. Waves arrive
-- at the mission's approach pads; maps the 0.18.4 build had none for keep
-- their own guards only (the API has no pad positions to pick from).

-- activeMax, missionMax, waveSize, secondsBetween (user, 2026-11-23: 0.17.1's numbers)
local RULES = {
  [33] = { 10, 30, 4, 2.8 }, [34] = { 8, 24, 3, 3.2 }, [35] = { 12, 36, 5, 2.8 },
  [36] = { 14, 40, 6, 3.0 }, [9] = { 7, 20, 3, 3.6 }, [20] = { 8, 24, 3, 3.4 },
  [26] = { 5, 15, 2, 2.4 }, [43] = { 14, 40, 6, 3.0 }, [27] = { 8, 24, 3, 3.4 },
  [22] = { 8, 22, 3, 3.4 }, [24] = { 8, 24, 3, 3.4 }, [29] = { 14, 40, 6, 3.0 },
  [30] = { 11, 33, 5, 3.0 }, [25] = { 5, 14, 2, 2.6 }, [37] = { 14, 40, 6, 3.0 },
  [23] = { 7, 20, 3, 3.8 }, [39] = { 7, 20, 3, 3.8 }, [41] = { 5, 14, 2, 2.6 },
  [28] = { 7, 18, 3, 3.6 }, [32] = { 5, 12, 2, 3.2 },
}
local DEFAULT_RULE = { 7, 20, 3, 3.6 }
local APPROACHES = {
  [33] = { { 97, 105, 100 } },
  [35] = { { 90, 148, 125 } },
  [36] = { { 104, 102, 105 }, { 183, 180, 181, 194 } },
  [43] = { { 173, 172, 184 } },
  [29] = { { 139, 228, 140 } },
  [30] = { { 207, 308, 204 }, { 64, 63, 244, 242, 243 } },
  [37] = { { 382, 379, 472, 376, 378, 491, 381 }, { 246, 404, 478, 158, 419, 412 } },
}
local FIRST_CALL = 8          -- seconds into a mission before the first call
local MEMBER_GAP = 0.45
local WAVE_DEADLINE = 8

local function runsGuards()
  local role = mod.net.role()
  return role == "solo" or role == "host" or role == "server"
end

local function living()
  local r, n = state.reinforcements, 0
  for id in pairs(r.ids) do
    local g = util.try(mod.guards.get, id)
    if g and g.alive then n = n + 1 else r.ids[id] = nil end
  end
  return n
end

function guards.alerted(id, how)
  if not runsGuards() or not mod.setting("reinforcements", true) then return end
  local r = state.reinforcements
  local pads = APPROACHES[state.stage]
  if not pads or r.wave or r.ids[id] or state.clock < FIRST_CALL or state.clock < r.after then return end
  local rule = RULES[state.stage] or DEFAULT_RULE
  local caller = util.try(mod.guards.get, id)
  if not caller then return end
  local room = math.min(rule[3], rule[1] - living(), rule[2] - r.count)
  if room <= 0 then return end
  r.sequence = r.sequence + 1
  r.wave = { body = caller.body, left = room, next = state.clock + MEMBER_GAP,
             deadline = state.clock + WAVE_DEADLINE, pads = pads[(r.sequence - 1) % #pads + 1], pad = 1 }
  r.after = state.clock + rule[4]
  mod.log(string.format("reinforcements: wave %d of %d called by guard %d (%s)", r.sequence, room, id, how))
end

function guards.tick()
  local r = state.reinforcements
  local w = r.wave
  if not w then return end
  if state.clock > w.deadline then r.wave = nil; return end
  if state.clock < w.next then return end
  w.next = state.clock + MEMBER_GAP
  local seq = r.count + 1
  local key = rosterKey(state.stage, w.body, 1000 + seq, mod.items.ak47) or catalog.defaultFor.ak47
  for _ = 1, #w.pads do
    local pad = w.pads[w.pad]
    w.pad = w.pad % #w.pads + 1
    local id = util.try(mod.guards.spawn, { pad = pad, body = w.body, ai = "chase",
                                            weapon = catalog.byKey[key].itemId })
    if id then
      r.ids[id] = true
      r.count = r.count + 1
      state.guards[id] = { key = key, armoured = false }
      w.left = w.left - 1
      break
    end
  end
  if w.left <= 0 then r.wave = nil end
end

guards.rosterKey = rosterKey
return guards
