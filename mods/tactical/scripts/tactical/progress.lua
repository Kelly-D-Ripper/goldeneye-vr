-- Progression and stats (Tactical 0.18.4 rules, agreed with the player on
-- 2026-11-23), kept in the mod's own save, progress.txt:
--
--  * Asked once per save whether to unlock everything; never offered again.
--  * A progression save starts with the Walther PPK only.
--  * Bring it home: a gun carried at a mission's completion unlocks. Dying
--    or leaving loses what was picked up.
--  * Reserve: each first clear of a mission, and every third replay, earns
--    one reserve token. A token buys a locked gun once the campaign has that
--    gun's reserveAt clears.
--  * Each successful clear with a gun you owned when the mission began
--    counts for that gun; its magazines unlock at their clear counts.
--  * Stats count on every save.
local catalog = require "tactical.catalog"

local progress = {}

local FILE = "progress.txt"
local REPLAYS_PER_RESERVE = 3
local STARTER = 1   -- Walther PPK

progress.UNSET, progress.EARN, progress.ALL = 0, 1, 2

local s

local function fresh()
  return {
    mode = progress.UNSET, reserve = 0, replays = 0, cleared = {}, missions = 0,
    guns = {},          -- key -> { unlocked, kills, headshots, shots, hits, clears }
    loadout = { primary = -1, pistol = STARTER },
    magazine = {},      -- key -> chosen capacity
    firemode = {},      -- key -> "semi" | "burst" | "auto"
  }
end

local function gun(key)
  local g = s.guns[key]
  if not g then
    g = { unlocked = false, kills = 0, headshots = 0, shots = 0, hits = 0, clears = 0 }
    s.guns[key] = g
  end
  return g
end

function progress.load()
  s = fresh()
  local text = mod.save.read(FILE)
  if not text then
    gun(STARTER).unlocked = true
    return
  end
  for line in string.gmatch(text, "[^\n]+") do
    local f = {}
    for w in string.gmatch(line, "%S+") do f[#f + 1] = w end
    local tag = f[1]
    if tag == "mode" then
      s.mode = tonumber(f[2]) or progress.UNSET
    elseif tag == "reserve" then
      s.reserve, s.replays = tonumber(f[2]) or 0, tonumber(f[3]) or 0
    elseif tag == "cleared" then
      for i = 2, #f do s.cleared[tonumber(f[i])] = true end
    elseif tag == "missions" then
      s.missions = tonumber(f[2]) or 0
    elseif tag == "gun" and catalog.byKey[tonumber(f[2])] then
      local g = gun(tonumber(f[2]))
      g.unlocked = f[3] == "1"
      g.kills, g.headshots, g.shots = tonumber(f[4]) or 0, tonumber(f[5]) or 0, tonumber(f[6]) or 0
      g.hits, g.clears = tonumber(f[7]) or 0, tonumber(f[8]) or 0
    elseif tag == "loadout" then
      s.loadout.primary, s.loadout.pistol = tonumber(f[2]) or -1, tonumber(f[3]) or -1
    elseif tag == "mag" and catalog.byKey[tonumber(f[2])] then
      s.magazine[tonumber(f[2])] = tonumber(f[3])
    elseif tag == "firemode" and catalog.byKey[tonumber(f[2])] then
      s.firemode[tonumber(f[2])] = f[3]
    end
  end
  gun(STARTER).unlocked = true
end

function progress.save()
  local out = { "GEVRTACTICAL1", "mode " .. s.mode, "reserve " .. s.reserve .. " " .. s.replays,
                "missions " .. s.missions }
  local cleared = {}
  for stage in pairs(s.cleared) do cleared[#cleared + 1] = stage end
  table.sort(cleared)
  out[#out + 1] = "cleared " .. table.concat(cleared, " ")
  for _, g in ipairs(catalog.guns) do
    local p = s.guns[g.key]
    if p then
      out[#out + 1] = string.format("gun %d %d %d %d %d %d %d", g.key, p.unlocked and 1 or 0,
        p.kills, p.headshots, p.shots, p.hits, p.clears)
    end
    if s.magazine[g.key] then out[#out + 1] = "mag " .. g.key .. " " .. s.magazine[g.key] end
    if s.firemode[g.key] then out[#out + 1] = "firemode " .. g.key .. " " .. s.firemode[g.key] end
  end
  out[#out + 1] = "loadout " .. s.loadout.primary .. " " .. s.loadout.pistol
  mod.save.write(FILE, table.concat(out, "\n") .. "\n")
end

function progress.mode() return s.mode end
function progress.locks() return s.mode ~= progress.ALL end

-- The one-time question. Answered once, never asked again.
function progress.answer(unlockAll)
  if s.mode ~= progress.UNSET then return end
  s.mode = unlockAll and progress.ALL or progress.EARN
  progress.save()
end

function progress.owned(key)
  if not progress.locks() then return true end
  local g = s.guns[key]
  return g ~= nil and g.unlocked
end

function progress.unlockedCount()
  local n = 0
  for _, g in ipairs(catalog.guns) do if progress.owned(g.key) then n = n + 1 end end
  return n
end

function progress.gunStats(key) return gun(key) end

function progress.totalClears()
  local n = 0
  for _ in pairs(s.cleared) do n = n + 1 end
  return n
end

function progress.reserve() return s.reserve end

-- Every clear of a mission, first or replay, as a campaign total.
function progress.missions() return s.missions end

-- Capacities this gun may use now.
function progress.magazineAllowed(key, cap)
  local g = catalog.byKey[key]
  local need = g and g.magazines[cap]
  if need == nil then return false end
  if not progress.locks() then return true end
  return gun(key).clears >= need
end

function progress.magazineChoice(key)
  local g = catalog.byKey[key]
  local cap = s.magazine[key]
  if cap and progress.magazineAllowed(key, cap) then return cap end
  if g.magazines[g.stock] ~= nil and progress.magazineAllowed(key, g.stock) then return g.stock end
  for _, c in ipairs(catalog.capacities(g)) do
    if progress.magazineAllowed(key, c) then return c end
  end
  return g.stock
end

function progress.setMagazine(key, cap)
  if progress.magazineAllowed(key, cap) then s.magazine[key] = cap; progress.save(); return true end
  return false
end

function progress.firemode(key)
  local g = catalog.byKey[key]
  local m = s.firemode[key]
  for _, have in ipairs(catalog.modes(g)) do if have == m then return m end end
  return catalog.defaultMode(g)
end

function progress.setFiremode(key, m)
  s.firemode[key] = m
  progress.save()
end

function progress.loadout() return s.loadout end

function progress.setLoadout(primary, pistol)
  s.loadout.primary, s.loadout.pistol = primary, pistol
  progress.save()
end

-- Why a locked gun is locked, for the pages (fits a 160-character text).
function progress.requirement(key)
  local g = catalog.byKey[key]
  if progress.owned(key) then return g.name .. " is yours." end
  local need = g.reserveAt
  return string.format("%s: bring one home from a mission, or spend 1 reserve after %d mission clears (%d now, reserve %d).",
    g.name, need, progress.totalClears(), s.reserve)
end

-- Spend a reserve token on a locked gun.
function progress.requisition(key)
  local g = catalog.byKey[key]
  if not g or progress.owned(key) or s.reserve < 1 or progress.totalClears() < g.reserveAt then
    return false
  end
  s.reserve = s.reserve - 1
  gun(key).unlocked = true
  progress.save()
  return true
end

-- Stats: the local player's own bullets, in every mode.
function progress.shot(key) local g = gun(key); g.shots = g.shots + 1 end
function progress.hit(key, head)
  local g = gun(key)
  g.hits = g.hits + 1
  if head then g.headshots = g.headshots + 1 end
end
function progress.kill(key) local g = gun(key); g.kills = g.kills + 1 end

function progress.totals()
  local t = { kills = 0, headshots = 0, shots = 0, hits = 0 }
  for _, g in pairs(s.guns) do
    t.kills, t.headshots = t.kills + g.kills, t.headshots + g.headshots
    t.shots, t.hits = t.shots + g.shots, t.hits + g.hits
  end
  return t
end

-- A mission completed. ownedAtStart: keys owned when it began. carried:
-- keys brought home. Returns what unlocked, for the debrief.
function progress.missionComplete(stage, ownedAtStart, carried)
  local result = { guns = {}, magazines = {}, reserve = 0 }
  s.missions = s.missions + 1
  if progress.locks() then
    if not s.cleared[stage] then
      s.cleared[stage] = true
      s.reserve = s.reserve + 1
      result.reserve = 1
    else
      s.replays = s.replays + 1
      if s.replays >= REPLAYS_PER_RESERVE then
        s.replays = 0
        s.reserve = s.reserve + 1
        result.reserve = 1
      end
    end
    for key in pairs(carried) do
      local g = gun(key)
      if not g.unlocked then
        g.unlocked = true
        result.guns[#result.guns + 1] = key
      elseif ownedAtStart[key] then
        g.clears = g.clears + 1
        for cap, need in pairs(catalog.byKey[key].magazines) do
          if need == g.clears then result.magazines[#result.magazines + 1] = { key = key, cap = cap } end
        end
      end
    end
  else
    s.cleared[stage] = true
    for key in pairs(carried) do
      if ownedAtStart[key] then local g = gun(key); g.clears = g.clears + 1 end
    end
  end
  progress.save()
  return result
end

-- Stats survive a failed mission; unlocks don't.
function progress.missionFailed()
  progress.save()
end

return progress
